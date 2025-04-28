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

	void AsShaderResourceView(uint32_t registersSlot, uint32_t registersState, D3D12_SHADER_VISIBILITY visibility)
	{
		Data.ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
		Data.Descriptor.Flags = D3D12_ROOT_DESCRIPTOR_FLAG_NONE;
		Data.Descriptor.ShaderRegister = registersSlot;
		Data.Descriptor.RegisterSpace = registersState;
		Data.ShaderVisibility = visibility;
	}

	void AsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE type, uint32_t shaderRegister, uint32_t count, D3D12_SHADER_VISIBILITY visibility = D3D12_SHADER_VISIBILITY_ALL)
	{
		AsDescriptorTable(1, visibility);
		SetTableRange(0, type, shaderRegister, count);
	}

	void AsDescriptorTable(uint32_t rangeCount, D3D12_SHADER_VISIBILITY visibility = D3D12_SHADER_VISIBILITY_ALL)
	{
		Data.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;  // this dont match will result in fatal error.
		Data.DescriptorTable.NumDescriptorRanges = rangeCount;
		Data.DescriptorTable.pDescriptorRanges = m_DescriptorTableRanges;
		Data.ShaderVisibility = visibility;
	}

	void SetTableRange(uint32_t rangeIndex, D3D12_DESCRIPTOR_RANGE_TYPE type, uint32_t shaderRegister, uint32_t count, uint32_t space = 0)
	{
		D3D12_DESCRIPTOR_RANGE1& range = m_DescriptorTableRanges[rangeIndex];
		range.RangeType = type;
		range.NumDescriptors = count;
		range.BaseShaderRegister = shaderRegister;
		range.RegisterSpace = space;
		range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
		range.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_NONE;
	}

	D3D12_ROOT_PARAMETER1 Data{};
	D3D12_DESCRIPTOR_RANGE1 m_DescriptorTableRanges[4];
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

	RootParameter& operator[](uint32_t i) { return m_RootParameters[i]; }
	const RootParameter& operator[](uint32_t i) const { return m_RootParameters[i]; }

	void Finalize(ID3D12Device* pDevice, D3D12_ROOT_SIGNATURE_FLAGS flags);

	ID3D12RootSignature* GetRootSignature() const { return m_pRootSignature.Get(); }

private:
	std::vector<RootParameter> m_RootParameters;
	std::vector<uint32_t> m_DescriptorTableSizes;
	std::vector<D3D12_STATIC_SAMPLER_DESC> m_StaticSamplers;
	ComPtr<ID3D12RootSignature> m_pRootSignature;

	BitField<16, uint32_t> m_DescriptorTableMask;
	BitField<16, uint32_t> m_SamplerMask;
	uint32_t m_NumParameters{ 0 };
};
