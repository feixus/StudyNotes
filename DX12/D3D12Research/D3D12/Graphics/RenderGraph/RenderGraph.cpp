#include "stdafx.h"
#include "RenderGraph.h"
#include "Graphics/Graphics.h"
#include "Graphics/CommandContext.h"
#include "Graphics/Profiler.h"
#include "ResourceAllocator.h"

RGResourceHandle RGPassBuilder::Read(const RGResourceHandle& resource)
{
	m_RenderGraph.GetResourceNode(resource);
#if RG_DEBUG
	RG_ASSERT(m_Pass.ReadsFrom(resource) == false, "Pass already reads from this resource");
#endif
	m_Pass.m_Reads.push_back(resource);
	return resource;
}

RGResourceHandleMutable& RGPassBuilder::Write(RGResourceHandleMutable& resource)
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
    RGResourceHandleMutable newResource = m_RenderGraph.CreateResourceNode(node.m_pResource);
    m_Pass.m_Writes.push_back(newResource);
    return newResource;
}

RGResourceHandleMutable RGPassBuilder::CreateTexture(const char* name, const TextureDesc& desc)
{
    return m_RenderGraph.CreateTexture(name, desc);
}

RGResourceHandleMutable RGPassBuilder::CreateBuffer(const char* name, const BufferDesc& desc)
{
    return m_RenderGraph.CreateBuffer(name, desc);
}

void RGPassBuilder::NeverCull()
{
    m_Pass.m_NeverCull = true;
}

RGGraph::RGGraph(RGResourceAllocator* pAllocator)
    : m_pAllocator(pAllocator)
{
}

RGGraph::~RGGraph()
{
    DestroyData();
}

void RGGraph::DestroyData()
{
    for (RGPassBase* pPass : m_RenderPasses)
    {
        delete pPass;
    }
    m_RenderPasses.clear();
    for (RGResource* pResource : m_Resources)
    {
        delete pResource;
    }
    m_Resources.clear();
    m_RGNodes.clear();
    m_Aliases.clear();
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

        for (RGPassBase* pPass : m_RenderPasses)
        {
            // make all renderpasses that read from "From" also read from "To"
            for (RGResourceHandle& handle : pPass->m_Reads)
            {
                if (pPass->ReadsFrom(alias.From) )
                {
                    if (pPass->ReadsFrom(alias.To) == false)
                    {
                        pPass->m_Reads.push_back(alias.To);
                    }
                    break;
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
    for (RGPassBase* pPass : m_RenderPasses)
    {
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

        RGPassBase* pWriter = pNode->m_pWriter;
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

int64_t RGGraph::Execute(Graphics* pGraphics)
{
    CommandContext* pContext = pGraphics->AllocateCommandContext(D3D12_COMMAND_LIST_TYPE_DIRECT);

    for (RGPassBase* pPass : m_RenderPasses)
    {
        if (pPass->m_References > 0)
        {
            ExecutePass(pPass, *pContext, m_pAllocator);
        }
    }

    DestroyData();
    return pContext->Execute(false);
}

void RGGraph::ExecutePass(RGPassBase* pPass, CommandContext& renderContext, RGResourceAllocator* pAllocator)
{
    PrepareResources(pPass,pAllocator);

    RGPassResource resources(*this, *pPass);

    // automatically insert resource barrier
    // check if we're in a graphics pass and automatically call BeginRenderPass

    GPU_PROFILE_BEGIN(pPass->m_Name, &renderContext);
    pPass->Execute(resources, renderContext);
    GPU_PROFILE_END(&renderContext);

    // check if we're in a graphics pass and automatically call EndRenderPass

    ReleaseResources(pPass, m_pAllocator);
}

void RGGraph::Present(RGResourceHandle resource)
{
    RG_ASSERT(IsValidHandle(resource), "resource is invalid");
    struct Empty{};
    AddCallbackPass<Empty>("Present",
            [&](RGPassBuilder& builder, Empty&) {
                builder.Read(resource);
                builder.NeverCull();
            },
            [=](CommandContext&, const RGPassResource&, const Empty&){

            });
}

RGResourceHandle RGGraph::MoveResource(RGResourceHandle from, RGResourceHandle to)
{
    RG_ASSERT(IsValidHandle(to), "resource is invalid");
    const RGNode& node = GetResourceNode(from);
    m_Aliases.push_back(ResourceAlias{from, to});
    ++node.m_pResource->m_Version;
    return CreateResourceNode(node.m_pResource);
}

RGResource* RGPassResource::GetResourceInternal(RGResourceHandle handle) const
{
	const RGNode& node = m_Graph.GetResourceNode(handle);
	return node.m_pResource;
}

void RGGraph::PrepareResources(RGPassBase* pPass, RGResourceAllocator* pAllocator)
{
    for (RGResourceHandle handle : pPass->m_Writes)
    {
        ConditionallyCreateResource(GetResource(handle), pAllocator);
    }
}

void RGGraph::ReleaseResources(RGPassBase* pPass, RGResourceAllocator* pAllocator)
{
    for (RGResourceHandle handle : pPass->m_Reads)
    {
        ConditionallyDestroyResource(GetResource(handle), pAllocator);
    }
}

void RGGraph::ConditionallyCreateResource(RGResource* pResource, RGResourceAllocator* pAllocator)
{
    if (pResource->m_pPhysicalResource == nullptr)
    {
        switch (pResource->m_Type)
        {
        case RGResourceType::Texture:
            pResource->m_pPhysicalResource = pAllocator->CreateTexture(static_cast<RGTexture*>(pResource)->GetDesc());
            break;
        case RGResourceType::Buffer:
            // pResource->m_pPhysicalResource = pAllocator->CreateBuffer(static_cast<BufferResource*>(pResource)->GetDesc());
            break;
        default:
            RG_ASSERT(false, "Invalid resource type");
        }
    }
}

void RGGraph::ConditionallyDestroyResource(RGResource* pResource, RGResourceAllocator* pAllocator)
{
    if (pResource->m_pPhysicalResource != nullptr)
    {
        switch (pResource->m_Type)
        {
        case RGResourceType::Texture:
            pAllocator->ReleaseTexture(static_cast<GraphicsTexture*>(pResource->m_pPhysicalResource));
            break;
        case RGResourceType::Buffer:
            // pAllocator->ReleaseBuffer(static_cast<GraphicsBuffer*>(pResource->m_pPhysicalResource));
            break;
        default:
            RG_ASSERT(false, "Invalid resource type");
        }
    }
}
