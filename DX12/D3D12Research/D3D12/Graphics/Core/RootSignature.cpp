#include "stdafx.h"
#include "RootSignature.h"
#include "Shader.h"

RootSignature::RootSignature()
    : m_NumParameters(0)
{
}

void RootSignature::SetSize(uint32_t size, bool shrink /*= true*/)
{
    if (size != m_NumParameters && (shrink || size > m_NumParameters))
    {
		check(size <= MAX_NUM_DESCRIPTORS);

        m_NumParameters = size;
        m_RootParameters.resize(size);
        m_DescriptorTableSizes.resize(size);
        m_DescriptorTableRanges.resize(size);
    }
}

void RootSignature::SetRootConstants(uint32_t rootIndex, uint32_t shaderRegister, uint32_t constantCount, D3D12_SHADER_VISIBILITY visibility)
{
    SetSize(rootIndex + 1);
    D3D12_ROOT_PARAMETER& rootParameter = m_RootParameters[rootIndex];
    rootParameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    rootParameter.Constants.Num32BitValues = constantCount;
    rootParameter.Constants.RegisterSpace = 0;
    rootParameter.Constants.ShaderRegister = shaderRegister;
    rootParameter.ShaderVisibility = visibility;
}

void RootSignature::SetConstantBufferView(uint32_t rootIndex, uint32_t shaderRegister, D3D12_SHADER_VISIBILITY visibility)
{
    SetSize(rootIndex + 1);
    D3D12_ROOT_PARAMETER& rootParameter = m_RootParameters[rootIndex];
    rootParameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    //rootParameter.Descriptor.Flags = D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC;
    rootParameter.Descriptor.ShaderRegister = shaderRegister;
    rootParameter.Descriptor.RegisterSpace = 0;
    rootParameter.ShaderVisibility = visibility;
}

void RootSignature::SetShaderResourceView(uint32_t rootIndex, uint32_t shaderRegister, D3D12_SHADER_VISIBILITY visibility)
{
    SetSize(rootIndex + 1);
    D3D12_ROOT_PARAMETER& rootParameter = m_RootParameters[rootIndex];
    rootParameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    //rootParameter.Descriptor.Flags = D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC;
    rootParameter.Descriptor.ShaderRegister = shaderRegister;
    rootParameter.Descriptor.RegisterSpace = 0;
    rootParameter.ShaderVisibility = visibility;
}

void RootSignature::SetUnorderedAccessView(uint32_t rootIndex, uint32_t shaderRegister, D3D12_SHADER_VISIBILITY visibility)
{
    SetSize(rootIndex + 1);
    D3D12_ROOT_PARAMETER& rootParameter = m_RootParameters[rootIndex];
    rootParameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    //rootParameter.Descriptor.Flags = D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC;
    rootParameter.Descriptor.ShaderRegister = shaderRegister;
    rootParameter.Descriptor.RegisterSpace = 0;
    rootParameter.ShaderVisibility = visibility;
}

void RootSignature::SetDescriptorTable(uint32_t rootIndex, uint32_t rangeCount, D3D12_SHADER_VISIBILITY visibility)
{
    SetSize(rootIndex + 1);
    D3D12_ROOT_PARAMETER& rootParameter = m_RootParameters[rootIndex];
    rootParameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameter.DescriptorTable.NumDescriptorRanges = rangeCount;
    rootParameter.DescriptorTable.pDescriptorRanges = m_DescriptorTableRanges[rootIndex].data();
    rootParameter.ShaderVisibility = visibility;
}

void RootSignature::SetDescriptorTableRange(uint32_t rootIndex, uint32_t rangeIndex, uint32_t startshaderRegister, D3D12_DESCRIPTOR_RANGE_TYPE type, uint32_t count, uint32_t heapSlotOffset)
{
    check(rangeIndex < MAX_NUM_DESCRIPTORS);
    D3D12_DESCRIPTOR_RANGE& range = m_DescriptorTableRanges[rootIndex][rangeIndex];
    range.RangeType = type;
    range.NumDescriptors = count;
    range.BaseShaderRegister = startshaderRegister;
    range.RegisterSpace = 0;
    range.OffsetInDescriptorsFromTableStart = heapSlotOffset;
    //range.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE;
}

void RootSignature::SetDescriptorTableSimple(uint32_t rootIndex, uint32_t startshaderRegister, D3D12_DESCRIPTOR_RANGE_TYPE type, uint32_t count, D3D12_SHADER_VISIBILITY visibility)
{
    SetDescriptorTable(rootIndex, 1, visibility);
    SetDescriptorTableRange(rootIndex, 0, startshaderRegister, type, count, D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND);
}

void RootSignature::AddStaticSampler(uint32_t shaderRegister, const D3D12_STATIC_SAMPLER_DESC& samplerDesc, D3D12_SHADER_VISIBILITY visibility)
{
    m_StaticSamplers.push_back(samplerDesc);
}

void RootSignature::Finalize(const char* pName, ID3D12Device* pDevice, D3D12_ROOT_SIGNATURE_FLAGS flags)
{
    D3D12_ROOT_SIGNATURE_FLAGS visibilityFlags =
        D3D12_ROOT_SIGNATURE_FLAG_DENY_VERTEX_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_PIXEL_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS;

    D3D12_FEATURE_DATA_D3D12_OPTIONS7 featuresCaps{};
    pDevice->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS7, &featuresCaps, sizeof(D3D12_FEATURE_DATA_D3D12_OPTIONS7));
    if (featuresCaps.MeshShaderTier != D3D12_MESH_SHADER_TIER_NOT_SUPPORTED)
    {
        visibilityFlags |= D3D12_ROOT_SIGNATURE_FLAG_DENY_MESH_SHADER_ROOT_ACCESS | 
                           D3D12_ROOT_SIGNATURE_FLAG_DENY_AMPLIFICATION_SHADER_ROOT_ACCESS;
    }

    for (size_t i = 0; i < m_RootParameters.size(); i++)
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
                m_DescriptorTableSizes[i] += rootParameter.DescriptorTable.pDescriptorRanges[j].NumDescriptors;
            }
		}
    }

    // it's illegal to have RS flags if it's a local root signature
    if ((flags & D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE) == 0)
    {
        flags |= visibilityFlags;
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
    VERIFY_HR_EX(pDevice->CreateRootSignature(0, pDataBlob->GetBufferPointer(), pDataBlob->GetBufferSize(), IID_PPV_ARGS(m_pRootSignature.GetAddressOf())), pDevice);
    D3D_SETNAME(m_pRootSignature.Get(), pName);
}

void RootSignature::FinalizeFromShader(const char* pName, const Shader& shader, ID3D12Device* pDevice)
{
    ComPtr<ID3D12VersionedRootSignatureDeserializer> pDeserializer;
    VERIFY_HR_EX(D3D12CreateVersionedRootSignatureDeserializer(shader.GetByteCode(), shader.GetByteCodeSize(), IID_PPV_ARGS(pDeserializer.GetAddressOf())), pDevice);

    const D3D12_VERSIONED_ROOT_SIGNATURE_DESC* pDesc = pDeserializer->GetUnconvertedRootSignatureDesc();

	m_NumParameters = pDesc->Desc_1_0.NumParameters;
    m_DescriptorTableRanges.resize(m_NumParameters);
    m_DescriptorTableSizes.resize(m_NumParameters);
    m_RootParameters.resize(m_NumParameters);
	m_StaticSamplers.resize(pDesc->Desc_1_0.NumStaticSamplers);

    memcpy(m_StaticSamplers.data(), pDesc->Desc_1_0.pStaticSamplers, m_StaticSamplers.size() * sizeof(D3D12_STATIC_SAMPLER_DESC));
    memcpy(m_RootParameters.data(), pDesc->Desc_1_0.pParameters, m_RootParameters.size() * sizeof(D3D12_ROOT_PARAMETER));

    for (uint32_t i = 0; i < m_NumParameters; i++)
    {
        const D3D12_ROOT_PARAMETER& rootParameter = pDesc->Desc_1_0.pParameters[i];
        if (rootParameter.ParameterType == D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE)
        {
            memcpy(m_DescriptorTableRanges[i].data(), rootParameter.DescriptorTable.pDescriptorRanges, rootParameter.DescriptorTable.NumDescriptorRanges * sizeof(D3D12_DESCRIPTOR_RANGE));
        }
    }

    Finalize(pName, pDevice, pDesc->Desc_1_0.Flags);
}

uint32_t RootSignature::GetDWordSize() const
{
    uint32_t count = 0;
    for (size_t i = 0; i < m_RootParameters.size(); i++)
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
