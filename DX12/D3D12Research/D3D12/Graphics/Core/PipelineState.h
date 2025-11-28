#pragma once
#include "GraphicsResource.h"
#include "Shader.h"

class ShaderManager;

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

class VertexElementLayout
{
public:
	static const int MAX_INPUT_ELEMENTS = 16;

	VertexElementLayout() = default;
	VertexElementLayout(const VertexElementLayout& rhs);
	VertexElementLayout& operator=(const VertexElementLayout& rhs);

	void AddVertexElement(const char* pSemantic, DXGI_FORMAT format, uint32_t semanticIndex = 0, uint32_t byteOffset = D3D12_APPEND_ALIGNED_ELEMENT, uint32_t inputSlot = 0);
	void AddInstanceElement(const char* pSemantic, DXGI_FORMAT format, uint32_t semanticIndex, uint32_t byteOffset, uint32_t inputSlot, uint32_t stepRate);
	
	const D3D12_INPUT_ELEMENT_DESC* GetElements() const { return m_ElementDesc.data(); }
	uint32_t GetNumElements() const { return m_NumElements; }

private:
	void FixupString();

	std::array<D3D12_INPUT_ELEMENT_DESC, MAX_INPUT_ELEMENTS> m_ElementDesc{};
	std::array<char[64], MAX_INPUT_ELEMENTS> m_SemanticNames{};
	uint32_t m_NumElements{0};
};

class PipelineStateInitializer
{
private:
#define SUBOBJECT_TRAIT(value, type) \
	template <> \
	struct CD3DX12_PIPELINE_STATE_SUBOJECT_TYPE_TRAITS<value> \
	{ \
		using Type = type; \
	};

	template <D3D12_PIPELINE_STATE_SUBOBJECT_TYPE T>
	struct CD3DX12_PIPELINE_STATE_SUBOJECT_TYPE_TRAITS;

	SUBOBJECT_TRAIT(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_FLAGS, D3D12_PIPELINE_STATE_FLAGS)
	SUBOBJECT_TRAIT(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_NODE_MASK, UINT)
	SUBOBJECT_TRAIT(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_ROOT_SIGNATURE, ID3D12RootSignature*)
	SUBOBJECT_TRAIT(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_INPUT_LAYOUT, D3D12_INPUT_LAYOUT_DESC)
	SUBOBJECT_TRAIT(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_IB_STRIP_CUT_VALUE, D3D12_INDEX_BUFFER_STRIP_CUT_VALUE)
	SUBOBJECT_TRAIT(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PRIMITIVE_TOPOLOGY, D3D12_PRIMITIVE_TOPOLOGY_TYPE)
	SUBOBJECT_TRAIT(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VS, D3D12_SHADER_BYTECODE)
	SUBOBJECT_TRAIT(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_GS, D3D12_SHADER_BYTECODE)
	SUBOBJECT_TRAIT(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_HS, D3D12_SHADER_BYTECODE)
	SUBOBJECT_TRAIT(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DS, D3D12_SHADER_BYTECODE)
	SUBOBJECT_TRAIT(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS, D3D12_SHADER_BYTECODE)
	SUBOBJECT_TRAIT(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_CS, D3D12_SHADER_BYTECODE)
	SUBOBJECT_TRAIT(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_AS, D3D12_SHADER_BYTECODE)
	SUBOBJECT_TRAIT(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_MS, D3D12_SHADER_BYTECODE)
	SUBOBJECT_TRAIT(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_STREAM_OUTPUT, D3D12_STREAM_OUTPUT_DESC)
	SUBOBJECT_TRAIT(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_BLEND, CD3DX12_BLEND_DESC)
	SUBOBJECT_TRAIT(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL, CD3DX12_DEPTH_STENCIL_DESC)
	SUBOBJECT_TRAIT(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL1, CD3DX12_DEPTH_STENCIL_DESC1)
	SUBOBJECT_TRAIT(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL_FORMAT, DXGI_FORMAT)
	SUBOBJECT_TRAIT(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER, CD3DX12_RASTERIZER_DESC)
	SUBOBJECT_TRAIT(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RENDER_TARGET_FORMATS, D3D12_RT_FORMAT_ARRAY)
	SUBOBJECT_TRAIT(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_DESC, DXGI_SAMPLE_DESC)
	SUBOBJECT_TRAIT(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_MASK, UINT)
	SUBOBJECT_TRAIT(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_CACHED_PSO, D3D12_CACHED_PIPELINE_STATE)
	SUBOBJECT_TRAIT(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VIEW_INSTANCING, CD3DX12_VIEW_INSTANCING_DESC)
#undef SUBOBJECT_TRAIT

	friend class PipelineState;

public:
	PipelineStateInitializer();

	void SetName(const char* pName);
	void SetDepthOnlyTarget(DXGI_FORMAT dsvFormat, uint32_t msaa);
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

	void SetInputLayout(const VertexElementLayout& layout);
	void SetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY_TYPE topology);

	void SetRootSignature(ID3D12RootSignature* pRootSignature);

	// shaders
	void SetVertexShader(Shader* pShader);
	void SetPixelShader(Shader* pShader);
	void SetHullShader(Shader* pShader);
	void SetDomainShader(Shader* pShader);
	void SetGeometryShader(Shader* pShader);
	void SetComputeShader(Shader* pShader);
	void SetMeshShader(Shader* pShader);
	void SetAmplificationShader(Shader* pShader);

	D3D12_PIPELINE_STATE_STREAM_DESC GetDesc();

private:
	template<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE ObjectType>
	typename CD3DX12_PIPELINE_STATE_SUBOJECT_TYPE_TRAITS<ObjectType>::Type& GetSubobject()
	{
		using InnerType = typename CD3DX12_PIPELINE_STATE_SUBOJECT_TYPE_TRAITS<ObjectType>::Type;
		struct SubobjectType
		{
			D3D12_PIPELINE_STATE_SUBOBJECT_TYPE ObjType;
			InnerType ObjectData;
		};
		if (m_pSubobjectLocations[ObjectType] < 0)
		{
			SubobjectType* pType = reinterpret_cast<SubobjectType*>(m_pSubobjectData.data() + m_Size);
			pType->ObjType = ObjectType;
			m_pSubobjectLocations[ObjectType] = m_Size;

			const auto AlignUp = [](uint32_t value, uint32_t alignment) { return (value + (alignment - 1)) & ~(alignment - 1); };
			m_Size += AlignUp(sizeof(SubobjectType), sizeof(void*));
			m_Subobjects++;
		}
		int offset = m_pSubobjectLocations[ObjectType];
		SubobjectType* pObj = reinterpret_cast<SubobjectType*>(m_pSubobjectData.data() + offset);
		return pObj->ObjectData;
	}

	const char* m_pName;
	VertexElementLayout m_InputLayout;
	PipelineStateType m_Type{PipelineStateType::MAX};
	std::array<Shader*, (int)ShaderType::MAX> m_Shaders{};

	std::array<int, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_MAX_VALID> m_pSubobjectLocations{};
	std::array<std::byte, sizeof(CD3DX12_PIPELINE_STATE_STREAM2)> m_pSubobjectData{};
	uint32_t m_Subobjects{0};
	uint32_t m_Size{0};
};

class PipelineState : public GraphicsObject
{
public:
	PipelineState(ShaderManager* pShaderManager, GraphicsDevice* pParent);
	PipelineState(const PipelineState& other) = delete;
	PipelineState& operator=(const PipelineState& other) = delete;
	~PipelineState() = default;

	void Create(const PipelineStateInitializer& initializer);
	void ConditionallyReload();

	PipelineStateType GetType() const { return m_Desc.m_Type; }
	ID3D12PipelineState* GetPipelineState() const { return m_pPipelineState.Get(); }

protected:
	void OnShaderReloaded(Shader* pOldShader, Shader* pNewShader);

	ComPtr<ID3D12PipelineState> m_pPipelineState;

	PipelineStateInitializer m_Desc;
	DelegateHandle m_ReloadHandle;
	bool m_NeedsReload{false};
};
