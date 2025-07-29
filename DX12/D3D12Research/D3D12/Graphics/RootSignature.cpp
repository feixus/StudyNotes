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
		assert(size <= MAX_NUM_DESCRIPTORS);

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
    assert(rangeIndex < MAX_NUM_DESCRIPTORS);
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

void RootSignature::AddStaticSampler(uint32_t shaderRegister, D3D12_SAMPLER_DESC samplerDesc, D3D12_SHADER_VISIBILITY visibility)
{
    D3D12_STATIC_SAMPLER_DESC desc{};
    desc.Filter = samplerDesc.Filter;
    desc.AddressU = samplerDesc.AddressU;
    desc.AddressV = samplerDesc.AddressV;
    desc.AddressW = samplerDesc.AddressW;
    desc.MipLODBias = samplerDesc.MipLODBias;
    desc.MaxAnisotropy = samplerDesc.MaxAnisotropy;
    desc.ComparisonFunc = samplerDesc.ComparisonFunc;
    desc.BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
    desc.MinLOD = samplerDesc.MinLOD;
    desc.MaxLOD = samplerDesc.MaxLOD;
    desc.ShaderRegister = shaderRegister;
    desc.RegisterSpace = 0;
    desc.ShaderVisibility = visibility;

	if (desc.AddressU == D3D12_TEXTURE_ADDRESS_MODE_BORDER ||
        desc.AddressV == D3D12_TEXTURE_ADDRESS_MODE_BORDER ||
        desc.AddressW == D3D12_TEXTURE_ADDRESS_MODE_BORDER)
	{
        desc.BorderColor = samplerDesc.BorderColor[3] * samplerDesc.BorderColor[0] == 1.0f ? D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE : D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
	}
    
    m_StaticSamplers.push_back(desc);
}

void RootSignature::Finalize(const char* pName, ID3D12Device* pDevice, D3D12_ROOT_SIGNATURE_FLAGS flags)
{
    std::array<bool, (uint32_t)Shader::Type::MAX> shaderVisibility{};

    for (size_t i = 0; i < m_RootParameters.size(); i++)
    {
        D3D12_ROOT_PARAMETER& rootParameter = m_RootParameters[i];

        switch (rootParameter.ShaderVisibility)
        {
        case D3D12_SHADER_VISIBILITY_VERTEX:
            shaderVisibility[(uint32_t)Shader::Type::Vertex] = true;
            break;
        case D3D12_SHADER_VISIBILITY_PIXEL:
            shaderVisibility[(uint32_t)Shader::Type::Pixel] = true;
            break;
        case D3D12_SHADER_VISIBILITY_GEOMETRY:
            shaderVisibility[(uint32_t)Shader::Type::Geometry] = true;
            break;
        case D3D12_SHADER_VISIBILITY_ALL:
            for (bool& v : shaderVisibility)
            {
                v = true;
            }
            break;
        default:
        case D3D12_SHADER_VISIBILITY_DOMAIN:
        case D3D12_SHADER_VISIBILITY_HULL:
            assert(false);
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
                assert(false);
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
        if (shaderVisibility[(uint32_t)Shader::Type::Vertex] == false)
        {
            flags |= D3D12_ROOT_SIGNATURE_FLAG_DENY_VERTEX_SHADER_ROOT_ACCESS;
        }

        if (shaderVisibility[(uint32_t)Shader::Type::Pixel] == false)
        {
            flags |= D3D12_ROOT_SIGNATURE_FLAG_DENY_PIXEL_SHADER_ROOT_ACCESS;
        }

        if (shaderVisibility[(uint32_t)Shader::Type::Geometry] == false)
        {
            flags |= D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;
        }

        //#todo tesellation not supported yet
        flags |= D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS;
        flags |= D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS;
    }

    constexpr uint32_t recommendedDwords = 12;
    uint32_t dwords = GetDWordSize();
    if (dwords > recommendedDwords)
    {
        E_LOG(LogType::Warning, "[RootSignature::Finalize] RootSignature '%s' uses %d DWORDs while under %d is recommended", pName, dwords, recommendedDwords);
    }

    CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC desc{};
	//desc.Init_1_1(m_NumParameters, m_RootParameters.data(), (uint32_t)m_StaticSamplers.size(), m_StaticSamplers.data(), flags);
	desc.Init_1_0(m_NumParameters, m_RootParameters.data(), (uint32_t)m_StaticSamplers.size(), m_StaticSamplers.data(), flags);

    ComPtr<ID3DBlob> pDataBlob, pErrorBlob;
    D3D12SerializeVersionedRootSignature(&desc, pDataBlob.GetAddressOf(), pErrorBlob.GetAddressOf());
    if (pErrorBlob)
    {
        const char* pError = (char*)pErrorBlob->GetBufferPointer();
        E_LOG(LogType::Error, "RootSignature::Finalize serialization error: %s", pError);
        return;
    }
    HR(pDevice->CreateRootSignature(0, pDataBlob->GetBufferPointer(), pDataBlob->GetBufferSize(), IID_PPV_ARGS(m_pRootSignature.GetAddressOf())));
    SetD3DObjectName(m_pRootSignature.Get(), pName);
}

void RootSignature::FinalizeFromShader(const char* pName, const Shader& shader, ID3D12Device* pDevice)
{
    ComPtr<ID3D12VersionedRootSignatureDeserializer> pDeserializer;
    HR(D3D12CreateVersionedRootSignatureDeserializer(shader.GetByteCode(), shader.GetByteCodeSize(), IID_PPV_ARGS(pDeserializer.GetAddressOf())));

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
