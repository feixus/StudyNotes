#pragma once

struct DynamicAllocation
{
	ID3D12Resource* pBackingResource{ nullptr };
	D3D12_GPU_VIRTUAL_ADDRESS GpuHandle{ 0 };
	int Offset{ 0 };
	int Size{ 0 };
	void* pMappedMemory{ nullptr };
};

class DynamicResourceAllocator
{
public:
	DynamicResourceAllocator(ID3D12Device* pDevice, bool gpuVisible, int size);
	~DynamicResourceAllocator() = default;

	DynamicAllocation Allocate(int size, int alignment = 256);
	void Free(uint64_t fenceValue);

private:
	ID3D12Device* m_pDevice;
	ComPtr<ID3D12Resource> CreateResource(ID3D12Device* pDevice, bool gpuVisible, int size, void** pMappedData);

	ComPtr<ID3D12Resource> m_pBackingResource;
	std::vector<ComPtr<ID3D12Resource>> m_LargeResources;
	std::queue<std::pair<uint64_t, uint32_t>> m_FenceOffsets;
	int m_CurrentOffset{ 0 };
	int m_Size{ 0 };
	void* m_pMappedMemory{ nullptr };
};

