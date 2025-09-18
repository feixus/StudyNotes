#pragma once
#include "GraphicsResource.h"

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

class PipelineState : public GraphicsObject
{
public:
	PipelineState(Graphics* pParent);
	PipelineState(const PipelineState& other);
	~PipelineState() = default;

	ID3D12PipelineState* GetPipelineState() const { return m_pPipelineState.Get(); }

	void Finalize(const char* pName);

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