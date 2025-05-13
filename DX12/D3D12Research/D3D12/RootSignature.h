#pragma once

class RootSignature
{
public:

	static const int MAX_NUM_DESCRIPTORS = 16;
	static const int MAX_RANGES_PER_TABLE = 2;
	using RootSignatureDescriptorMask = BitField32;
	static_assert(MAX_NUM_DESCRIPTORS <= BitField32::Capacity(), "Descriptor bitfield is not large enough");

	RootSignature(uint32_t numRootParameters);

	void SetRootConstants(uint32_t rootIndex, uint32_t registerSlot, uint32_t constantCount, D3D12_SHADER_VISIBILITY visibility);
	void SetConstantBufferView(uint32_t rootIndex, uint32_t registerSlot, D3D12_SHADER_VISIBILITY visibility);
	void SetShaderResourceView(uint32_t rootIndex, uint32_t registerSlot, D3D12_SHADER_VISIBILITY visibility);
	void SetUnorderedAccessView(uint32_t rootIndex, uint32_t registerSlot, D3D12_SHADER_VISIBILITY visibility);
	void SetDescriptorTable(uint32_t rootIndex, uint32_t rangeCount, D3D12_SHADER_VISIBILITY visibility);
	void SetDescriptorTableRange(uint32_t rootIndex, uint32_t rangeIndex, uint32_t startRegisterSlot, D3D12_DESCRIPTOR_RANGE_TYPE type, uint32_t count);
	void SetDescriptorTableSimple(uint32_t rootIndex, uint32_t startRegisterSlot, D3D12_DESCRIPTOR_RANGE_TYPE type, uint32_t count, D3D12_SHADER_VISIBILITY visibility);
	
	void AddStaticSampler(uint32_t slot, D3D12_SAMPLER_DESC samplerDesc, D3D12_SHADER_VISIBILITY visibility);

	void Finalize(ID3D12Device* pDevice, D3D12_ROOT_SIGNATURE_FLAGS flags);

	ID3D12RootSignature* GetRootSignature() const { return m_pRootSignature.Get(); }
	
	const BitField32& GetSamplerTableMask() const { return m_SamplerMask; }
	const BitField32& GetDescriptorTableMask() const { return m_DescriptorTableMask; }
	const std::vector<uint32_t>& GetDescriptorTableSizes() const { return m_DescriptorTableSizes; }

private:
	std::vector<D3D12_ROOT_PARAMETER1> m_RootParameters;
	std::vector<std::array<D3D12_DESCRIPTOR_RANGE1, MAX_RANGES_PER_TABLE>> m_DescriptorTableRanges;
	std::vector<uint32_t> m_DescriptorTableSizes;
	std::vector<D3D12_STATIC_SAMPLER_DESC> m_StaticSamplers;
	ComPtr<ID3D12RootSignature> m_pRootSignature;

	RootSignatureDescriptorMask m_DescriptorTableMask;
	RootSignatureDescriptorMask m_SamplerMask;
	uint32_t m_NumParameters{ 0 };
};
