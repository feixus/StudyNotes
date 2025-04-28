#include "stdafx.h"
#include "RootSignature.h"

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

   /* for (size_t i = 0; i < m_RootParameters.size(); i++)
    {
        const RootParameter& rootParameter = m_RootParameters[i];
		if (rootParameter.Data.ParameterType == D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE)
		{
            if (rootParameter.Data.DescriptorTable.pDescriptorRanges->RangeType == D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER)
            {
			    m_DescriptorTableMask.SetBit(i);
            }
            else
            {
                m_SamplerMask.SetBit(1);
                m_DescriptorTableSizes[i] = rootParameter.Data.DescriptorTable.NumDescriptorRanges;
            }
		}
		else if (rootParameter.Data.ParameterType == D3D12_ROOT_PARAMETER_TYPE_CBV)
		{
			m_DescriptorTableMask.SetBit(i);
		}
    }*/

    /*std::vector<D3D12_ROOT_PARAMETER1> parameters(m_RootParameters.size());
    for (size_t i = 0; i < parameters.size(); i++)
    {
        parameters[i] = m_RootParameters[i].Data;
    }*/
    desc.Init_1_1(m_NumParameters, reinterpret_cast<D3D12_ROOT_PARAMETER1*>(m_RootParameters.data()), (uint32_t)m_StaticSamplers.size(), m_StaticSamplers.data(), flags);

    ComPtr<ID3DBlob> pDataBlob, pErrorBlob;
    HR(D3D12SerializeVersionedRootSignature(&desc, pDataBlob.GetAddressOf(), pErrorBlob.GetAddressOf()));
    HR(pDevice->CreateRootSignature(0, pDataBlob->GetBufferPointer(), pDataBlob->GetBufferSize(), IID_PPV_ARGS(m_pRootSignature.GetAddressOf())));
}
