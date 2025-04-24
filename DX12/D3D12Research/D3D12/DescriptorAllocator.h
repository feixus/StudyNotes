#pragma once

class DescriptorAllocator
{
public:
	DescriptorAllocator(ID3D12Device* pDevice, D3D12_DESCRIPTOR_HEAP_TYPE type);
	~DescriptorAllocator();

	D3D12_CPU_DESCRIPTOR_HANDLE AllocateDescriptor();

	std::pair<D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_GPU_DESCRIPTOR_HANDLE> AllocateDescriptorWithGPU();

	std::vector<ComPtr<ID3D12DescriptorHeap>>& GetDescriptorHeapPool() { return m_DescriptorHeapPool
	; }

private:
	void AllocateNewHeap();

	static const int DESCRIPTORS_PER_HEAP = 64;
	std::vector<ComPtr<ID3D12DescriptorHeap>> m_DescriptorHeapPool;
	CD3DX12_CPU_DESCRIPTOR_HANDLE m_CurrentHandle;
	CD3DX12_GPU_DESCRIPTOR_HANDLE m_CurrentGPUHandle;
	ID3D12Device* m_pDevice;
	D3D12_DESCRIPTOR_HEAP_TYPE m_Type;
	uint32_t m_DescriptorSize = 0;
	uint32_t m_RemainingDescriptors = 0;
};