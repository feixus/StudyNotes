#include "stdafx.h"
#include "RootSignature.h"

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
    D3D12_ROOT_PARAMETER1& rootParameter = m_RootParameters[rootIndex];
    rootParameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    rootParameter.Constants.Num32BitValues = constantCount;
    rootParameter.Constants.RegisterSpace = 0;
    rootParameter.Constants.ShaderRegister = shaderRegister;
    rootParameter.ShaderVisibility = visibility;
}

void RootSignature::SetConstantBufferView(uint32_t rootIndex, uint32_t shaderRegister, D3D12_SHADER_VISIBILITY visibility)
{
    SetSize(rootIndex + 1);
    D3D12_ROOT_PARAMETER1& rootParameter = m_RootParameters[rootIndex];
    rootParameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameter.Descriptor.Flags = D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC;
    rootParameter.Descriptor.ShaderRegister = shaderRegister;
    rootParameter.Descriptor.RegisterSpace = 0;
    rootParameter.ShaderVisibility = visibility;
}

void RootSignature::SetShaderResourceView(uint32_t rootIndex, uint32_t shaderRegister, D3D12_SHADER_VISIBILITY visibility)
{
    SetSize(rootIndex + 1);
    D3D12_ROOT_PARAMETER1& rootParameter = m_RootParameters[rootIndex];
    rootParameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    rootParameter.Descriptor.Flags = D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC;
    rootParameter.Descriptor.ShaderRegister = shaderRegister;
    rootParameter.Descriptor.RegisterSpace = 0;
    rootParameter.ShaderVisibility = visibility;
}

void RootSignature::SetUnorderedAccessView(uint32_t rootIndex, uint32_t shaderRegister, D3D12_SHADER_VISIBILITY visibility)
{
    SetSize(rootIndex + 1);
    D3D12_ROOT_PARAMETER1& rootParameter = m_RootParameters[rootIndex];
    rootParameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    rootParameter.Descriptor.Flags = D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC;
    rootParameter.Descriptor.ShaderRegister = shaderRegister;
    rootParameter.Descriptor.RegisterSpace = 0;
    rootParameter.ShaderVisibility = visibility;
}

void RootSignature::SetDescriptorTable(uint32_t rootIndex, uint32_t rangeCount, D3D12_SHADER_VISIBILITY visibility)
{
    SetSize(rootIndex + 1);
    D3D12_ROOT_PARAMETER1& rootParameter = m_RootParameters[rootIndex];
    rootParameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameter.DescriptorTable.NumDescriptorRanges = rangeCount;
    rootParameter.DescriptorTable.pDescriptorRanges = m_DescriptorTableRanges[rootIndex].data();
    rootParameter.ShaderVisibility = visibility;
}

void RootSignature::SetDescriptorTableRange(uint32_t rootIndex, uint32_t rangeIndex, uint32_t startshaderRegister, D3D12_DESCRIPTOR_RANGE_TYPE type, uint32_t count)
{
    assert(rangeIndex < MAX_NUM_DESCRIPTORS);
    D3D12_DESCRIPTOR_RANGE1& range = m_DescriptorTableRanges[rootIndex][rangeIndex];
    range.RangeType = type;
    range.NumDescriptors = count;
    range.BaseShaderRegister = startshaderRegister;
    range.RegisterSpace = 0;
    range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
    range.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_NONE;
}

void RootSignature::SetDescriptorTableSimple(uint32_t rootIndex, uint32_t startshaderRegister, D3D12_DESCRIPTOR_RANGE_TYPE type, uint32_t count, D3D12_SHADER_VISIBILITY visibility)
{
    SetDescriptorTable(rootIndex, 1, visibility);
    SetDescriptorTableRange(rootIndex, 0, startshaderRegister, type, count);
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
    CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC desc{};

    for (size_t i = 0; i < m_RootParameters.size(); i++)
    {
        D3D12_ROOT_PARAMETER1& rootParameter = m_RootParameters[i];
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

    desc.Init_1_1(m_NumParameters, m_RootParameters.data(), (uint32_t)m_StaticSamplers.size(), m_StaticSamplers.data(), flags);

    ComPtr<ID3DBlob> pDataBlob, pErrorBlob;
    HR(D3D12SerializeVersionedRootSignature(&desc, pDataBlob.GetAddressOf(), pErrorBlob.GetAddressOf()));
    HR(pDevice->CreateRootSignature(0, pDataBlob->GetBufferPointer(), pDataBlob->GetBufferSize(), IID_PPV_ARGS(m_pRootSignature.GetAddressOf())));
    SetD3DObjectName(m_pRootSignature.Get(), pName);
}
