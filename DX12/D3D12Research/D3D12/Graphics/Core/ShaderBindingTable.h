#pragma once

class StateObject;
class CommandContext;

class ShaderBindingTable
{
private:
    struct ShaderRecord
    {
        std::vector<uint64_t> data;
        const void* pIdentifier{nullptr};
    };

public:
    ShaderBindingTable(StateObject* pStateObject);
    void BindRayGenShader(const char* pName, const std::vector<uint64_t>& data = {});
    void BindMissShader(const char* pName, uint32_t rayIndex, const std::vector<uint64_t>& data = {});
    void BindHitGroup(const char* pName, uint32_t index, const std::vector<uint64_t>& data = {});
    void Commit(CommandContext& context, D3D12_DISPATCH_RAYS_DESC& rayDesc);
    
private:
    ShaderRecord CreateRecord(const char* pName, const std::vector<uint64_t>& data);
    uint32_t ComputeRecordSize(uint32_t elements);
    
    StateObject* m_pStateObject;
    ShaderRecord m_RayGenRecord;
    uint32_t m_RayGenRecordSize{0};
    std::vector<ShaderRecord> m_MissShaderRecords;
    uint32_t m_MissRecordSize{0};
    std::vector<ShaderRecord> m_HitGroupShaderRecords;
    uint32_t m_HitRecordSize{0};
    std::unordered_map<std::string, const void*> m_IdentifierMap;
};
