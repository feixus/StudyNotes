#include "stdafx.h"
#include "RenderGraph.h"
#include "Graphics/Graphics.h"
#include "Graphics/CommandContext.h"
#include "Graphics/Profiler.h"

RG::ResourceHandle RG::RenderPassBuilder::Read(const ResourceHandle& resource)
{
	m_RenderGraph.GetResourceNode(resource);
#if RG_DEBUG
	RG_ASSERT(m_Pass.ReadsFrom(resource) == false, "Pass already reads from this resource");
#endif
	m_Pass.m_Reads.push_back(resource);
	return resource;
}

RG::ResourceHandleMutable& RG::RenderPassBuilder::Write(ResourceHandleMutable& resource)
{
#if RG_DEBUG
    RG_ASSERT(m_Pass.WritesTo(resource) == false, "Pass already writes to this resource");
#endif
    const ResourceNode& node = m_RenderGraph.GetResourceNode(resource);
    RG_ASSERT(node.m_pResource->m_Version == node.m_Version, "Version mismatch");
    ++node.m_pResource->m_Version;
    if (node.m_pResource->m_IsImported)
    {
        m_Pass.m_NeverCull = true;
    }
    resource.Invalidate();
    ResourceHandleMutable newResource = m_RenderGraph.CreateResourceNode(node.m_pResource);
    m_Pass.m_Writes.push_back(newResource);
    return newResource;
}

RG::ResourceHandleMutable RG::RenderPassBuilder::CreateTexture(const char* name, const TextureDesc& desc)
{
    return m_RenderGraph.CreateTexture(name, desc);
}

RG::ResourceHandleMutable RG::RenderPassBuilder::CreateBuffer(const char* name, const BufferDesc& desc)
{
    return m_RenderGraph.CreateBuffer(name, desc);
}

void RG::RenderPassBuilder::NeverCull()
{
    m_Pass.m_NeverCull = true;
}

RG::RenderGraph::RenderGraph()
{
}

RG::RenderGraph::~RenderGraph()
{
    for (RenderPassBase* pPass : m_RenderPasses)
    {
        delete pPass;
    }
    for (VirtualResourceBase* pResource : m_Resources)
    {
        delete pResource;
    }
}

void RG::RenderGraph::Compile()
{
    // process all the resource aliases
    for (ResourceAlias& alias : m_Aliases)
    {
        const ResourceNode& fromNode = GetResourceNode(alias.From);
        const ResourceNode& toNode = GetResourceNode(alias.To);

        // reroute all "to" resource to be the "from" resource
        for (ResourceNode& node : m_ResourceNodes)
        {
            if (node.m_pResource == toNode.m_pResource)
            {
                node.m_pResource = fromNode.m_pResource;
            }
        }

        for (RenderPassBase* pPass : m_RenderPasses)
        {
            // make all renderpasses that read from "From" also read from "To"
            for (ResourceHandle& handle : pPass->m_Reads)
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
    for (RenderPassBase* pPass : m_RenderPasses)
    {
        pPass->m_References = (int)pPass->m_Writes.size() + (int)pPass->m_NeverCull;

        for (ResourceHandle read : pPass->m_Reads)
        {
            ResourceNode& node = m_ResourceNodes[read.Index];
            node.m_Reads++;
        }

        for (ResourceHandle write : pPass->m_Writes)
        {
            ResourceNode& node = m_ResourceNodes[write.Index];
            node.m_pWriter = pPass;
            node.m_Reads++;
        }
    }

    // do the culling
    std::queue<ResourceNode*> stack;
    for (ResourceNode& node : m_ResourceNodes)
    {
        if (node.m_Reads == 0)
        {
            stack.push(&node);
        }
    }

    while (!stack.empty())
    {
        const ResourceNode* pNode = stack.front();
        stack.pop();

        RenderPassBase* pWriter = pNode->m_pWriter;
        if (pWriter)
        {
            RG_ASSERT(pWriter->m_References >= 1, "Pass is expected to have references");
            --pWriter->m_References;
            if (pWriter->m_References == 0)
            {
                std::vector<ResourceHandle>& reads = pWriter->m_Reads;
                for (ResourceHandle& resource : reads)
                {
                    ResourceNode& node = m_ResourceNodes[resource.Index];   
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
    for (ResourceNode& node : m_ResourceNodes)
    {
        node.m_pResource->m_References += node.m_Reads;
    }

    // find when to create and destroy resources
    for (RenderPassBase* pPass : m_RenderPasses)
    {
        if (pPass->m_References == 0)
        {
            RG_ASSERT(pPass->m_NeverCull == false, "A pass that should never cull should have references");
            continue;
        }

        for (ResourceHandle read : pPass->m_Reads)
        {
            VirtualResourceBase* pResource = GetResource(read);
            if (pResource->pFirstPassUsage == nullptr)
            {
                pResource->pFirstPassUsage = pPass;
            }
            pResource->pLastPassUsage = pPass;
        }

        for (ResourceHandle write : pPass->m_Writes)
        {
            VirtualResourceBase* pResource = GetResource(write);
            if (pResource->pFirstPassUsage == nullptr)
            {
                pResource->pFirstPassUsage = pPass;
            }
            pResource->pLastPassUsage = pPass;
        }
    }

    for (VirtualResourceBase* pResource : m_Resources)
    {
        if (pResource->m_References > 0)
        {
            RG_ASSERT(!pResource->pFirstPassUsage == !pResource->pLastPassUsage, "a resource's usage should have been calculated in a pair(begin/end usage)");
            if (pResource->pFirstPassUsage && pResource->pLastPassUsage)
            {
                pResource->pFirstPassUsage->m_ResourcesToCreate.push_back(pResource);
                pResource->pLastPassUsage->m_ResourcesToDestroy.push_back(pResource);
            }
        }
    }
}

int64_t RG::RenderGraph::Execute(Graphics* pGraphics)
{
    CommandContext* pContext = pGraphics->AllocateCommandContext(D3D12_COMMAND_LIST_TYPE_DIRECT);

    for (RenderPassBase* pPass : m_RenderPasses)
    {
        if (pPass->m_References > 0)
        {
            for (VirtualResourceBase* pResource : pPass->m_ResourcesToCreate)
            {
                pResource->Create(*this);
            }

            RenderPassResources resources(*this, *pPass);
            Profiler::Instance()->Begin(pPass->m_Name, pContext);
            pPass->Execute(resources, *pContext);
            Profiler::Instance()->End(pContext);

            for (VirtualResourceBase* pResource : pPass->m_ResourcesToDestroy)
            {
                pResource->Destroy(*this);
            }
                
        }
    }

    return pContext->Execute(false);
}

void RG::RenderGraph::Present(ResourceHandle resource)
{
    RG_ASSERT(IsValidHandle(resource), "resource is invalid");
    struct Empty{};
    AddCallbackPass<Empty>("Present",
            [&](RenderPassBuilder& builder, Empty&) {
                builder.Read(resource);
                builder.NeverCull();
            },
            [=](CommandContext&, const RenderPassResources&, const Empty&){

            });
}

RG::ResourceHandle RG::RenderGraph::MoveResource(ResourceHandle from, ResourceHandle to)
{
    RG_ASSERT(IsValidHandle(to), "resource is invalid");
    const ResourceNode& node = GetResourceNode(from);
    m_Aliases.push_back(ResourceAlias{from, to});
    ++node.m_pResource->m_Version;
    return CreateResourceNode(node.m_pResource);
}

RG::VirtualResourceBase* RG::RenderPassResources::GetResourceInternal(ResourceHandle handle) const
{
	const ResourceNode& node = m_Graph.GetResourceNode(handle);
	return node.m_pResource;
}
