#include "stdafx.h"
#include "RenderGraph.h"
#include <fstream>
#include "../Graphics.h"
#include "../CommandContext.h"
#include "../Profiler.h"

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

RG::ResourceHandleMutable RG::RenderPassBuilder::CreateTexture(const std::string& name, const Texture::Descriptor& desc)
{
    return m_RenderGraph.CreateResource<Texture>(name, desc);
}

RG::ResourceHandleMutable RG::RenderPassBuilder::CreateBuffer(const std::string& name, const Buffer::Descriptor& desc)
{
    return m_RenderGraph.CreateResource<Buffer>(name, desc);
}

void RG::RenderPassBuilder::NeverCull()
{
    m_Pass.m_NeverCull = true;
}

RG::ResourceBase* RG::RenderPassResources::GetResourceInternal(ResourceHandle handle) const
{
    const ResourceNode& node = m_RenderGraph.GetResourceNode(handle);
    return node.m_pResource;
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
    for (ResourceBase* pResource : m_Resources)
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
            ResourceBase* pResource = GetResource(read);
            if (pResource->pFirstPassUsage == nullptr)
            {
                pResource->pFirstPassUsage = pPass;
            }
            pResource->pLastPassUsage = pPass;
        }

        for (ResourceHandle write : pPass->m_Writes)
        {
            ResourceBase* pResource = GetResource(write);
            if (pResource->pFirstPassUsage == nullptr)
            {
                pResource->pFirstPassUsage = pPass;
            }
            pResource->pLastPassUsage = pPass;
        }
    }

    for (ResourceBase* pResource : m_Resources)
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

void RG::RenderGraph::Execute(Graphics* pGraphics)
{
    CommandContext* pContext = pGraphics->AllocateCommandContext(D3D12_COMMAND_LIST_TYPE_DIRECT);

    for (RenderPassBase* pPass : m_RenderPasses)
    {
        if (pPass->m_References > 0)
        {
            for (ResourceBase* pResource : pPass->m_ResourcesToCreate)
            {
                pResource->Create();
            }

            RenderPassResources resources(*this, *pPass);
            Profiler::Instance()->Begin(pPass->m_Name.c_str(), pContext);
            pPass->Execute(resources, *pContext);
            Profiler::Instance()->End(pContext);

            for (ResourceBase* pResource : pPass->m_ResourcesToDestroy)
            {
                pResource->Destroy();
            }
                
        }
    }

    pContext->Execute(true);
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

void RG::RenderGraph::DumpGraphViz(const char* pPath) const
{
    std::ofstream stream(pPath);

    stream << "digraph RenderGraph {\n";
    stream << "rankdir = LR\n";

    // pass declaration
    int passIndex = 0;
    for (const RenderPassBase* pPass : m_RenderPasses)
    {
        stream << "Pass" << pPass->m_Id << " [";
        stream << "shape=rectangle, style=\"filled, rounded\", margin=0.2, ";
        if (pPass->m_References == 0)
        {
            stream << "fillcolor = mistyrose";
        }
        else
        {
            stream << "fillcolor = orange";
        }

        stream << ", label = \"";
        stream << pPass->m_Name << "\n";
        stream << "Refs: " << pPass->m_References << "\n";
        stream << "Index: " << passIndex;
        stream << "\"]\n";
        if (pPass->m_References)
        {
            ++passIndex;
        }
    }

    // resource declaration
    for (const ResourceNode& node : m_ResourceNodes)
    {
        stream << "Resource" << node.m_pResource->m_Id << "_" << node.m_Version << " [";
        stream << "shape=rectangle, style=filled, ";
        if (node.m_pResource->m_References == 0)
        {
            stream << "fillcolor = azure2";
        }
        else if (node.m_pResource->m_IsImported)
        {
            stream << "fillcolor = lightskyblue3";
        }
        else
        {
            stream << "fillcolor = lightskyblue1";
        }

        stream << ", label = \"";
        stream << node.m_pResource->m_Name << "\n";
        stream << "Id:" << node.m_pResource->m_Id << "\n";
        stream << "Refs:" << node.m_pResource->m_References;
        stream << "\"]\n";
    }

    // writes
    for (RenderPassBase* pPass : m_RenderPasses)
    {
        for (ResourceHandle write : pPass->m_Writes)
        {
            const ResourceNode& node = GetResourceNode(write);
            stream << "Pass" << pPass->m_Id << " -> Resource" << node.m_pResource->m_Id << "_" << node.m_Version << "[color=chocolate1]\n";
        }
    }
    stream << "\n";

    // reads
    for (const ResourceNode& node : m_ResourceNodes)
    {
        stream << "Resource" << node.m_pResource->m_Id << "_" << node.m_Version << " -> {\n";
        for (RenderPassBase* pPass : m_RenderPasses)
        {
            for (ResourceHandle read : pPass->m_Reads)
            {
                const ResourceNode& readNode = GetResourceNode(read);
                if (node.m_Version == readNode.m_Version && node.m_pResource->m_Id == readNode.m_pResource->m_Id)
                {
                    stream << "Pass" << pPass->m_Id << "\n";
                }
            }
        }
        stream << "} " << "[color=darkseagreen]";
    }
    stream << "\n";

    // aliases
    for (const ResourceAlias& alias : m_Aliases)
    {
        stream << "Resource" << GetResourceNode(alias.From).m_pResource->m_Id << "_" << GetResourceNode(alias.From).m_Version << " -> ";
        stream << "Resource" << GetResourceNode(alias.To).m_pResource->m_Id << "_" << GetResourceNode(alias.To).m_Version << "[color=darkorchid3]\n";
    }

    stream << "}";
}

RG::ResourceHandle RG::RenderGraph::MoveResource(ResourceHandle from, ResourceHandle to)
{
    RG_ASSERT(IsValidHandle(to), "resource is invalid");
    const ResourceNode& node = GetResourceNode(from);
    m_Aliases.push_back(ResourceAlias{from, to});
    ++node.m_pResource->m_Version;
    return CreateResourceNode(node.m_pResource);
}

