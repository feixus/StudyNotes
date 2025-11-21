#pragma once
#include "Core/Bitfield.h"
#include "GraphicsResource.h"

/*
	the RootSignature describes how the GPU resources map to the shader.
	a Shader Resource can get bound to a root index directly or a descriptor table.
	a root index maps to a shader register (eg. b0, t4, u3, s1, ...)
	we keep a bitmask to later dynamically copy CPU descriptors to the GPU when rendering
*/

class ShaderBase;

using RootSignatureMask = BitField16;
static constexpr int MAX_NUM_ROOT_PARAMETERS = RootSignatureMask::Size();

class RootSignature : public GraphicsObject
{
public:
	static constexpr int MAX_RANGES_PER_TABLE = 5;

	RootSignature(Graphics* pParent);

	template<typename T>
	void SetRootConstants(uint32_t rootIndex, uint32_t shaderRegister, D3D12_SHADER_VISIBILITY visibility)
	{
		SetRootConstants(rootIndex, shaderRegister, sizeof(T) / sizeof(uint32_t), visibility);
	}

	void SetRootConstants(uint32_t rootIndex, uint32_t shaderRegister, uint32_t constantCount, D3D12_SHADER_VISIBILITY visibility);
	void SetConstantBufferView(uint32_t rootIndex, uint32_t shaderRegister, D3D12_SHADER_VISIBILITY visibility);
	void SetShaderResourceView(uint32_t rootIndex, uint32_t shaderRegister, D3D12_SHADER_VISIBILITY visibility);
	void SetUnorderedAccessView(uint32_t rootIndex, uint32_t shaderRegister, D3D12_SHADER_VISIBILITY visibility);
	void SetDescriptorTable(uint32_t rootIndex, uint32_t rangeCount, D3D12_SHADER_VISIBILITY visibility);
	void SetDescriptorTableRange(uint32_t rootIndex, uint32_t rangeIndex, uint32_t startshaderRegister, D3D12_DESCRIPTOR_RANGE_TYPE type, uint32_t count, uint32_t heapSlotOffset);
	void SetDescriptorTableSimple(uint32_t rootIndex, uint32_t startshaderRegister, D3D12_DESCRIPTOR_RANGE_TYPE type, uint32_t count, D3D12_SHADER_VISIBILITY visibility);
	
	void AddStaticSampler(uint32_t shaderRegister, const D3D12_STATIC_SAMPLER_DESC& samplerDesc, D3D12_SHADER_VISIBILITY visibility);

	void Finalize(const char* pName, D3D12_ROOT_SIGNATURE_FLAGS flags);
	void FinalizeFromShader(const char* pName, const ShaderBase* pShader);

	ID3D12RootSignature* GetRootSignature() const { return m_pRootSignature.Get(); }
	
	const RootSignatureMask& GetSamplerTableMask() const { return m_SamplerMask; }
	const RootSignatureMask& GetDescriptorTableMask() const { return m_DescriptorTableMask; }
	const std::array<uint32_t, MAX_NUM_ROOT_PARAMETERS> GetDescriptorTableSizes() const { return m_DescriptorTableSizes; }

	uint32_t GetDWordSize() const;

private:
	CD3DX12_ROOT_PARAMETER& Get(uint32_t index)
	{
		check(index < MAX_NUM_ROOT_PARAMETERS);
		m_NumParameters = Math::Max(index + 1, m_NumParameters);
		return m_RootParameters[index];
	}

	CD3DX12_DESCRIPTOR_RANGE& GetRange(uint32_t rootIndex, uint32_t rangeIndex)
	{
		check(rootIndex < MAX_NUM_ROOT_PARAMETERS);
		check(rangeIndex < MAX_RANGES_PER_TABLE);
		return m_DescriptorTableRanges[rootIndex][rangeIndex];
	}

	std::array<CD3DX12_ROOT_PARAMETER, MAX_NUM_ROOT_PARAMETERS> m_RootParameters{};
	std::array<uint32_t, MAX_NUM_ROOT_PARAMETERS> m_DescriptorTableSizes;
	std::vector<CD3DX12_STATIC_SAMPLER_DESC> m_StaticSamplers;
	std::array<std::array<CD3DX12_DESCRIPTOR_RANGE, MAX_RANGES_PER_TABLE>, MAX_NUM_ROOT_PARAMETERS> m_DescriptorTableRanges{};
	ComPtr<ID3D12RootSignature> m_pRootSignature;

	RootSignatureMask m_DescriptorTableMask;
	RootSignatureMask m_SamplerMask;
	uint32_t m_NumParameters{0};
};
