#include "stdafx.h"
#include "RenderGraph.h"

namespace RG
{
	void RenderGraph::DumpGraphMermaid(const char* pPath) const
	{
		std::ofstream stream(pPath);

		stream << "<script src=\"https://unpkg.com/mermaid@11.6.0/dist/mermaid.min.js\"></script>";
		stream << "<script>mermaid.initialize({ startOnLoad:true });</script>";
		stream << "<div class=\"mermaid\">";

		stream << "graph LR;\n";

		stream << "classDef referencedPass fill:#fa0,stroke:#333,stroke-width:2px;\n";
		stream << "classDef unreferenced stroke:#fee,stroke-width:1px;\n";
		stream << "classDef referencedResource fill:#bef,stroke:#333,stroke-width:2px;\n";
		stream << "classDef importedResource fill:#9bd,stroke:#333,stroke-width:2px;\n";

		const char* writeLinkStyle = "stroke:#f82,stroke-width:2px;";
		const char* readLinkStyle = "stroke:#9c9,stroke-width:2px;";
		const char* aliasLinkStyle = "stroke:#e1c,stroke-width:3px;";
		int linkIndex = 0;

		int passIndex = 0;
		for (const RenderPassBase* pPass : m_RenderPasses)
		{
			stream << "Pass" << pPass->m_Id << "[";
			stream << pPass->m_Name << "<br/>";
			stream << "Refs: " << pPass->m_References << "<br/>";
			stream << "Index: " << passIndex;
			stream << "]:::";

			if (pPass->m_References == 0)
			{
				stream << "unreferenced";
			}
			else
			{
				stream << "referencedPass";
			}
			if (pPass->m_References)
			{
				++passIndex;
			}

			stream << "\n";
		}

		for (const ResourceNode& node : m_ResourceNodes)
		{
			stream << "Resource" << node.m_pResource->m_Id << "_" << node.m_Version << "([";
			stream << node.m_pResource->m_Name << "<br/>";
			stream << "Id: " << node.m_pResource->m_Id << "<br/>";
			stream << "Refs: " << node.m_pResource->m_References << "]):::";

			if (node.m_pResource->m_References == 0)
			{
				stream << "unreferenced";
			}
			else if (node.m_pResource->m_IsImported)
			{
				stream << "importedResource";
			}
			else
			{
				stream << "referencedResource";
			}

			stream << "\n";
		}

		for (RenderPassBase* pPass : m_RenderPasses)
		{
			for (ResourceHandle write : pPass->m_Writes)
			{
				const ResourceNode& node = GetResourceNode(write);
				stream << "Pass" << pPass->m_Id << " --> Resource" << node.m_pResource->m_Id << "_" << node.m_Version << "\n";
				stream << "linkStyle " << linkIndex++ << " " << writeLinkStyle << "\n";
			}
		}
		stream << "\n";

		for (const ResourceNode& node : m_ResourceNodes)
		{
			for (RenderPassBase* pPass : m_RenderPasses)
			{
				for (ResourceHandle read : pPass->m_Reads)
				{
					const ResourceNode& readNode = GetResourceNode(read);
					if (node.m_Version == readNode.m_Version && node.m_pResource->m_Id == readNode.m_pResource->m_Id)
					{
						stream << "Resource" << node.m_pResource->m_Id << "_" << node.m_Version << " --> Pass" << pPass->m_Id << "\n";
						stream << "linkStyle " << linkIndex++ << " " << readLinkStyle << "\n";
					}
				}
			}
		}
		stream << "\n";

		for (const ResourceAlias& alias : m_Aliases)
		{
			const ResourceNode& fromNode = GetResourceNode(alias.From);
			const ResourceNode& toNode = GetResourceNode(alias.To);
			stream << "Resource" << fromNode.m_pResource->m_Id << "_" << fromNode.m_Version << " --> Resource" << toNode.m_pResource->m_Id << "_" << toNode.m_Version << "\n";
			stream << "linkStyle " << linkIndex++ << " " << aliasLinkStyle << "\n";
		}

		stream << "</div>";
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
}