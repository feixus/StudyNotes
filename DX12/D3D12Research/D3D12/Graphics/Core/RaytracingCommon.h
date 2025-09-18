#pragma once

class ShaderBindingTable
{
private:
    struct TableEntry
    {
        std::vector<uint64_t> data;
        void* pIdentifier{nullptr};
    };

public:
    ShaderBindingTable(ID3D12StateObject* pStateObject)
    {
        VERIFY_HR(pStateObject->QueryInterface(IID_PPV_ARGS(m_pObjectProperties.GetAddressOf())));
    }

    void AddRayGenEntry(const char* pName, const std::vector<uint64_t>& data)
    {
        m_RayGenTable.push_back(CreateEntry(pName, data));
        uint32_t entrySize = Math::AlignUp<uint32_t>(D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES + (uint32_t)data.size() * sizeof(uint64_t), D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT);
        m_RayGenEntrySize = Math::Max(m_RayGenEntrySize, entrySize);
    }

    void AddMissEntry(const char* pName, const std::vector<uint64_t>& data)
    {
        m_MissTable.push_back(CreateEntry(pName, data));
        uint32_t entrySize = Math::AlignUp<uint32_t>(D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES + (uint32_t)data.size() * sizeof(uint64_t), D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT);
        m_MissEntrySize = Math::Max(m_MissEntrySize, entrySize);
    }

    void AddHitGroupEntry(const char* pName, const std::vector<uint64_t>& data)
    {
        m_HitTable.push_back(CreateEntry(pName, data));
        uint32_t entrySize = Math::AlignUp<uint32_t>(D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES + (uint32_t)data.size() * sizeof(uint64_t), D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT);
        m_HitEntrySize = Math::Max(m_HitEntrySize, entrySize);
    }

    void Commit(CommandContext& context, D3D12_DISPATCH_RAYS_DESC& rayDesc)
    {
        uint32_t totalSize = 0;
		uint32_t rayGenSection = m_RayGenEntrySize * (uint32_t)m_RayGenTable.size();
		uint32_t rayGenSectionAligned = Math::AlignUp<uint32_t>(rayGenSection, D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT);
		uint32_t missSection = m_MissEntrySize * (uint32_t)m_MissTable.size();
		uint32_t missSectionAligned = Math::AlignUp<uint32_t>(missSection, D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT);
		uint32_t hitGroupSection = m_HitEntrySize * (uint32_t)m_HitTable.size();
		uint32_t hitGroupSectionAligned = Math::AlignUp<uint32_t>(hitGroupSection, D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT);
        
        totalSize = Math::AlignUp<uint32_t>(rayGenSectionAligned + missSectionAligned + hitGroupSectionAligned, 256);
        DynamicAllocation allocation = context.AllocateTransientMemory(totalSize);
        
        uint8_t* pStart = (uint8_t*)allocation.pMappedMemory;
        uint8_t* pData = pStart;
        for (const TableEntry& e : m_RayGenTable)
        {
            memcpy(pData, e.pIdentifier, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
            memcpy(pData + D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES, e.data.data(), e.data.size() * sizeof(uint64_t));
            pData += m_RayGenEntrySize;
        }
        pData = pStart + rayGenSectionAligned;
        for (const TableEntry& e : m_MissTable)
        {
			memcpy(pData, e.pIdentifier, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
			memcpy(pData + D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES, e.data.data(), e.data.size() * sizeof(uint64_t));
			pData += m_MissEntrySize;
        }
        pData = pStart + rayGenSectionAligned + missSectionAligned;
		for (const TableEntry& e : m_HitTable)
		{
			memcpy(pData, e.pIdentifier, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
			memcpy(pData + D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES, e.data.data(), e.data.size() * sizeof(uint64_t));
			pData += m_HitEntrySize;
		}

        rayDesc.RayGenerationShaderRecord.StartAddress = allocation.GpuHandle;
		rayDesc.RayGenerationShaderRecord.SizeInBytes = rayGenSection;
		rayDesc.MissShaderTable.StartAddress = allocation.GpuHandle + rayGenSectionAligned;
		rayDesc.MissShaderTable.SizeInBytes = missSection;
		rayDesc.MissShaderTable.StrideInBytes = m_MissEntrySize;
		rayDesc.HitGroupTable.StartAddress = allocation.GpuHandle + rayGenSectionAligned + missSectionAligned;
		rayDesc.HitGroupTable.SizeInBytes = hitGroupSection;
		rayDesc.HitGroupTable.StrideInBytes = m_HitEntrySize;

        m_RayGenTable.clear();
        m_RayGenEntrySize = 0;
        m_MissTable.clear();
        m_MissEntrySize = 0;
        m_HitTable.clear();
        m_HitEntrySize = 0;
    }
    
private:
    TableEntry CreateEntry(const char* pName, const std::vector<uint64_t>& data)
    {
        TableEntry entry;
        auto it = m_IdentifierMap.find(pName);
        if (it == m_IdentifierMap.end())
        {
            wchar_t wName[256];
            ToWidechar(pName, wName, 256);
            m_IdentifierMap[pName] = m_pObjectProperties->GetShaderIdentifier(wName);
        }
        entry.pIdentifier = m_IdentifierMap[pName];
        check(entry.pIdentifier);
        entry.data = data;
        return entry;
    }

    ComPtr<ID3D12StateObjectProperties> m_pObjectProperties;
    std::vector<TableEntry> m_RayGenTable;
    uint32_t m_RayGenEntrySize{0};
    std::vector<TableEntry> m_MissTable;
    uint32_t m_MissEntrySize{0};
    std::vector<TableEntry> m_HitTable;
    uint32_t m_HitEntrySize{0};
    std::unordered_map<std::string, void*> m_IdentifierMap;
};
