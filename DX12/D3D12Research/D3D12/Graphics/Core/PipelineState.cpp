#include "stdafx.h"
#include "PipelineState.h"
#include "Shader.h"

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

void PipelineState::SetVertexShader(const Shader& shader)
{
    m_Type = PipelineStateType::Graphics;
    m_Desc.VS = { shader.GetByteCode(), shader.GetByteCodeSize() };
}

void PipelineState::SetPixelShader(const Shader& shader)
{
	m_Desc.PS = { shader.GetByteCode(), shader.GetByteCodeSize() };
}

void PipelineState::SetHullShader(const Shader& shader)
{
	m_Type = PipelineStateType::Graphics;
	m_Desc.HS = { shader.GetByteCode(), shader.GetByteCodeSize() };
}

void PipelineState::SetDomainShader(const Shader& shader)
{
	m_Type = PipelineStateType::Graphics;
	m_Desc.DS = { shader.GetByteCode(), shader.GetByteCodeSize() };
}

void PipelineState::SetGeometryShader(const Shader& shader)
{
	m_Type = PipelineStateType::Graphics;
	m_Desc.GS = { shader.GetByteCode(), shader.GetByteCodeSize() };
}

void PipelineState::SetComputeShader(const Shader& shader)
{
	m_Type = PipelineStateType::Compute;
	m_Desc.CS = { shader.GetByteCode(), shader.GetByteCodeSize() };
}

void PipelineState::SetMeshShader(const Shader& shader)
{
	m_Type = PipelineStateType::Mesh;
	m_Desc.MS = { shader.GetByteCode(), shader.GetByteCodeSize() };
}

void PipelineState::SetAmplificationShader(const Shader& shader)
{
	m_Type = PipelineStateType::Mesh;
	m_Desc.AS = { shader.GetByteCode(), shader.GetByteCodeSize() };
}

StateObjectDesc::StateObjectDesc(D3D12_STATE_OBJECT_TYPE type)
    : m_ScratchAllocator(0xFFFF), m_StateObjectAllocator(0xFF), m_Type(type)
{}

uint32_t StateObjectDesc::AddLibrary(const ShaderLibrary& shader, const std::vector<std::string>& exports)
{
    D3D12_DXIL_LIBRARY_DESC* pDesc = m_ScratchAllocator.Allocate<D3D12_DXIL_LIBRARY_DESC>();
    pDesc->DXILLibrary.pShaderBytecode = shader.GetByteCode();
    pDesc->DXILLibrary.BytecodeLength = shader.GetByteCodeSize();
    if (exports.size())
    {
        D3D12_EXPORT_DESC* pExports = m_ScratchAllocator.Allocate<D3D12_EXPORT_DESC>((uint32_t)exports.size());
        D3D12_EXPORT_DESC* pCurrentExport = pExports;
        for (const std::string& exportName : exports)
        {
            uint32_t len = (uint32_t)exportName.length();
            wchar_t* pNameData = m_ScratchAllocator.Allocate<wchar_t>(len + 1);
            MultiByteToWideChar(0, 0, exportName.c_str(), len, pNameData, len);
            pCurrentExport->Name = pNameData;
            pCurrentExport->ExportToRename = pNameData;
            pCurrentExport->Flags = D3D12_EXPORT_FLAG_NONE;
            pCurrentExport++;
        }
        pDesc->NumExports = (uint32_t)exports.size();
        pDesc->pExports = pExports;
    }
    return AddStateObject(pDesc, D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY);
}

uint32_t StateObjectDesc::AddHitGroup(const char* pHitGroupExport, const char* pClosestHigShaderImport, const char* pAnyHitShaderImport, const char* pIntersectionShaderImport)
{
    check(pHitGroupExport);
    D3D12_HIT_GROUP_DESC* pDesc = m_ScratchAllocator.Allocate<D3D12_HIT_GROUP_DESC>();
    {
        int len = (int)strlen(pHitGroupExport);
        wchar_t* pNameData = m_ScratchAllocator.Allocate<wchar_t>(len + 1);
        MultiByteToWideChar(0, 0, pHitGroupExport, len, pNameData, len);
        pDesc->HitGroupExport = pNameData;
    }
    if (pClosestHigShaderImport)
    {
        uint32_t len = (uint32_t)strlen(pClosestHigShaderImport);
        wchar_t* pNameData = m_ScratchAllocator.Allocate<wchar_t>(len + 1);
        MultiByteToWideChar(0, 0, pClosestHigShaderImport, len, pNameData, len);
        pDesc->ClosestHitShaderImport = pNameData;
    }
    if (pAnyHitShaderImport)
    {
        uint32_t len = (uint32_t)strlen(pAnyHitShaderImport);
        wchar_t* pNameData = m_ScratchAllocator.Allocate<wchar_t>(len + 1);
        MultiByteToWideChar(0, 0, pAnyHitShaderImport, len, pNameData, len);
        pDesc->AnyHitShaderImport = pNameData;
    }
    if (pIntersectionShaderImport)
    {
        uint32_t len = (uint32_t)strlen(pIntersectionShaderImport);
        wchar_t* pNameData = m_ScratchAllocator.Allocate<wchar_t>(len + 1);
        MultiByteToWideChar(0, 0, pIntersectionShaderImport, len, pNameData, len);
        pDesc->IntersectionShaderImport = pNameData;
    }

    pDesc->Type = pIntersectionShaderImport ? D3D12_HIT_GROUP_TYPE_PROCEDURAL_PRIMITIVE : D3D12_HIT_GROUP_TYPE_TRIANGLES;
    return AddStateObject(pDesc, D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP);
}

uint32_t StateObjectDesc::AddStateAssociation(uint32_t index, const std::vector<std::string>& exports)
{
    check(exports.size());
    D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION* pAssociation = m_ScratchAllocator.Allocate<D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION>();
    pAssociation->NumExports = (uint32_t)exports.size();
    pAssociation->pSubobjectToAssociate = GetSubobject(index);
    const wchar_t** pExportList = m_ScratchAllocator.Allocate<const wchar_t*>(pAssociation->NumExports);
    pAssociation->pExports = pExportList;
    for (size_t i = 0; i < exports.size(); i++)
    {
        uint32_t len = (uint32_t)exports[i].length();
        wchar_t* pNameData = m_ScratchAllocator.Allocate<wchar_t>(len + 1);
        MultiByteToWideChar(0, 0, exports[i].c_str(), len, pNameData, len);
        pExportList[i] = pNameData;
    }
    return AddStateObject(pAssociation, D3D12_STATE_SUBOBJECT_TYPE_SUBOBJECT_TO_EXPORTS_ASSOCIATION);
}

uint32_t StateObjectDesc::AddCollection(ID3D12StateObject* pStateObject, const std::vector<std::string>& exports)
{
    D3D12_EXISTING_COLLECTION_DESC* pDesc = m_ScratchAllocator.Allocate<D3D12_EXISTING_COLLECTION_DESC>();
    pDesc->pExistingCollection = pStateObject;
    if (exports.size())
    {
        D3D12_EXPORT_DESC* pExports = m_ScratchAllocator.Allocate<D3D12_EXPORT_DESC>((uint32_t)exports.size());
        D3D12_EXPORT_DESC* pCurrentExport = pExports;
        for (const std::string& exportName : exports)
        {
            uint32_t len = (uint32_t)exportName.length();
            wchar_t* pNameData = m_ScratchAllocator.Allocate<wchar_t>(len + 1);
            MultiByteToWideChar(0, 0, exportName.c_str(), len, pNameData, len);
            pCurrentExport->Name = pNameData;
            pCurrentExport->ExportToRename = pNameData;
            pCurrentExport->Flags = D3D12_EXPORT_FLAG_NONE;
            pCurrentExport++;
        }
        pDesc->NumExports = (uint32_t)exports.size();
        pDesc->pExports = pExports;
    }
    return AddStateObject(pDesc, D3D12_STATE_SUBOBJECT_TYPE_EXISTING_COLLECTION);
}

uint32_t StateObjectDesc::BindLocalRootSignature(const char* pExportName, ID3D12RootSignature* pRootSignature)
{
    D3D12_LOCAL_ROOT_SIGNATURE* pRS = m_ScratchAllocator.Allocate<D3D12_LOCAL_ROOT_SIGNATURE>();
    pRS->pLocalRootSignature = pRootSignature;
    uint32_t rsState = AddStateObject(pRS, D3D12_STATE_SUBOBJECT_TYPE_LOCAL_ROOT_SIGNATURE);
    return AddStateAssociation(rsState, { pExportName });
}

uint32_t StateObjectDesc::SetRaytracingShaderConfig(uint32_t maxPayloadSize, uint32_t maxAttributeSize)
{
    D3D12_RAYTRACING_SHADER_CONFIG* pConfig = m_ScratchAllocator.Allocate<D3D12_RAYTRACING_SHADER_CONFIG>();
    pConfig->MaxPayloadSizeInBytes = maxPayloadSize;
    pConfig->MaxAttributeSizeInBytes = maxAttributeSize;
    return AddStateObject(pConfig, D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG);
}

uint32_t StateObjectDesc::SetRaytracingPipelineConfig(uint32_t maxRecursionDepth, D3D12_RAYTRACING_PIPELINE_FLAGS flags)
{
    D3D12_RAYTRACING_PIPELINE_CONFIG1* pConfig = m_ScratchAllocator.Allocate<D3D12_RAYTRACING_PIPELINE_CONFIG1>();
    pConfig->MaxTraceRecursionDepth = maxRecursionDepth;
    pConfig->Flags = flags;
    return AddStateObject(pConfig, D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG);
}

uint32_t StateObjectDesc::SetGlobalRootSignature(ID3D12RootSignature* pRootSignature)
{
    D3D12_GLOBAL_ROOT_SIGNATURE* pRS = m_ScratchAllocator.Allocate<D3D12_GLOBAL_ROOT_SIGNATURE>();
    pRS->pGlobalRootSignature = pRootSignature;
    return AddStateObject(pRS, D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE);
}

ComPtr<ID3D12StateObject> StateObjectDesc::Finalize(const char* pName, ID3D12Device5* pDevice) const
{
    D3D12_STATE_OBJECT_DESC desc = {
        .Type = m_Type,
        .NumSubobjects = m_SubObjects,
        .pSubobjects = (D3D12_STATE_SUBOBJECT*)m_StateObjectAllocator.Data(),
    };
    ComPtr<ID3D12StateObject> pStateObject;
    VERIFY_HR_EX(pDevice->CreateStateObject(&desc, IID_PPV_ARGS(pStateObject.GetAddressOf())), pDevice);
    return pStateObject;
}

uint32_t StateObjectDesc::AddStateObject(void* pDesc, D3D12_STATE_SUBOBJECT_TYPE type)
{
    D3D12_STATE_SUBOBJECT* pSubObject = m_StateObjectAllocator.Allocate<D3D12_STATE_SUBOBJECT>();
    pSubObject->Type = type;
    pSubObject->pDesc = pDesc;
    return m_SubObjects++;
}
