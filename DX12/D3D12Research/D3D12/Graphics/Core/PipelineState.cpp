#include "stdafx.h"
#include "PipelineState.h"

PipelineState::PipelineState()
{
	m_Desc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	m_Desc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC1(D3D12_DEFAULT);
	m_Desc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	m_Desc.SampleDesc = DefaultSampleDesc();
    m_Desc.SampleMask = DefaultSampleMask();
	m_Desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	m_Desc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
    m_Desc.NodeMask = 0;
}

PipelineState::PipelineState(const PipelineState& other) : m_Desc(other.m_Desc)
{}

void PipelineState::Finalize(const char* pName, ID3D12Device* pDevice)
{
    ComPtr<ID3D12Device2> pDevice2;
    VERIFY_HR_EX(pDevice->QueryInterface(IID_PPV_ARGS(pDevice2.GetAddressOf())), pDevice);

    D3D12_PIPELINE_STATE_STREAM_DESC streamDesc = {
        .SizeInBytes = sizeof(m_Desc),
        .pPipelineStateSubobjectStream = &m_Desc,
    };
    VERIFY_HR_EX(pDevice2->CreatePipelineState(&streamDesc, IID_PPV_ARGS(m_pPipelineState.GetAddressOf())), pDevice);

    D3D_SETNAME(m_pPipelineState.Get(), pName);
}

void PipelineState::SetRenderTargetFormat(DXGI_FORMAT rtvFormat, DXGI_FORMAT dsvFormat, uint32_t msaa)
{
	SetRenderTargetFormats(&rtvFormat, 1, dsvFormat, msaa);
}

void PipelineState::SetRenderTargetFormats(DXGI_FORMAT* rtvFormats, uint32_t count, DXGI_FORMAT dsvFormat, uint32_t msaa)
{
    D3D12_RT_FORMAT_ARRAY* pFormatArray = &m_Desc.RTVFormats;
    pFormatArray->NumRenderTargets = count;
    for (uint32_t i = 0; i < count; i++)
    {
        pFormatArray->RTFormats[i] = rtvFormats[i];
    }
    DXGI_SAMPLE_DESC* pSampleDesc = &m_Desc.SampleDesc;
    pSampleDesc->Count = msaa;
    pSampleDesc->Quality = 0;
    m_Desc.DSVFormat = dsvFormat;
}

void PipelineState::SetBlendMode(const BlendMode& blendMode, bool /*alphaToCoverage*/)
{
    CD3DX12_BLEND_DESC* pBlendDesc = &m_Desc.BlendState;
    D3D12_RENDER_TARGET_BLEND_DESC& desc = pBlendDesc->RenderTarget[0];
    desc.RenderTargetWriteMask = 0xf;
    desc.BlendEnable = blendMode == BlendMode::Replace ? false : true;

    switch (blendMode)
    {
    case BlendMode::Replace:
        desc.SrcBlend = D3D12_BLEND_ONE;
        desc.DestBlend = D3D12_BLEND_ZERO;
        desc.BlendOp = D3D12_BLEND_OP_ADD;
        desc.SrcBlendAlpha = D3D12_BLEND_ONE;
        desc.DestBlendAlpha = D3D12_BLEND_ZERO;
        desc.BlendOpAlpha = D3D12_BLEND_OP_ADD;
        break;
    case BlendMode::Alpha:
        desc.SrcBlend = D3D12_BLEND_SRC_ALPHA;
        desc.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
        desc.BlendOp = D3D12_BLEND_OP_ADD;
        desc.SrcBlendAlpha = D3D12_BLEND_SRC_ALPHA;
        desc.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
        desc.BlendOpAlpha = D3D12_BLEND_OP_ADD;
        break;
    case BlendMode::Add:
        desc.SrcBlend = D3D12_BLEND_SRC_ALPHA;
        desc.DestBlend = D3D12_BLEND_ONE;
        desc.BlendOp = D3D12_BLEND_OP_ADD;
        desc.SrcBlendAlpha = D3D12_BLEND_ONE;
        desc.DestBlendAlpha = D3D12_BLEND_ONE;
        desc.BlendOpAlpha = D3D12_BLEND_OP_ADD;
        break;
    case BlendMode::Multiply:
        desc.SrcBlend = D3D12_BLEND_DEST_COLOR;
        desc.DestBlend = D3D12_BLEND_ZERO;
        desc.BlendOp = D3D12_BLEND_OP_ADD;
        desc.SrcBlendAlpha = D3D12_BLEND_DEST_COLOR;
        desc.DestBlendAlpha = D3D12_BLEND_ZERO;
        desc.BlendOpAlpha = D3D12_BLEND_OP_ADD;
        break;
    case BlendMode::AddAlpha:
        desc.SrcBlend = D3D12_BLEND_SRC_ALPHA;
        desc.DestBlend = D3D12_BLEND_ONE;
        desc.BlendOp = D3D12_BLEND_OP_ADD;
        desc.SrcBlendAlpha = D3D12_BLEND_SRC_ALPHA;
        desc.DestBlendAlpha = D3D12_BLEND_ONE;
        desc.BlendOpAlpha = D3D12_BLEND_OP_ADD;
        break;
    case BlendMode::PreMultiplyAlpha:
        desc.SrcBlend = D3D12_BLEND_ONE;
        desc.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
        desc.BlendOp = D3D12_BLEND_OP_ADD;
        desc.SrcBlendAlpha = D3D12_BLEND_ONE;
        desc.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
        desc.BlendOpAlpha = D3D12_BLEND_OP_ADD;
        break;
    case BlendMode::InverseDestinationAlpha:
        desc.SrcBlend = D3D12_BLEND_INV_DEST_ALPHA;
        desc.DestBlend = D3D12_BLEND_DEST_ALPHA;
        desc.BlendOp = D3D12_BLEND_OP_ADD;
        desc.SrcBlendAlpha = D3D12_BLEND_INV_DEST_ALPHA;
        desc.DestBlendAlpha = D3D12_BLEND_DEST_ALPHA;
        desc.BlendOpAlpha = D3D12_BLEND_OP_ADD;
        break;
    case BlendMode::Subtract:
        desc.SrcBlend = D3D12_BLEND_ONE;
        desc.DestBlend = D3D12_BLEND_ONE;
        desc.BlendOp = D3D12_BLEND_OP_REV_SUBTRACT;
        desc.SrcBlendAlpha = D3D12_BLEND_ONE;
        desc.DestBlendAlpha = D3D12_BLEND_ONE;
        desc.BlendOpAlpha = D3D12_BLEND_OP_REV_SUBTRACT;
        break;
    case BlendMode::SubtractAlpha:
        desc.SrcBlend = D3D12_BLEND_SRC_ALPHA;
        desc.DestBlend = D3D12_BLEND_ONE;
        desc.BlendOp = D3D12_BLEND_OP_REV_SUBTRACT;
        desc.SrcBlendAlpha = D3D12_BLEND_SRC_ALPHA;
        desc.DestBlendAlpha = D3D12_BLEND_ONE;
        desc.BlendOpAlpha = D3D12_BLEND_OP_REV_SUBTRACT;
        break;
    case BlendMode::Undefined:
    default:
        break;
    }
}

void PipelineState::SetDepthEnable(bool enabled)
{
    CD3DX12_DEPTH_STENCIL_DESC1* pDssDesc = &m_Desc.DepthStencilState;
    pDssDesc->DepthEnable = enabled;
}

void PipelineState::SetDepthWrite(bool enabled)
{
    CD3DX12_DEPTH_STENCIL_DESC1* pDssDesc = &m_Desc.DepthStencilState;
    pDssDesc->DepthWriteMask = enabled ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
}

void PipelineState::SetDepthTest(const D3D12_COMPARISON_FUNC func)
{
    CD3DX12_DEPTH_STENCIL_DESC1* pDssDesc = &m_Desc.DepthStencilState;
    pDssDesc->DepthFunc = func;
}

void PipelineState::SetStencilTest(bool stencilEnabled, D3D12_COMPARISON_FUNC mode, D3D12_STENCIL_OP pass, D3D12_STENCIL_OP fail, D3D12_STENCIL_OP zFail, uint32_t /*stencilRef*/, uint8_t compareMask, uint8_t writeMask)
{
    CD3DX12_DEPTH_STENCIL_DESC1* pDssDesc = &m_Desc.DepthStencilState;
    pDssDesc->StencilEnable = stencilEnabled;
    pDssDesc->FrontFace.StencilFunc = mode;
    pDssDesc->FrontFace.StencilPassOp = pass;
    pDssDesc->FrontFace.StencilFailOp = fail;
    pDssDesc->FrontFace.StencilDepthFailOp = zFail;
    pDssDesc->StencilReadMask = compareMask;
    pDssDesc->StencilWriteMask = writeMask;
    pDssDesc->BackFace = pDssDesc->FrontFace;
}

void PipelineState::SetFillMode(D3D12_FILL_MODE fillMode)
{
    CD3DX12_RASTERIZER_DESC* pRsDesc = &m_Desc.RasterizerState;
    pRsDesc->FillMode = fillMode;
}

void PipelineState::SetCullMode(D3D12_CULL_MODE cullMode)
{
    CD3DX12_RASTERIZER_DESC* pRsDesc = &m_Desc.RasterizerState;
    pRsDesc->CullMode = cullMode;
}

void PipelineState::SetLineAntialias(bool lineAntialias)
{
    CD3DX12_RASTERIZER_DESC* pRsDesc = &m_Desc.RasterizerState;
    pRsDesc->AntialiasedLineEnable = lineAntialias;
}

void PipelineState::SetDepthBias(int depthBias, float depthBiasClamp, float slopeScaledDepthBias)
{
    // final depth = depthFromInterpolation + (slopeScaledDepthBias * max(abs(dz/dx), abs(dz/dy))) + depthBias * r. (r is the 1/2^n, such as 1/16777216 for 24-bit)
    CD3DX12_RASTERIZER_DESC* pRsDesc = &m_Desc.RasterizerState;
    pRsDesc->SlopeScaledDepthBias = slopeScaledDepthBias;
    pRsDesc->DepthBias = depthBias;
    pRsDesc->DepthBiasClamp = depthBiasClamp;
}

void PipelineState::SetInputLayout(D3D12_INPUT_ELEMENT_DESC* pElements, uint32_t count)
{
    D3D12_INPUT_LAYOUT_DESC* pIlDesc = &m_Desc.InputLayout;
    pIlDesc->NumElements = count;
    pIlDesc->pInputElementDescs = pElements;
}

void PipelineState::SetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY_TYPE topology)
{
	m_Desc.PrimitiveTopologyType = topology;
}

void PipelineState::SetRootSignature(ID3D12RootSignature* pRootSignature)
{
	m_Desc.pRootSignature = pRootSignature;
}

void PipelineState::SetVertexShader(const void* pByteCode, uint32_t byteCodeLength)
{
	m_Desc.VS = {pByteCode, byteCodeLength };
}

void PipelineState::SetPixelShader(const void* pByteCode, uint32_t byteCodeLength)
{
	m_Desc.PS = {pByteCode, byteCodeLength };
}

void PipelineState::SetGeometryShader(const void* pByteCode, uint32_t byteCodeLength)
{
	m_Desc.GS = { pByteCode, byteCodeLength };
}

void PipelineState::SetComputeShader(const void* pByteCode, uint32_t byteCodeLength)
{
    m_Desc.CS = { pByteCode, byteCodeLength };
}
