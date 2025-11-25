#include "stdafx.h"
#include "PipelineState.h"
#include "Shader.h"
#include "Graphics.h"

VertexElementLayout::VertexElementLayout(const VertexElementLayout& rhs)
    : m_ElementDesc(rhs.m_ElementDesc), m_SemanticNames(rhs.m_SemanticNames)
{
    FixupString();
}

VertexElementLayout& VertexElementLayout::operator=(const VertexElementLayout& rhs)
{
    m_ElementDesc = rhs.m_ElementDesc;
    m_SemanticNames = rhs.m_SemanticNames;
    FixupString();
    return *this;
}

void VertexElementLayout::AddVertexElement(const char* pSemantic, DXGI_FORMAT format, uint32_t semanticIndex, uint32_t byteOffset, uint32_t inputSlot)
{
    m_SemanticNames.push_back(pSemantic);
    m_ElementDesc.resize(m_ElementDesc.size() + 1);
    D3D12_INPUT_ELEMENT_DESC& element = m_ElementDesc.back();
    element.AlignedByteOffset = byteOffset;
    element.Format = format;
    element.InputSlot = inputSlot;
    element.InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
    element.InstanceDataStepRate = 0;
    element.SemanticIndex = semanticIndex;
    FixupString();
}

void VertexElementLayout::AddInstanceElement(const char* pSemantic, DXGI_FORMAT format, uint32_t semanticIndex, uint32_t byteOffset, uint32_t inputSlot, uint32_t stepRate)
{
    m_SemanticNames.push_back(pSemantic);
    m_ElementDesc.resize(m_ElementDesc.size() + 1);
    D3D12_INPUT_ELEMENT_DESC& element = m_ElementDesc.back();
    element.AlignedByteOffset = byteOffset;
    element.Format = format;
    element.InputSlot = inputSlot;
    element.InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA;
    element.InstanceDataStepRate = stepRate;
    element.SemanticIndex = semanticIndex;
    FixupString();
}

void VertexElementLayout::FixupString()
{
    for (size_t i = 0; i < m_ElementDesc.size(); i++)
    {
        m_ElementDesc[i].SemanticName = m_SemanticNames[i].c_str();
    }
}

PipelineStateInitializer::PipelineStateInitializer()
{
    m_pSubobjectLocations.fill(-1);

    GetSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_BLEND>() = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    GetSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL1>() = CD3DX12_DEPTH_STENCIL_DESC1(D3D12_DEFAULT);
    GetSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER>() = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    GetSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_DESC>() = DefaultSampleDesc();
    GetSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_MASK>() = DefaultSampleMask();
    GetSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PRIMITIVE_TOPOLOGY>() = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    GetSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_FLAGS>() = D3D12_PIPELINE_STATE_FLAG_NONE;
}

void PipelineStateInitializer::SetName(const char* pName)
{
    m_Name = pName;
}

void PipelineStateInitializer::SetDepthOnlyTarget(DXGI_FORMAT dsvFormat, uint32_t msaa)
{
    D3D12_RT_FORMAT_ARRAY& formatArray = GetSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RENDER_TARGET_FORMATS>();
    formatArray.NumRenderTargets = 0;

    DXGI_SAMPLE_DESC& sampleDesc = GetSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_DESC>();
    sampleDesc.Count = msaa;
    sampleDesc.Quality = 0;

    GetSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER>().MultisampleEnable = msaa > 1;
    GetSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL_FORMAT>() = dsvFormat;
}

void PipelineStateInitializer::SetRenderTargetFormat(DXGI_FORMAT rtvFormat, DXGI_FORMAT dsvFormat, uint32_t msaa)
{
	SetRenderTargetFormats(&rtvFormat, 1, dsvFormat, msaa);
}

void PipelineStateInitializer::SetRenderTargetFormats(DXGI_FORMAT* rtvFormats, uint32_t count, DXGI_FORMAT dsvFormat, uint32_t msaa)
{
    D3D12_RT_FORMAT_ARRAY& formatArray = GetSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RENDER_TARGET_FORMATS>();
    for (uint32_t i = 0; i < count; i++)
    {
        formatArray.RTFormats[i] = rtvFormats[i];
    }
    formatArray.NumRenderTargets = count;

    DXGI_SAMPLE_DESC& sampleDesc = GetSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_DESC>();
    sampleDesc.Count = msaa;
    sampleDesc.Quality = 0;
    
    GetSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER>().MultisampleEnable = msaa > 1;
    GetSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL_FORMAT>() = dsvFormat;
}

void PipelineStateInitializer::SetBlendMode(const BlendMode& blendMode, bool /*alphaToCoverage*/)
{
    D3D12_BLEND_DESC& blendDesc = GetSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_BLEND>();
    D3D12_RENDER_TARGET_BLEND_DESC& desc = blendDesc.RenderTarget[0];
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

void PipelineStateInitializer::SetDepthEnable(bool enabled)
{
    GetSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL1>().DepthEnable = enabled;
}

void PipelineStateInitializer::SetDepthWrite(bool enabled)
{
    GetSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL1>().DepthWriteMask = enabled ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
}

void PipelineStateInitializer::SetDepthTest(const D3D12_COMPARISON_FUNC func)
{
    GetSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL1>().DepthFunc = func;
}

void PipelineStateInitializer::SetStencilTest(bool stencilEnabled, D3D12_COMPARISON_FUNC mode, D3D12_STENCIL_OP pass, D3D12_STENCIL_OP fail, D3D12_STENCIL_OP zFail, uint32_t /*stencilRef*/, uint8_t compareMask, uint8_t writeMask)
{
    D3D12_DEPTH_STENCIL_DESC1& dssDesc = GetSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL1>();
    dssDesc.StencilEnable = stencilEnabled;
    dssDesc.FrontFace.StencilFunc = mode;
    dssDesc.FrontFace.StencilPassOp = pass;
    dssDesc.FrontFace.StencilFailOp = fail;
    dssDesc.FrontFace.StencilDepthFailOp = zFail;
    dssDesc.StencilReadMask = compareMask;
    dssDesc.StencilWriteMask = writeMask;
    dssDesc.BackFace = dssDesc.FrontFace;
}

void PipelineStateInitializer::SetFillMode(D3D12_FILL_MODE fillMode)
{
    GetSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER>().FillMode = fillMode;
}

void PipelineStateInitializer::SetCullMode(D3D12_CULL_MODE cullMode)
{
    GetSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER>().CullMode = cullMode;
}

void PipelineStateInitializer::SetLineAntialias(bool lineAntialias)
{
    GetSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER>().AntialiasedLineEnable = lineAntialias;
}

void PipelineStateInitializer::SetDepthBias(int depthBias, float depthBiasClamp, float slopeScaledDepthBias)
{
    // final depth = depthFromInterpolation + (slopeScaledDepthBias * max(abs(dz/dx), abs(dz/dy))) + depthBias * r. (r is the 1/2^n, such as 1/16777216 for 24-bit)
    CD3DX12_RASTERIZER_DESC& rsDesc = GetSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER>();
    rsDesc.SlopeScaledDepthBias = slopeScaledDepthBias;
    rsDesc.DepthBias = depthBias;
    rsDesc.DepthBiasClamp = depthBiasClamp;
}

void PipelineStateInitializer::SetInputLayout(const VertexElementLayout& layout)
{
    D3D12_INPUT_LAYOUT_DESC& ilDesc = GetSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_INPUT_LAYOUT>();
    m_InputLayout = layout;
    ilDesc.NumElements = (uint32_t)m_InputLayout.GetDesc().size();
    ilDesc.pInputElementDescs = m_InputLayout.GetDesc().data();
}

void PipelineStateInitializer::SetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY_TYPE topology)
{
	GetSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PRIMITIVE_TOPOLOGY>() = topology;
}

void PipelineStateInitializer::SetRootSignature(ID3D12RootSignature* pRootSignature)
{
	GetSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_ROOT_SIGNATURE>() = pRootSignature;
}

void PipelineStateInitializer::SetVertexShader(Shader* pShader)
{
    m_Type = PipelineStateType::Graphics;
    m_Shaders[(int)ShaderType::Vertex] = pShader;
}

void PipelineStateInitializer::SetPixelShader(Shader* pShader)
{
	m_Shaders[(int)ShaderType::Pixel] = pShader;
}

void PipelineStateInitializer::SetHullShader(Shader* pShader)
{
	m_Type = PipelineStateType::Graphics;
	m_Shaders[(int)ShaderType::Hull] = pShader;
}

void PipelineStateInitializer::SetDomainShader(Shader* pShader)
{
	m_Type = PipelineStateType::Graphics;
	m_Shaders[(int)ShaderType::Domain] = pShader;
}

void PipelineStateInitializer::SetGeometryShader(Shader* pShader)
{
	m_Type = PipelineStateType::Graphics;
	m_Shaders[(int)ShaderType::Geometry] = pShader;
}

void PipelineStateInitializer::SetComputeShader(Shader* pShader)
{
	m_Type = PipelineStateType::Compute;
	m_Shaders[(int)ShaderType::Compute] = pShader;
}

void PipelineStateInitializer::SetMeshShader(Shader* pShader)
{
	m_Type = PipelineStateType::Mesh;
	m_Shaders[(int)ShaderType::Mesh] = pShader;
}

void PipelineStateInitializer::SetAmplificationShader(Shader* pShader)
{
	m_Type = PipelineStateType::Mesh;
	m_Shaders[(int)ShaderType::Amplification] = pShader;
}

D3D12_PIPELINE_STATE_STREAM_DESC PipelineStateInitializer::GetDesc()
{
	auto GetByteCode = [this](ShaderType type) -> D3D12_SHADER_BYTECODE& {
		switch (type)
		{
		case ShaderType::Vertex: return GetSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VS>();
		case ShaderType::Pixel: return GetSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS>();
		case ShaderType::Geometry: return GetSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_GS>();
		case ShaderType::Hull: return GetSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_HS>();
		case ShaderType::Domain: return GetSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DS>();
		case ShaderType::Mesh: return GetSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_MS>();
		case ShaderType::Amplification: return GetSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_AS>();
		case ShaderType::Compute: return GetSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_CS>();
		case ShaderType::MAX:
		default:
			noEntry();
			static D3D12_SHADER_BYTECODE dummy;
			return dummy;
		}
	};

	for (uint32_t i = 0; i < (int)ShaderType::MAX; i++)
	{
		Shader* pShader = m_Shaders[i];
		if (pShader)
		{
			GetByteCode((ShaderType)i) = D3D12_SHADER_BYTECODE{ pShader->GetByteCode(), pShader->GetByteCodeSize() };
		}
	}

    D3D12_PIPELINE_STATE_STREAM_DESC streamDesc = {
        .SizeInBytes = m_Size,
        .pPipelineStateSubobjectStream = m_pSubobjectData.data(),
    };

    return streamDesc;
}

PipelineState::PipelineState(ShaderManager* pShaderManager, GraphicsDevice* pParent) : GraphicsObject(pParent)
{
	m_ReloadHandle = pShaderManager->OnShaderRecompiledEvent().AddRaw(this, &PipelineState::OnShaderReloaded);
}

void PipelineState::Create(const PipelineStateInitializer& initializer)
{
    check(initializer.m_Type != PipelineStateType::MAX);
    ComPtr<ID3D12Device2> pDevice2;
    VERIFY_HR_EX(GetGraphics()->GetDevice()->QueryInterface(IID_PPV_ARGS(pDevice2.GetAddressOf())), GetGraphics()->GetDevice());

    m_Desc = initializer;

    D3D12_INPUT_LAYOUT_DESC& ilDesc = m_Desc.GetSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_INPUT_LAYOUT>();
    ilDesc.NumElements = (uint32_t)m_Desc.m_InputLayout.GetDesc().size();
    ilDesc.pInputElementDescs = m_Desc.m_InputLayout.GetDesc().data();

    D3D12_PIPELINE_STATE_STREAM_DESC streamDesc = m_Desc.GetDesc();
    VERIFY_HR_EX(pDevice2->CreatePipelineState(&streamDesc, IID_PPV_ARGS(m_pPipelineState.ReleaseAndGetAddressOf())), GetGraphics()->GetDevice());
    D3D::SetObjectName(m_pPipelineState.Get(), m_Desc.m_Name.c_str());
}

void PipelineState::ConditionallyReload()
{
	if (m_NeedsReload)
	{
		Create(m_Desc);
		m_NeedsReload = false;
		E_LOG(Info, "Reloaded Pipeline: %s", m_Desc.m_Name.c_str());
	}
}

void PipelineState::OnShaderReloaded(Shader* pOldShader, Shader* pNewShader)
{
	for (Shader*& pShader : m_Desc.m_Shaders)
	{
		if (pShader && pShader == pOldShader)
		{
			pShader = pNewShader;
			m_NeedsReload = true;
		}
	}
}
