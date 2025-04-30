#include "stdafx.h"
#include "RootSignature.h"

RootSignature::RootSignature(uint32_t numRootParameters)
{
    assert(numRootParameters <= MAX_NUM_DESCRIPTORS);

    m_NumParameters = numRootParameters;
	m_RootParameters.resize(numRootParameters);
    m_DescriptorTableSizes.resize(numRootParameters);
	m_DescriptorTableRanges.resize(numRootParameters);
}

void RootSignature::SetConstantBufferView(uint32_t rootIndex, uint32_t registerSlot, D3D12_SHADER_VISIBILITY visibility)
{
    D3D12_ROOT_PARAMETER1& rootParameter = m_RootParameters[rootIndex];
    rootParameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameter.Descriptor.Flags = D3D12_ROOT_DESCRIPTOR_FLAG_NONE;
    rootParameter.Descriptor.ShaderRegister = registerSlot;
    rootParameter.Descriptor.RegisterSpace = 0;
    rootParameter.ShaderVisibility = visibility;
}

void RootSignature::SetShaderResourceView(uint32_t rootIndex, uint32_t registerSlot, D3D12_SHADER_VISIBILITY visibility)
{
    D3D12_ROOT_PARAMETER1& rootParameter = m_RootParameters[rootIndex];
    rootParameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    rootParameter.Descriptor.Flags = D3D12_ROOT_DESCRIPTOR_FLAG_NONE;
    rootParameter.Descriptor.ShaderRegister = registerSlot;
    rootParameter.Descriptor.RegisterSpace = 0;
    rootParameter.ShaderVisibility = visibility;
}

void RootSignature::SetDescriptorTable(uint32_t rootIndex, uint32_t rangeCount, D3D12_SHADER_VISIBILITY visibility)
{
    D3D12_ROOT_PARAMETER1& rootParameter = m_RootParameters[rootIndex];
    rootParameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameter.DescriptorTable.NumDescriptorRanges = rangeCount;
    rootParameter.DescriptorTable.pDescriptorRanges = m_DescriptorTableRanges[rootIndex].data();
    rootParameter.ShaderVisibility = visibility;
}

void RootSignature::SetDescriptorTableRange(uint32_t rootIndex, uint32_t rangeIndex, uint32_t startRegisterSlot, D3D12_DESCRIPTOR_RANGE_TYPE type, uint32_t count)
{
    D3D12_DESCRIPTOR_RANGE1& range = m_DescriptorTableRanges[rootIndex][rangeIndex];
    range.RangeType = type;
    range.NumDescriptors = count;
    range.BaseShaderRegister = startRegisterSlot;
    range.RegisterSpace = 0;
    range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
    range.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE;
}

void RootSignature::SetDescriptorTableSimple(uint32_t rootIndex, uint32_t startRegisterSlot, D3D12_DESCRIPTOR_RANGE_TYPE type, uint32_t count, D3D12_SHADER_VISIBILITY visibility)
{
    SetDescriptorTable(rootIndex, 1, visibility);
    SetDescriptorTableRange(rootIndex, 0, startRegisterSlot, type, count);
}

void RootSignature::AddStaticSampler(uint32_t slot, D3D12_SAMPLER_DESC samplerDesc, D3D12_SHADER_VISIBILITY visibility)
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
    desc.ShaderRegister = slot;
    desc.RegisterSpace = 0;
    desc.ShaderVisibility = visibility;

	if (desc.AddressU == D3D12_TEXTURE_ADDRESS_MODE_BORDER ||
        desc.AddressV == D3D12_TEXTURE_ADDRESS_MODE_BORDER ||
        desc.AddressW == D3D12_TEXTURE_ADDRESS_MODE_BORDER)
	{
        if (samplerDesc.BorderColor[3] == 1.0f)
        {
            if (samplerDesc.BorderColor[0] == 1.0f)
            {
			    desc.BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
            }
            else
            {
                desc.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
            }
		}
        else
        {
            desc.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
        }
	}
    
    m_StaticSamplers.push_back(desc);
}

void RootSignature::Finalize(ID3D12Device* pDevice, D3D12_ROOT_SIGNATURE_FLAGS flags)
{
    CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC desc{};

    for (size_t i = 0; i < m_RootParameters.size(); i++)
    {
        const D3D12_ROOT_PARAMETER1& rootParameter = m_RootParameters[i];
		if (rootParameter.ParameterType == D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE)
		{
            if (rootParameter.DescriptorTable.pDescriptorRanges->RangeType == D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER)
            {
                m_SamplerMask.SetBit((uint32_t)i);
            }
            else
            {
			    m_DescriptorTableMask.SetBit(1);
                m_DescriptorTableSizes[i] = rootParameter.DescriptorTable.NumDescriptorRanges;
            }
		}
		else
		{
		}
    }

    desc.Init_1_1(m_NumParameters, m_RootParameters.data(), (uint32_t)m_StaticSamplers.size(), m_StaticSamplers.data(), flags);

    ComPtr<ID3DBlob> pDataBlob, pErrorBlob;
    HR(D3D12SerializeVersionedRootSignature(&desc, pDataBlob.GetAddressOf(), pErrorBlob.GetAddressOf()));
    HR(pDevice->CreateRootSignature(0, pDataBlob->GetBufferPointer(), pDataBlob->GetBufferSize(), IID_PPV_ARGS(m_pRootSignature.GetAddressOf())));
}
