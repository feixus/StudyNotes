#pragma once
#include "GraphicsResource.h"

class RootSignature;
class ShaderLibrary;
class StateObject;
class ShaderManager;

class StateObjectInitializer
{
public:
	friend class StateObject;

	void AddHitGroup(const std::string& name, const std::string& closestHit = "",
                         const std::string& anyHit = "", const std::string& intersection = "", RootSignature* pRootSignature = nullptr);
	void AddLibrary(ShaderLibrary* pLibrary, const std::vector<std::string>& exports = {});
    void AddCollection(StateObject* pOtherObject);
    void AddMissShader(const std::string& name, RootSignature* pRootSignature = nullptr);

    void CreateStateObjectStream(class StateObjectStream& stateObjectStream);
	void SetMaxPipelineStackSize(StateObject* pStateObject);

    std::string Name;
    uint32_t MaxRecursion{1};
    uint32_t MaxPayloadSize{0};
    uint32_t MaxAttributeSize{0};
    std::string RayGenShader;
    RootSignature* pGlobalRootSignature{ nullptr };
    D3D12_STATE_OBJECT_TYPE Type{ D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE };
    D3D12_RAYTRACING_PIPELINE_FLAGS Flags{ D3D12_RAYTRACING_PIPELINE_FLAG_NONE };
    
    private:
    struct HitGroupDefinition
    {
        HitGroupDefinition() = default;

        std::string Name;
        std::string ClosestHit;
        std::string AnyHit;
        std::string Intersection;
        RootSignature* pLocalRootSignature{nullptr};
    };

    struct LibraryShaderExport
    {
        std::string Name;
        RootSignature* pLocalRootSignature{nullptr};
    };

    struct LibraryExports
    {
        ShaderLibrary* pLibrary{nullptr};
        std::vector<std::string> Exports;
    };

    std::vector<LibraryExports> m_Libraries;
    std::vector<HitGroupDefinition> m_HitGroups;
    std::vector<LibraryShaderExport> m_MissShaders;
    std::vector<StateObject*> m_Collections;
};

class StateObject : public GraphicsObject
{
public:
    StateObject(ShaderManager* pShaderManager, GraphicsDevice* pParent);

	StateObject(const StateObject& rhs) = delete;
	StateObject& operator=(const StateObject& rhs) = delete;

    void Create(const StateObjectInitializer& initializer);
	void ConditionallyReload();

    const StateObjectInitializer& GetDesc() const { return m_Desc; }
    ID3D12StateObject* GetStateObject() const { return m_pStateObject.Get(); }
    ID3D12StateObjectProperties* GetStateObjectProperties() const { return m_pStateObjectProperties.Get(); }

private:
	void OnLibraryReloaded(ShaderLibrary* pOldShaderLibrary, ShaderLibrary* pNewShaderLibrary);

	bool m_NeedsReload{false};
	DelegateHandle m_ReloadHandle;

    StateObjectInitializer m_Desc;
    ComPtr<ID3D12StateObject> m_pStateObject;
    ComPtr<ID3D12StateObjectProperties> m_pStateObjectProperties;
};
