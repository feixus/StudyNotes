#include "stdafx.h"
#include "ShaderBindingTable.h"
#include "CommandContext.h"
#include "StateObject.h"

uint32_t ComputeRecordSize(uint32_t elements)
{
	return Math::AlignUp<uint32_t>(D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES + elements * sizeof(uint64_t), D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT);
}

ShaderBindingTable::ShaderBindingTable(StateObject* pStateObject) : m_pStateObject(pStateObject)
{}

void ShaderBindingTable::BindRayGenShader(const char* pName, const std::vector<uint64_t>& data)
{
	uint32_t dataSize = (uint32_t)data.size() * sizeof(uint64_t);
    m_RayGenRecord = CreateRecord(pName, data.data(), dataSize);
    m_RayGenRecordSize = ComputeRecordSize(dataSize);
}

void ShaderBindingTable::BindMissShader(const char* pName, uint32_t rayIndex, const std::vector<uint64_t>& data)
{
    if (rayIndex >= (uint32_t)m_MissShaderRecords.size())
    {
        m_MissShaderRecords.resize(rayIndex + 1);
    }

	uint32_t dataSize = (uint32_t)data.size() * sizeof(uint64_t);
    m_MissShaderRecords[rayIndex] = CreateRecord(pName, data.data(), dataSize);
    m_MissRecordSize = Math::Max<int>(m_MissRecordSize, ComputeRecordSize(dataSize));
}

void ShaderBindingTable::BindHitGroup(const char* pName, uint32_t index, const std::vector<uint64_t>& data)
{
	BindHitGroup(pName, index, data.data(), (uint32_t)data.size() * sizeof(uint64_t));
}

void ShaderBindingTable::BindHitGroup(const char* pName, uint32_t index, const void* pData, uint32_t dataSize)
{
    if (index >= m_HitGroupShaderRecords.size())
    {
        m_HitGroupShaderRecords.resize(index + 1);
    }
    m_HitGroupShaderRecords[index] = CreateRecord(pName, pData, dataSize);
    m_HitRecordSize = Math::Max<int>(m_HitRecordSize, ComputeRecordSize(dataSize));
}

void ShaderBindingTable::Commit(CommandContext& context, D3D12_DISPATCH_RAYS_DESC& rayDesc)
{
    uint32_t totalSize = 0;
    uint32_t rayGenSection = m_RayGenRecordSize;
    uint32_t rayGenSectionAligned = Math::AlignUp<uint32_t>(rayGenSection, D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT);
    uint32_t missSection = m_MissRecordSize * (uint32_t)m_MissShaderRecords.size();
    uint32_t missSectionAligned = Math::AlignUp<uint32_t>(missSection, D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT);
    uint32_t hitGroupSection = m_HitRecordSize * (uint32_t)m_HitGroupShaderRecords.size();
    uint32_t hitGroupSectionAligned = Math::AlignUp<uint32_t>(hitGroupSection, D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT);
    
    totalSize = Math::AlignUp<uint32_t>(rayGenSectionAligned + missSectionAligned + hitGroupSectionAligned, 256);
    DynamicAllocation allocation = context.AllocateTransientMemory(totalSize);
    allocation.Clear();
    
    uint8_t* pStart = (uint8_t*)allocation.pMappedMemory;
    uint8_t* pData = pStart;
    
    // ray gen
    {
        memcpy(pData, m_RayGenRecord.pIdentifier, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
        memcpy(pData + D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES, m_RayGenRecord.pData.get(), m_RayGenRecord.Size);
        pData += m_RayGenRecordSize;
    }
    pData = pStart + rayGenSectionAligned;

    // miss
    for (const ShaderRecord& e : m_MissShaderRecords)
    {
        memcpy(pData, e.pIdentifier, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
        memcpy(pData + D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES, e.pData.get(), e.Size);
        pData += m_MissRecordSize;
    }
    pData = pStart + rayGenSectionAligned + missSectionAligned;

    // hit
    for (const ShaderRecord& e : m_HitGroupShaderRecords)
    {
        memcpy(pData, e.pIdentifier, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
        memcpy(pData + D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES, e.pData.get(), e.Size);
        pData += m_HitRecordSize;
    }

    rayDesc.RayGenerationShaderRecord.StartAddress = allocation.GpuHandle;
    rayDesc.RayGenerationShaderRecord.SizeInBytes = rayGenSection;
    rayDesc.MissShaderTable.StartAddress = allocation.GpuHandle + rayGenSectionAligned;
    rayDesc.MissShaderTable.SizeInBytes = missSection;
    rayDesc.MissShaderTable.StrideInBytes = m_MissRecordSize;
    rayDesc.HitGroupTable.StartAddress = allocation.GpuHandle + rayGenSectionAligned + missSectionAligned;
    rayDesc.HitGroupTable.SizeInBytes = hitGroupSection;
    rayDesc.HitGroupTable.StrideInBytes = m_HitRecordSize;

    m_RayGenRecordSize = 0;
    m_MissShaderRecords.clear();
    m_MissRecordSize = 0;
    m_HitGroupShaderRecords.clear();
    m_HitRecordSize = 0;
}

ShaderBindingTable::ShaderRecord ShaderBindingTable::CreateRecord(const char* pName, const void* pData, uint32_t dataSize)
{
    ShaderRecord entry;
    if (pName)
    {
        auto it = m_IdentifierMap.find(pName);
        if (it == m_IdentifierMap.end())
        {
            m_IdentifierMap[pName] = m_pStateObject->GetStateObjectProperties()->GetShaderIdentifier(MULTIBYTE_TO_UNICODE(pName));
        }

        entry.pIdentifier = m_IdentifierMap[pName];
        check(entry.pIdentifier);

        entry.pData = std::make_unique<char[]>(dataSize);
		entry.Size = dataSize;
		memcpy(entry.pData.get(), pData, dataSize);
    }
    else
    {
        constexpr const void* NullEntry = (void*)"";
        entry.pIdentifier = NullEntry;
    }
    return entry;
}
