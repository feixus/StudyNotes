#pragma once

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

class PipelineState
{
public:
	PipelineState();
	PipelineState(const PipelineState& other);
	ID3D12PipelineState* GetPipelineState() const { return m_pPipelineState.Get(); }

	void Finalize(const char* pName, ID3D12Device* pDevice);

	void SetRenderTargetFormat(DXGI_FORMAT rtvFormat, DXGI_FORMAT dsvFormat, uint32_t msaa, uint32_t msaaQuality);
	void SetRenderTargetFormats(DXGI_FORMAT* rtvFormats, uint32_t count, DXGI_FORMAT dsvFormat, uint32_t msaa, uint32_t msaaQuality);

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

	void SetVertexShader(const void* pByteCode, uint32_t byteCodeLength);
	void SetPixelShader(const void* pByteCode, uint32_t byteCodeLength);
	void SetGeometryShader(const void* pByteCode, uint32_t byteCodeLength);
	void SetComputeShader(const void* pByteCode, uint32_t byteCodeLength);

protected:
	ComPtr<ID3D12PipelineState> m_pPipelineState;
	CD3DX12_PIPELINE_STATE_STREAM1 m_Desc{};
};