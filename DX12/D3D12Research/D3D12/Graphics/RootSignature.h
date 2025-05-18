#pragma once

/*
	the RootSignature describes how the GPU resources map to the shader.
	a Shader Resource can get bound to a root index directly or a descriptor table.
	a root index maps to a shader register (eg. b0, t4, u3, s1, ...)
	we keep a bitmask to later dynamically copy CPU descriptors to the GPU when rendering
*/

class RootSignature
{
public:

	static const int MAX_NUM_DESCRIPTORS = 16;
	static const int MAX_RANGES_PER_TABLE = 2;
	static_assert(MAX_NUM_DESCRIPTORS <= BitField32::Capacity(), "Descriptor bitfield is not large enough");

	RootSignature();

	void SetSize(uint32_t size, bool shrink = true);

	void SetRootConstants(uint32_t rootIndex, uint32_t shaderRegister, uint32_t constantCount, D3D12_SHADER_VISIBILITY visibility);
	void SetConstantBufferView(uint32_t rootIndex, uint32_t shaderRegister, D3D12_SHADER_VISIBILITY visibility);
	void SetShaderResourceView(uint32_t rootIndex, uint32_t shaderRegister, D3D12_SHADER_VISIBILITY visibility);
	void SetUnorderedAccessView(uint32_t rootIndex, uint32_t shaderRegister, D3D12_SHADER_VISIBILITY visibility);
	void SetDescriptorTable(uint32_t rootIndex, uint32_t rangeCount, D3D12_SHADER_VISIBILITY visibility);
	void SetDescriptorTableRange(uint32_t rootIndex, uint32_t rangeIndex, uint32_t startshaderRegister, D3D12_DESCRIPTOR_RANGE_TYPE type, uint32_t count);
	void SetDescriptorTableSimple(uint32_t rootIndex, uint32_t startshaderRegister, D3D12_DESCRIPTOR_RANGE_TYPE type, uint32_t count, D3D12_SHADER_VISIBILITY visibility);
	
	void AddStaticSampler(uint32_t shaderRegister, D3D12_SAMPLER_DESC samplerDesc, D3D12_SHADER_VISIBILITY visibility);

	void Finalize(const char* pName, ID3D12Device* pDevice, D3D12_ROOT_SIGNATURE_FLAGS flags);

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

	BitField32 m_DescriptorTableMask;
	BitField32 m_SamplerMask;
	uint32_t m_NumParameters{ 0 };
};
