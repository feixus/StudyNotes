#pragma once

class Shader;
class ShaderLibrary;

enum class BlendMode : uint8_t
{
	Replace = 0,
	Add,
	Multiply,
	Alpha,
	AddAlpha,
	PreMultiplyAlpha,
	InverseDestinationAlpha,
	Subtract,
	SubtractAlpha,
	Undefined,
};

enum class PipelineStateType
{
	Graphics,
	Compute,
	Mesh,
	MAX
};

class StateObjectDesc
{
	class PODLinearAllocator
	{
	public:
		PODLinearAllocator(uint32_t size)
			: m_Size(size), m_pData(new uint8_t[m_Size]), m_pCurrentData(m_pData)
		{
			memset(m_pData, 0, m_Size);
		}

		~PODLinearAllocator()
		{
			delete[] m_pData;
			m_pData = nullptr;
		}

		template<typename T>
		T* Allocate(int count = 1)
		{
			return (T*)Allocate(sizeof(T) * count);
		}

		uint8_t* Allocate(uint32_t size)
		{
			check(size > 0);
			checkf(m_pCurrentData - m_pData - size <= m_Size, "make allocator size larger");
			uint8_t* pData = m_pCurrentData;
			m_pCurrentData += size;
			return pData;
		}

		const uint8_t* Data() const
		{
			return m_pData;
		}

	private:
		uint32_t m_Size;
		uint8_t* m_pData;
		uint8_t* m_pCurrentData;
	};

public:
	StateObjectDesc(D3D12_STATE_OBJECT_TYPE type = D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE);

	uint32_t AddLibrary(const ShaderLibrary& shader, const std::vector<std::string>& exports = {});
	uint32_t AddHitGroup(const char* pHitGroupExport, const char* pClosestHigShaderImport = nullptr, 
						 const char* pAnyHitShaderImport = nullptr, const char* pIntersectionShaderImport = nullptr);
	uint32_t AddStateAssociation(uint32_t index, const std::vector<std::string>& exports);
	uint32_t AddCollection(ID3D12StateObject* pStateObject, const std::vector<std::string>& exports = {});
	uint32_t BindLocalRootSignature(const char* pExportName, ID3D12RootSignature* pRootSignature);
	uint32_t SetRaytracingShaderConfig(uint32_t maxPayloadSize, uint32_t maxAttributeSize);
	uint32_t SetRaytracingPipelineConfig(uint32_t maxRecursionDepth);
	uint32_t SetGlobalRootSignature(ID3D12RootSignature* pRootSignature);
	ComPtr<ID3D12StateObject> Finalize(const char* pName, ID3D12Device5* pDevice) const;

private:
	uint32_t AddStateObject(void* pDesc, D3D12_STATE_SUBOBJECT_TYPE type);
	const D3D12_STATE_SUBOBJECT* GetSubobject(uint32_t index) const
	{
		checkf(index < m_SubObjects, "index out of bounds");
		const D3D12_STATE_SUBOBJECT* pData = (D3D12_STATE_SUBOBJECT*)m_StateObjectAllocator.Data();
		return &pData[index];
	}

	PODLinearAllocator m_StateObjectAllocator;
	PODLinearAllocator m_ScratchAllocator;
	uint32_t m_SubObjects = 0;
	D3D12_STATE_OBJECT_TYPE m_Type;
};

class PipelineState
{
public:
	PipelineState();
	PipelineState(const PipelineState& other);
	ID3D12PipelineState* GetPipelineState() const { return m_pPipelineState.Get(); }

	void Finalize(const char* pName, ID3D12Device* pDevice);

	void SetRenderTargetFormat(DXGI_FORMAT rtvFormat, DXGI_FORMAT dsvFormat, uint32_t msaa);
	void SetRenderTargetFormats(DXGI_FORMAT* rtvFormats, uint32_t count, DXGI_FORMAT dsvFormat, uint32_t msaa);

	// blend state
	void SetBlendMode(const BlendMode& blendMode, bool alphaToCoverage);

	// depth stencil state
	void SetDepthEnable(bool enabled);
	void SetDepthWrite(bool enabled);
	void SetDepthTest(const D3D12_COMPARISON_FUNC func);
	void SetStencilTest(bool stencilEnabled,
			D3D12_COMPARISON_FUNC mode, D3D12_STENCIL_OP pass, D3D12_STENCIL_OP fail, D3D12_STENCIL_OP zFail,
			uint32_t stencilRef, uint8_t compareMask, uint8_t writeMask);

	// rasterizer state
	void SetFillMode(D3D12_FILL_MODE fillMode);
	void SetCullMode(D3D12_CULL_MODE cullMode);
	void SetLineAntialias(bool lineAntialias);
	void SetDepthBias(int depthBias, float depthBiasClamp, float slopeScaledDepthBias);

	void SetInputLayout(D3D12_INPUT_ELEMENT_DESC* pElements, uint32_t count);
	void SetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY_TYPE topology);

	void SetRootSignature(ID3D12RootSignature* pRootSignature);

	void SetVertexShader(const Shader& shader);
	void SetPixelShader(const Shader& shader);
	void SetHullShader(const Shader& shader);
	void SetDomainShader(const Shader& shader);
	void SetGeometryShader(const Shader& shader);
	void SetComputeShader(const Shader& shader);
	void SetMeshShader(const Shader& shader);
	void SetAmplificationShader(const Shader& shader);

	PipelineStateType GetType() const { return m_Type; }

protected:
	ComPtr<ID3D12PipelineState> m_pPipelineState;

	struct PipelineDesc
	{
		CD3DX12_PIPELINE_STATE_STREAM1 PS{};
		CD3DX12_PIPELINE_STATE_STREAM_AS AS{};
		CD3DX12_PIPELINE_STATE_STREAM_MS MS{};
	};
	PipelineDesc m_Desc;

	PipelineStateType m_Type{PipelineStateType::MAX};
};