#include "stdafx.h"
#include "RootSignature.h"
#include "Shader.h"
#include "Graphics.h"

RootSignature::RootSignature(GraphicsDevice* pParent)
    : GraphicsObject(pParent), m_NumParameters(0)
{}

void RootSignature::SetRootConstants(uint32_t rootIndex, uint32_t shaderRegister, uint32_t constantCount, D3D12_SHADER_VISIBILITY visibility)
{
    Get(rootIndex).InitAsConstants(constantCount, shaderRegister, 0u, visibility);
}

void RootSignature::SetConstantBufferView(uint32_t rootIndex, uint32_t shaderRegister, D3D12_SHADER_VISIBILITY visibility)
{
    Get(rootIndex).InitAsConstantBufferView(shaderRegister, 0u, visibility);
}

void RootSignature::SetShaderResourceView(uint32_t rootIndex, uint32_t shaderRegister, D3D12_SHADER_VISIBILITY visibility)
{
    Get(rootIndex).InitAsShaderResourceView(shaderRegister, 0u, visibility);
}

void RootSignature::SetUnorderedAccessView(uint32_t rootIndex, uint32_t shaderRegister, D3D12_SHADER_VISIBILITY visibility)
{
    Get(rootIndex).InitAsUnorderedAccessView(shaderRegister, 0u, visibility);
}

void RootSignature::SetDescriptorTable(uint32_t rootIndex, uint32_t rangeCount, D3D12_SHADER_VISIBILITY visibility)
{
    Get(rootIndex).InitAsDescriptorTable(rangeCount, m_DescriptorTableRanges[rootIndex].data(), visibility);
}

void RootSignature::SetDescriptorTableRange(uint32_t rootIndex, uint32_t rangeIndex, uint32_t startshaderRegister, uint32_t space, D3D12_DESCRIPTOR_RANGE_TYPE type, uint32_t count, uint32_t heapSlotOffset)
{
    GetRange(rootIndex, rangeIndex).Init(type, count, startshaderRegister, space, heapSlotOffset);
}

void RootSignature::SetDescriptorTableSimple(uint32_t rootIndex, uint32_t startshaderRegister, D3D12_DESCRIPTOR_RANGE_TYPE type, uint32_t count, D3D12_SHADER_VISIBILITY visibility)
{
    SetDescriptorTable(rootIndex, 1, visibility);
    SetDescriptorTableRange(rootIndex, 0, startshaderRegister, 0, type, count, D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND);
}

void RootSignature::AddStaticSampler(const D3D12_STATIC_SAMPLER_DESC& samplerDesc)
{
    m_StaticSamplers.push_back(CD3DX12_STATIC_SAMPLER_DESC(samplerDesc));
}

void RootSignature::Finalize(const char* pName, D3D12_ROOT_SIGNATURE_FLAGS flags)
{
    AddDefaultParameters();

    D3D12_ROOT_SIGNATURE_FLAGS visibilityFlags =
        D3D12_ROOT_SIGNATURE_FLAG_DENY_VERTEX_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_PIXEL_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS;

    D3D12_FEATURE_DATA_D3D12_OPTIONS7 featuresCaps{};
    GetGraphics()->GetDevice()->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS7, &featuresCaps, sizeof(D3D12_FEATURE_DATA_D3D12_OPTIONS7));
    if (featuresCaps.MeshShaderTier != D3D12_MESH_SHADER_TIER_NOT_SUPPORTED)
    {
        visibilityFlags |= D3D12_ROOT_SIGNATURE_FLAG_DENY_MESH_SHADER_ROOT_ACCESS | 
                           D3D12_ROOT_SIGNATURE_FLAG_DENY_AMPLIFICATION_SHADER_ROOT_ACCESS;
    }

    for (size_t i = 0; i < m_NumParameters; i++)
    {
        D3D12_ROOT_PARAMETER& rootParameter = m_RootParameters[i];

        switch (rootParameter.ShaderVisibility)
        {
        case D3D12_SHADER_VISIBILITY_VERTEX:
            visibilityFlags = visibilityFlags & ~D3D12_ROOT_SIGNATURE_FLAG_DENY_VERTEX_SHADER_ROOT_ACCESS;
            break;
        case D3D12_SHADER_VISIBILITY_PIXEL:
            visibilityFlags = visibilityFlags & ~D3D12_ROOT_SIGNATURE_FLAG_DENY_PIXEL_SHADER_ROOT_ACCESS;
            break;
        case D3D12_SHADER_VISIBILITY_GEOMETRY:
            visibilityFlags = visibilityFlags & ~D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;
            break;
        case D3D12_SHADER_VISIBILITY_MESH:
            visibilityFlags = visibilityFlags & ~D3D12_ROOT_SIGNATURE_FLAG_DENY_MESH_SHADER_ROOT_ACCESS;
            break;
		case D3D12_SHADER_VISIBILITY_AMPLIFICATION:
			visibilityFlags = visibilityFlags & ~D3D12_ROOT_SIGNATURE_FLAG_DENY_AMPLIFICATION_SHADER_ROOT_ACCESS;
			break;
        case D3D12_SHADER_VISIBILITY_ALL:
            visibilityFlags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
            break;
        default:
            noEntry();
            break;
        }

		if (rootParameter.ParameterType == D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE)
		{
            rootParameter.DescriptorTable.pDescriptorRanges = m_DescriptorTableRanges[i].data();
            switch (rootParameter.DescriptorTable.pDescriptorRanges->RangeType)
            {
			case D3D12_DESCRIPTOR_RANGE_TYPE_SRV:
			case D3D12_DESCRIPTOR_RANGE_TYPE_UAV:
			case D3D12_DESCRIPTOR_RANGE_TYPE_CBV:
                m_DescriptorTableMask.SetBit((uint32_t)i);
                break;
            case D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER:
                m_SamplerMask.SetBit((uint32_t)i);
                break;
            default:
                noEntry();
                break;
            }

            for (uint32_t j = 0; j < rootParameter.DescriptorTable.NumDescriptorRanges; j++)
            {
                const D3D12_DESCRIPTOR_RANGE& range = rootParameter.DescriptorTable.pDescriptorRanges[j];
                m_DescriptorTableSizes[i] = range.NumDescriptors;
            }
		}
    }

    // it's illegal to have RS flags if it's a local root signature
    if ((flags & D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE) == 0)
    {
        flags |= visibilityFlags;
		//flags |= D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED;
		//flags |= D3D12_ROOT_SIGNATURE_FLAG_SAMPLER_HEAP_DIRECTLY_INDEXED;
    }

    constexpr uint32_t recommendedDwords = 12;
    uint32_t dwords = GetDWordSize();
    if (dwords > recommendedDwords)
    {
        E_LOG(Warning, "[RootSignature::Finalize] RootSignature '%s' uses %d DWORDs while under %d is recommended", pName, dwords, recommendedDwords);
    }

    CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC desc{};
	//desc.Init_1_1(m_NumParameters, m_RootParameters.data(), (uint32_t)m_StaticSamplers.size(), m_StaticSamplers.data(), flags);
	desc.Init_1_0(m_NumParameters, m_RootParameters.data(), (uint32_t)m_StaticSamplers.size(), m_StaticSamplers.data(), flags);

    ComPtr<ID3DBlob> pDataBlob, pErrorBlob;
    D3D12SerializeVersionedRootSignature(&desc, pDataBlob.GetAddressOf(), pErrorBlob.GetAddressOf());
    if (pErrorBlob)
    {
        const char* pError = (char*)pErrorBlob->GetBufferPointer();
        E_LOG(Error, "RootSignature::Finalize serialization error: %s", pError);
        return;
    }
    VERIFY_HR_EX(GetGraphics()->GetDevice()->CreateRootSignature(0, pDataBlob->GetBufferPointer(), pDataBlob->GetBufferSize(), IID_PPV_ARGS(m_pRootSignature.ReleaseAndGetAddressOf())), GetGraphics()->GetDevice());
    D3D_SETNAME(m_pRootSignature.Get(), pName);
}

void RootSignature::FinalizeFromShader(const char* pName, const ShaderBase* pShader)
{
    ComPtr<ID3D12VersionedRootSignatureDeserializer> pDeserializer;
    VERIFY_HR_EX(D3D12CreateVersionedRootSignatureDeserializer(pShader->GetByteCode(), pShader->GetByteCodeSize(), IID_PPV_ARGS(pDeserializer.GetAddressOf())), GetGraphics()->GetDevice());

    const D3D12_VERSIONED_ROOT_SIGNATURE_DESC* pDesc;
    pDeserializer->GetRootSignatureDescAtVersion(D3D_ROOT_SIGNATURE_VERSION_1_0, &pDesc);
    const D3D12_ROOT_SIGNATURE_DESC& rsDesc = pDesc->Desc_1_0;

	m_NumParameters = rsDesc.NumParameters;
	m_StaticSamplers.resize(rsDesc.NumStaticSamplers);

    memcpy(m_StaticSamplers.data(), rsDesc.pStaticSamplers, m_StaticSamplers.size() * sizeof(D3D12_STATIC_SAMPLER_DESC));
    memcpy(m_RootParameters.data(), rsDesc.pParameters, m_NumParameters * sizeof(D3D12_ROOT_PARAMETER));

    for (uint32_t i = 0; i < m_NumParameters; i++)
    {
        const D3D12_ROOT_PARAMETER& rootParameter = rsDesc.pParameters[i];
        if (rootParameter.ParameterType == D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE)
        {
			m_DescriptorTableRanges[i].resize(rootParameter.DescriptorTable.NumDescriptorRanges);
            memcpy(m_DescriptorTableRanges[i].data(), rootParameter.DescriptorTable.pDescriptorRanges, rootParameter.DescriptorTable.NumDescriptorRanges * sizeof(D3D12_DESCRIPTOR_RANGE));
        }
    }

    Finalize(pName, rsDesc.Flags);
}

uint32_t RootSignature::GetDWordSize() const
{
    uint32_t count = 0;
    for (size_t i = 0; i < m_NumParameters; i++)
    {
        const D3D12_ROOT_PARAMETER& rootParameter = m_RootParameters[i];
        switch (rootParameter.ParameterType)
        {
        case D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS:
            count += rootParameter.Constants.Num32BitValues;
            break;
        case D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE:
            count += 1;
            break;
        case D3D12_ROOT_PARAMETER_TYPE_CBV:
        case D3D12_ROOT_PARAMETER_TYPE_SRV:
        case D3D12_ROOT_PARAMETER_TYPE_UAV:
            count += 2;
            break;
        }
    }
    return count;
}

void RootSignature::AddDefaultParameters()
{
#define CODE_ROOT_SIG 0
#if CODE_ROOT_SIG
    int staticSamplerRegisterSlot = 10;
    AddStaticSampler(CD3DX12_STATIC_SAMPLER_DESC(staticSamplerRegisterSlot++, D3D12_FILTER_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_WRAP, D3D12_TEXTURE_ADDRESS_MODE_WRAP, D3D12_TEXTURE_ADDRESS_MODE_WRAP));
    AddStaticSampler(CD3DX12_STATIC_SAMPLER_DESC(staticSamplerRegisterSlot++, D3D12_FILTER_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP));
    AddStaticSampler(CD3DX12_STATIC_SAMPLER_DESC(staticSamplerRegisterSlot++, D3D12_FILTER_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_BORDER, D3D12_TEXTURE_ADDRESS_MODE_BORDER, D3D12_TEXTURE_ADDRESS_MODE_BORDER));
    
    AddStaticSampler(CD3DX12_STATIC_SAMPLER_DESC(staticSamplerRegisterSlot++, D3D12_FILTER_MIN_MAG_MIP_POINT, D3D12_TEXTURE_ADDRESS_MODE_WRAP, D3D12_TEXTURE_ADDRESS_MODE_WRAP, D3D12_TEXTURE_ADDRESS_MODE_WRAP));
    AddStaticSampler(CD3DX12_STATIC_SAMPLER_DESC(staticSamplerRegisterSlot++, D3D12_FILTER_MIN_MAG_MIP_POINT, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP));
    AddStaticSampler(CD3DX12_STATIC_SAMPLER_DESC(staticSamplerRegisterSlot++, D3D12_FILTER_MIN_MAG_MIP_POINT, D3D12_TEXTURE_ADDRESS_MODE_BORDER, D3D12_TEXTURE_ADDRESS_MODE_BORDER, D3D12_TEXTURE_ADDRESS_MODE_BORDER));
    
    AddStaticSampler(CD3DX12_STATIC_SAMPLER_DESC(staticSamplerRegisterSlot++, D3D12_FILTER_ANISOTROPIC, D3D12_TEXTURE_ADDRESS_MODE_WRAP, D3D12_TEXTURE_ADDRESS_MODE_WRAP, D3D12_TEXTURE_ADDRESS_MODE_WRAP));
    AddStaticSampler(CD3DX12_STATIC_SAMPLER_DESC(staticSamplerRegisterSlot++, D3D12_FILTER_ANISOTROPIC, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP));
    AddStaticSampler(CD3DX12_STATIC_SAMPLER_DESC(staticSamplerRegisterSlot++, D3D12_FILTER_ANISOTROPIC, D3D12_TEXTURE_ADDRESS_MODE_BORDER, D3D12_TEXTURE_ADDRESS_MODE_BORDER, D3D12_TEXTURE_ADDRESS_MODE_BORDER));
    
    AddStaticSampler(CD3DX12_STATIC_SAMPLER_DESC(staticSamplerRegisterSlot++, D3D12_FILTER_ANISOTROPIC, D3D12_TEXTURE_ADDRESS_MODE_WRAP, D3D12_TEXTURE_ADDRESS_MODE_WRAP, D3D12_TEXTURE_ADDRESS_MODE_WRAP));
	AddStaticSampler(CD3DX12_STATIC_SAMPLER_DESC(staticSamplerRegisterSlot++, D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT, D3D12_TEXTURE_ADDRESS_MODE_WRAP, D3D12_TEXTURE_ADDRESS_MODE_WRAP, D3D12_TEXTURE_ADDRESS_MODE_WRAP, 0.0f, 16u, D3D12_COMPARISON_FUNC_GREATER));

    uint32_t numSRVRanges = 5;
    uint32_t numUAVRanges = 5;
    uint32_t currentRangeIndex = 0;
    m_BindlessViewsIndex = m_NumParameters;
    SetDescriptorTable(m_BindlessViewsIndex, numSRVRanges + numUAVRanges, D3D12_SHADER_VISIBILITY_ALL);
    for (uint32_t i = 0; i < numSRVRanges; i++)
    {
        SetDescriptorTableRange(m_BindlessViewsIndex, currentRangeIndex, 0, currentRangeIndex + 100, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0xFFFFFFFF, 0);
        currentRangeIndex++;
    }

    for (uint32_t i = 0; i < numUAVRanges; i++)
    {
        SetDescriptorTableRange(m_BindlessViewsIndex, currentRangeIndex, 0, currentRangeIndex + 100, D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 0xFFFFFFFF, 0);
        currentRangeIndex++;
    }

    m_BindlessSamplerIndex = m_NumParameters;
    SetDescriptorTable(m_BindlessSamplerIndex, 1, D3D12_SHADER_VISIBILITY_ALL);
    SetDescriptorTableRange(m_BindlessSamplerIndex, 0, 0, 100, D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, 0xFFFFFFFF, 0);
#else
    m_BindlessViewsIndex = m_NumParameters - 2;
    m_BindlessSamplerIndex = m_NumParameters - 1;
#endif
}
