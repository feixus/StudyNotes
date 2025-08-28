#include "stdafx.h"
#include "RenderGraph.h"
#include "ResourceAllocator.h"
#include "Graphics/Profiler.h"
#include "Graphics/Core/Graphics.h"
#include "Graphics/Core/CommandContext.h"
#include "Core/CommandLine.h"

RGResourceHandle RGPassBuilder::Read(const RGResourceHandle& resource)
{
	m_RenderGraph.GetResourceNode(resource);
#if RG_DEBUG
	RG_ASSERT(m_Pass.ReadsFrom(resource) == false, "Pass already reads from this resource");
#endif
	m_Pass.m_Reads.push_back(resource);
	return resource;
}

RGResourceHandle& RGPassBuilder::Write(RGResourceHandle& resource)
{
#if RG_DEBUG
    RG_ASSERT(m_Pass.WritesTo(resource) == false, "Pass already writes to this resource");
#endif
    const RGNode& node = m_RenderGraph.GetResourceNode(resource);
    RG_ASSERT(node.m_pResource->m_Version == node.m_Version, "Version mismatch");
    ++node.m_pResource->m_Version;
    if (node.m_pResource->m_IsImported)
    {
        m_Pass.m_NeverCull = true;
    }
    resource.Invalidate();
    RGResourceHandle newResource = m_RenderGraph.CreateResourceNode(node.m_pResource);
    m_Pass.m_Writes.push_back(newResource);
    return newResource;
}

RGResourceHandle RGPassBuilder::CreateTexture(const char* name, const TextureDesc& desc)
{
    return m_RenderGraph.CreateTexture(name, desc);
}

RGResourceHandle RGPassBuilder::CreateBuffer(const char* name, const BufferDesc& desc)
{
    return m_RenderGraph.CreateBuffer(name, desc);
}

void RGPassBuilder::NeverCull()
{
    m_Pass.m_NeverCull = true;
}

RGGraph::RGGraph(Graphics* pGraphics, uint64_t allocatorSize)
    : m_pGraphics(pGraphics), m_Allocator(allocatorSize)
{
    m_ImmediateMode = CommandLine::GetBool("rgimmediate");

    m_pAllocator = std::make_unique<RGResourceAllocator>(pGraphics);
}

RGGraph::~RGGraph()
{
    DestroyData();
}

void RGGraph::DestroyData()
{
    for (RGPass* pPass : m_RenderPasses)
    {
        m_Allocator.Release(pPass);
    }
    m_RenderPasses.clear();
    for (RGResource* pResource : m_Resources)
    {
        m_Allocator.Release(pResource);
    }
    m_Resources.clear();
    m_RGNodes.clear();
    m_Aliases.clear();
}

void RGGraph::PushEvent(const char* pName)
{
    ProfileEvent e = {
        .pName = pName,
        .Begin = true,
        .PassIndex = (uint32_t)m_RenderPasses.size()
    };
    m_Events.push_back(e);
    m_EventStackSize++;
}

void RGGraph::PopEvent()
{
    RG_ASSERT(m_RenderPasses.size() > 0, "Can't pop event before a RenderPass has been added");
    RG_ASSERT(m_EventStackSize > 0, "No Event to Pop");
    ProfileEvent e = {
        .pName = nullptr,
        .Begin = false,
        .PassIndex = (uint32_t)m_RenderPasses.size() - 1
    };
    m_Events.push_back(e);
    m_EventStackSize--;
}

void RGGraph::Compile()
{
    // process all the resource aliases
    for (ResourceAlias& alias : m_Aliases)
    {
        const RGNode& fromNode = GetResourceNode(alias.From);
        const RGNode& toNode = GetResourceNode(alias.To);

        // reroute all "to" resource to be the "from" resource
        for (RGNode& node : m_RGNodes)
        {
            if (node.m_pResource == toNode.m_pResource)
            {
                node.m_pResource = fromNode.m_pResource;
            }
        }

        for (RGPass* pPass : m_RenderPasses)
        {
            // make all renderpasses that read from "From" also read from "To"
            if (pPass->ReadsFrom(alias.From))
            {
                if (pPass->ReadsFrom(alias.To) == false)
                {
                    pPass->m_Reads.push_back(alias.To);
                }
            }

            // remove any write to "From" should be removed
            for (size_t i = 0; i < pPass->m_Writes.size(); ++i)
            {
                if (pPass->m_Writes[i] == alias.From)
                {
                    std::swap(pPass->m_Writes[i], pPass->m_Writes[pPass->m_Writes.size() - 1]);
                    pPass->m_Writes.pop_back();
                    break;
                }
            }
        }
    }

    // set all the compile metadata
    for (RGPass* pPass : m_RenderPasses)
    {
        pPass->m_NeverCull = true;

        pPass->m_References = (int)pPass->m_Writes.size() + (int)pPass->m_NeverCull;

        for (RGResourceHandle read : pPass->m_Reads)
        {
            RGNode& node = m_RGNodes[read.Index];
            node.m_Reads++;
        }

        for (RGResourceHandle write : pPass->m_Writes)
        {
            RGNode& node = m_RGNodes[write.Index];
            node.m_pWriter = pPass;
            node.m_Reads++;
        }
    }

    // do the culling
    std::queue<RGNode*> stack;
    for (RGNode& node : m_RGNodes)
    {
        if (node.m_Reads == 0)
        {
            stack.push(&node);
        }
    }

    while (!stack.empty())
    {
        const RGNode* pNode = stack.front();
        stack.pop();

        RGPass* pWriter = pNode->m_pWriter;
        if (pWriter)
        {
            RG_ASSERT(pWriter->m_References >= 1, "Pass is expected to have references");
            --pWriter->m_References;
            if (pWriter->m_References == 0)
            {
                std::vector<RGResourceHandle>& reads = pWriter->m_Reads;
                for (RGResourceHandle& resource : reads)
                {
                    RGNode& node = m_RGNodes[resource.Index];   
                    --node.m_Reads;
                    if (node.m_Reads == 0)
                    {
                        stack.push(&node);
                    }
                }
            }
        }
    }

    // set the final reference count
    for (RGNode& node : m_RGNodes)
    {
        node.m_pResource->m_References += node.m_Reads;
    }
}

RGPass& RGGraph::AddPass(RGPass* pPass)
{
    m_RenderPasses.push_back(pPass);

    return *pPass;
}

int64_t RGGraph::Execute()
{
    if (m_ImmediateMode)
    {
        return m_LastFenceValue;
    }

    RG_ASSERT(m_EventStackSize == 0, "Missing PopEvent");

    CommandContext* pContext = m_pGraphics->AllocateCommandContext(D3D12_COMMAND_LIST_TYPE_DIRECT);
    for (int passIdx = 0; passIdx < m_RenderPasses.size(); passIdx++)
    {
        RGPass* pPass = m_RenderPasses[passIdx];

        ProcessEvents(*pContext, passIdx, true);
        if (pPass->m_References > 0)
        {
            ExecutePass(pPass, *pContext);
        }
        ProcessEvents(*pContext, passIdx, false);
    }
    m_LastFenceValue = pContext->Execute(false);

    DestroyData();
    return m_LastFenceValue;
}

void RGGraph::ExecutePass(RGPass* pPass, CommandContext& renderContext)
{
    PrepareResources(pPass);

    RGPassResource resources(*this, *pPass);

    // automatically insert resource barrier
    // check if we're in a graphics pass and automatically call BeginRenderPass

    {
        GPU_PROFILE_SCOPE(pPass->m_Name, &renderContext);
        pPass->Execute(renderContext, resources);
    }
    

    // check if we're in a graphics pass and automatically call EndRenderPass

    ReleaseResources(pPass);
}

void RGGraph::Present(RGResourceHandle resource)
{
    RG_ASSERT(IsValidHandle(resource), "resource is invalid");

    RGPassBuilder builder = AddPass("Present");
    builder.Bind([=](CommandContext&, const RGPassResource&){});
}

RGResourceHandle RGGraph::MoveResource(RGResourceHandle from, RGResourceHandle to)
{
    RG_ASSERT(IsValidHandle(to), "resource is invalid");
    const RGNode& node = GetResourceNode(from);
    m_Aliases.push_back(ResourceAlias{from, to});
    ++node.m_pResource->m_Version;
    return CreateResourceNode(node.m_pResource);
}

GraphicsTexture* RGPassResource::GetTexture(RGResourceHandle handle) const
{
    RG_ASSERT(m_Pass.ReadsFrom(handle) || m_Pass.WritesTo(handle), "Pass does not read or write to this resource");
	const RGNode& node = m_Graph.GetResourceNode(handle);
	RG_ASSERT(node.m_pResource->m_Type == RGResourceType::Texture, "Resource is not a texture");
	return static_cast<GraphicsTexture*>(node.m_pResource->m_pPhysicalResource);
}

RGResource* RGPassResource::GetResourceInternal(RGResourceHandle handle) const
{
	const RGNode& node = m_Graph.GetResourceNode(handle);
	return node.m_pResource;
}

void RGGraph::PrepareResources(RGPass* pPass)
{
    for (RGResourceHandle handle : pPass->m_Writes)
    {
        ConditionallyCreateResource(GetResource(handle));
    }
}

void RGGraph::ReleaseResources(RGPass* pPass)
{
    for (RGResourceHandle handle : pPass->m_Reads)
    {
        ConditionallyReleaseResource(GetResource(handle));
    }
}

void RGGraph::ConditionallyCreateResource(RGResource* pResource)
{
    if (pResource->m_pPhysicalResource == nullptr)
    {
        switch (pResource->m_Type)
        {
        case RGResourceType::Texture:
            pResource->m_pPhysicalResource = m_pAllocator->CreateTexture(static_cast<RGTexture*>(pResource)->GetDesc());
            break;
        case RGResourceType::Buffer:
            // pResource->m_pPhysicalResource = m_pAllocator->CreateBuffer(static_cast<BufferResource*>(pResource)->GetDesc());
            break;
        default:
            RG_ASSERT(false, "Invalid resource type");
        }
    }
}

void RGGraph::ConditionallyReleaseResource(RGResource* pResource)
{
    if (pResource->m_pPhysicalResource != nullptr)
    {
        switch (pResource->m_Type)
        {
        case RGResourceType::Texture:
            m_pAllocator->ReleaseTexture(static_cast<GraphicsTexture*>(pResource->m_pPhysicalResource));
            break;
        case RGResourceType::Buffer:
            // m_pAllocator->ReleaseBuffer(static_cast<GraphicsBuffer*>(pResource->m_pPhysicalResource));
            break;
        default:
            RG_ASSERT(false, "Invalid resource type");
        }
    }
}

void RGGraph::ProcessEvents(CommandContext& context, uint32_t passIdx, bool begin)
{
    for (uint32_t eventIdx = m_CurrentEvent; eventIdx < m_Events.size() && m_Events[eventIdx].PassIndex == passIdx; ++eventIdx)
    {
        ProfileEvent e = m_Events[eventIdx];
        if (e.Begin && begin)
        {
            m_CurrentEvent++;
            GPU_PROFILE_BEGIN(e.pName, &context);
        }
        else if (!e.Begin && !begin)
        {
            m_CurrentEvent++;
            GPU_PROFILE_END(&context);
        }
    }
}
