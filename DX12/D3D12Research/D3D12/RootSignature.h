#pragma once

struct RootParameter
{
	void AsConstantBufferView(uint32_t registersSlot, uint32_t registersState, D3D12_SHADER_VISIBILITY visibility)
	{
		Data.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
		Data.Descriptor.Flags = D3D12_ROOT_DESCRIPTOR_FLAG_NONE;
		Data.Descriptor.ShaderRegister = registersSlot;
		Data.Descriptor.RegisterSpace = registersState;
		Data.ShaderVisibility = visibility;
	}

	void AsShaderResourceView(D3D12_DESCRIPTOR_RANGE1& range, D3D12_SHADER_VISIBILITY visibility)
	{
		Data.ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
		Data.ShaderVisibility = visibility;
		Data.DescriptorTable.NumDescriptorRanges = range.NumDescriptors;
		Data.DescriptorTable.pDescriptorRanges = &range;
	}

	D3D12_ROOT_PARAMETER1 Data{};
};

class RootSignature
{
public:
	RootSignature(uint32_t numRootParameters)
	{
		m_NumParameters = numRootParameters;
		m_RootParameters.resize(numRootParameters);
		m_DescriptorTableSizes.resize(numRootParameters);
	}

	void AddStaticSampler(uint32_t slot, D3D12_SAMPLER_DESC samplerDesc, D3D12_SHADER_VISIBILITY visibility);

	CD3DX12_ROOT_PARAMETER1& operator[](uint32_t i) { return m_RootParameters[i]; }
	const CD3DX12_ROOT_PARAMETER1& operator[](uint32_t i) const { return m_RootParameters[i]; }

	void Finalize(ID3D12Device* pDevice, D3D12_ROOT_SIGNATURE_FLAGS flags);

	ID3D12RootSignature* GetRootSignature() const { return m_pRootSignature.Get(); }

private:
	std::vector<CD3DX12_ROOT_PARAMETER1> m_RootParameters;
	std::vector<uint32_t> m_DescriptorTableSizes;
	std::vector<D3D12_STATIC_SAMPLER_DESC> m_StaticSamplers;
	ComPtr<ID3D12RootSignature> m_pRootSignature;

	BitField<16, uint32_t> m_DescriptorTableMask;
	BitField<16, uint32_t> m_SamplerMask;
	uint32_t m_NumParameters{ 0 };
};
