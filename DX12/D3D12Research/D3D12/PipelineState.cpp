#include "stdafx.h"
#include "PipelineState.h"

PipelineState::PipelineState()
{
	m_Desc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	m_Desc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	m_Desc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	m_Desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
	m_Desc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
	m_Desc.NumRenderTargets = 1;
	m_Desc.SampleDesc.Count = 1;
	m_Desc.SampleDesc.Quality = 0;
    m_Desc.SampleMask = UINT_MAX;
	m_Desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	m_Desc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
    m_Desc.NodeMask = 0;
}

void PipelineState::Finalize(ID3D12Device* pDevice)
{
	pDevice->CreateGraphicsPipelineState(&m_Desc, IID_PPV_ARGS(m_pPipelineState.GetAddressOf()));
}

void PipelineState::SetBlendMode(const BlendMode& blendMode, bool alphaToCoverage)
{
    D3D12_RENDER_TARGET_BLEND_DESC& desc = m_Desc.BlendState.RenderTarget[0];
    desc.RenderTargetWriteMask = 0xf;
    desc.BlendEnable = blendMode == BlendMode::REPLACE ? false : true;

    switch (blendMode)
    {
    case BlendMode::REPLACE:
        desc.SrcBlend = D3D12_BLEND_ONE;
        desc.DestBlend = D3D12_BLEND_ZERO;
        desc.BlendOp = D3D12_BLEND_OP_ADD;
        desc.SrcBlendAlpha = D3D12_BLEND_ONE;
        desc.DestBlendAlpha = D3D12_BLEND_ZERO;
        desc.BlendOpAlpha = D3D12_BLEND_OP_ADD;
        break;
    case BlendMode::ALPHA:
        desc.SrcBlend = D3D12_BLEND_SRC_ALPHA;
        desc.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
        desc.BlendOp = D3D12_BLEND_OP_ADD;
        desc.SrcBlendAlpha = D3D12_BLEND_SRC_ALPHA;
        desc.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
        desc.BlendOpAlpha = D3D12_BLEND_OP_ADD;
        break;
    case BlendMode::ADD:
        desc.SrcBlend = D3D12_BLEND_SRC_ALPHA;
        desc.DestBlend = D3D12_BLEND_ONE;
        desc.BlendOp = D3D12_BLEND_OP_ADD;
        desc.SrcBlendAlpha = D3D12_BLEND_ONE;
        desc.DestBlendAlpha = D3D12_BLEND_ONE;
        desc.BlendOpAlpha = D3D12_BLEND_OP_ADD;
        break;
    case BlendMode::MULTIPLY:
        desc.SrcBlend = D3D12_BLEND_DEST_COLOR;
        desc.DestBlend = D3D12_BLEND_ZERO;
        desc.BlendOp = D3D12_BLEND_OP_ADD;
        desc.SrcBlendAlpha = D3D12_BLEND_DEST_COLOR;
        desc.DestBlendAlpha = D3D12_BLEND_ZERO;
        desc.BlendOpAlpha = D3D12_BLEND_OP_ADD;
        break;
    case BlendMode::ADDALPHA:
        desc.SrcBlend = D3D12_BLEND_SRC_ALPHA;
        desc.DestBlend = D3D12_BLEND_ONE;
        desc.BlendOp = D3D12_BLEND_OP_ADD;
        desc.SrcBlendAlpha = D3D12_BLEND_SRC_ALPHA;
        desc.DestBlendAlpha = D3D12_BLEND_ONE;
        desc.BlendOpAlpha = D3D12_BLEND_OP_ADD;
        break;
    case BlendMode::PREMULALPHA:
        desc.SrcBlend = D3D12_BLEND_ONE;
        desc.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
        desc.BlendOp = D3D12_BLEND_OP_ADD;
        desc.SrcBlendAlpha = D3D12_BLEND_ONE;
        desc.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
        desc.BlendOpAlpha = D3D12_BLEND_OP_ADD;
        break;
    case BlendMode::INVDESTALPHA:
        desc.SrcBlend = D3D12_BLEND_INV_DEST_ALPHA;
        desc.DestBlend = D3D12_BLEND_DEST_ALPHA;
        desc.BlendOp = D3D12_BLEND_OP_ADD;
        desc.SrcBlendAlpha = D3D12_BLEND_INV_DEST_ALPHA;
        desc.DestBlendAlpha = D3D12_BLEND_DEST_ALPHA;
        desc.BlendOpAlpha = D3D12_BLEND_OP_ADD;
        break;
    case BlendMode::SUBTRACT:
        desc.SrcBlend = D3D12_BLEND_ONE;
        desc.DestBlend = D3D12_BLEND_ONE;
        desc.BlendOp = D3D12_BLEND_OP_REV_SUBTRACT;
        desc.SrcBlendAlpha = D3D12_BLEND_ONE;
        desc.DestBlendAlpha = D3D12_BLEND_ONE;
        desc.BlendOpAlpha = D3D12_BLEND_OP_REV_SUBTRACT;
        break;
    case BlendMode::SUBTRACTALPHA:
        desc.SrcBlend = D3D12_BLEND_SRC_ALPHA;
        desc.DestBlend = D3D12_BLEND_ONE;
        desc.BlendOp = D3D12_BLEND_OP_REV_SUBTRACT;
        desc.SrcBlendAlpha = D3D12_BLEND_SRC_ALPHA;
        desc.DestBlendAlpha = D3D12_BLEND_ONE;
        desc.BlendOpAlpha = D3D12_BLEND_OP_REV_SUBTRACT;
        break;
    case BlendMode::UNDEFINED:
    default:
        break;
    }
}

void PipelineState::SetDepthEnable(bool enabled)
{
	m_Desc.DepthStencilState.DepthEnable = enabled;
}

void PipelineState::SetDepthWrite(bool enabled)
{
	m_Desc.DepthStencilState.DepthWriteMask = enabled ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
}

void PipelineState::SetDepthTest(const D3D12_COMPARISON_FUNC func)
{
	m_Desc.DepthStencilState.DepthFunc = func;
}

void PipelineState::SetStencilTest(bool stencilEnabled, D3D12_COMPARISON_FUNC mode, D3D12_STENCIL_OP pass, D3D12_STENCIL_OP fail, D3D12_STENCIL_OP zFail, uint32_t stencilRef, uint8_t compareMask, uint8_t writeMask)
{
	m_Desc.DepthStencilState.StencilEnable = stencilEnabled;
	m_Desc.DepthStencilState.FrontFace.StencilFunc = mode;
	m_Desc.DepthStencilState.FrontFace.StencilPassOp = pass;
	m_Desc.DepthStencilState.FrontFace.StencilFailOp = fail;
	m_Desc.DepthStencilState.FrontFace.StencilDepthFailOp = zFail;
	m_Desc.DepthStencilState.StencilReadMask = compareMask;
	m_Desc.DepthStencilState.StencilWriteMask = writeMask;
	m_Desc.DepthStencilState.BackFace = m_Desc.DepthStencilState.FrontFace;
}

void PipelineState::SetScissorEnabled(bool enabled)
{
}

void PipelineState::SetMultisampleEnabled(bool enabled)
{
}

void PipelineState::SetFillMode(D3D12_FILL_MODE fillMode)
{
	m_Desc.RasterizerState.FillMode = fillMode;
}

void PipelineState::SetCullMode(D3D12_CULL_MODE cullMode)
{
	m_Desc.RasterizerState.CullMode = cullMode;
}

void PipelineState::SetLineAntialias(bool lineAntialias)
{
	m_Desc.RasterizerState.AntialiasedLineEnable = lineAntialias;
}

void PipelineState::SetInputLayout(D3D12_INPUT_ELEMENT_DESC* pElements, uint32_t count)
{
	m_Desc.InputLayout.NumElements = count;
	m_Desc.InputLayout.pInputElementDescs = pElements;
}

void PipelineState::SetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY_TYPE topology)
{
	m_Desc.PrimitiveTopologyType = topology;
}

void PipelineState::SetRootSignature(ID3D12RootSignature* pRootSignature)
{
	m_Desc.pRootSignature = pRootSignature;
}

void PipelineState::SetVertexShader(const void* byteCode, uint32_t byteCodeLength)
{
	m_Desc.VS.pShaderBytecode = byteCode;
	m_Desc.VS.BytecodeLength = byteCodeLength;
}

void PipelineState::SetPixelShader(const void* byteCode, uint32_t byteCodeLength)
{
	m_Desc.PS.pShaderBytecode = byteCode;
	m_Desc.PS.BytecodeLength = byteCodeLength;
}
