#pragma once
#include "DescriptorHandle.h"

class DescriptorAllocator
{
public:
	DescriptorAllocator(ID3D12Device* pDevice, D3D12_DESCRIPTOR_HEAP_TYPE type, bool gpuVisible = false);
	~DescriptorAllocator() = default;

	D3D12_CPU_DESCRIPTOR_HANDLE AllocateDescriptor();

	ID3D12DescriptorHeap* GetCurrentHeap() { return m_DescriptorHeapPool.back().Get(); }
	uint32_t GetHeapCount() const { return (uint32_t)m_DescriptorHeapPool.size(); }
	uint32_t GetNumAllocatedDescriptors() const { return DESCRIPTORS_PER_HEAP - m_RemainingDescriptors; }
	D3D12_DESCRIPTOR_HEAP_TYPE GetType() const { return m_Type; }
	
	static const int DESCRIPTORS_PER_HEAP = 256;

private:
	void AllocateNewHeap();

	bool m_GpuVisible;
	std::vector<ComPtr<ID3D12DescriptorHeap>> m_DescriptorHeapPool;
	CD3DX12_CPU_DESCRIPTOR_HANDLE m_CurrentCpuHandle;
	ID3D12Device* m_pDevice;
	D3D12_DESCRIPTOR_HEAP_TYPE m_Type;
	uint32_t m_DescriptorSize{ 0 };
	uint32_t m_RemainingDescriptors{ DESCRIPTORS_PER_HEAP };
};