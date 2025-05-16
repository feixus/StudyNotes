#pragma once

class Graphics;

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
	DynamicResourceAllocator(Graphics* pDevice, bool gpuVisible, int size);
	~DynamicResourceAllocator() = default;

	DynamicAllocation Allocate(int size, int alignment = 256);
	void Free(uint64_t fenceValue);

	void ResetAllocationCounter() { m_TotalMemoryAllocation = 0; }
	uint64_t GetTotalMemoryAllocation() const { return m_TotalMemoryAllocation; }
	uint64_t GetTotalMemoryAllocationPeak() const { return m_TotalMemoryAllocationPeak; }

private:
	ComPtr<ID3D12Resource> CreateResource(bool gpuVisible, int size, void** pMappedData);

	Graphics* m_pGraphics;
	ComPtr<ID3D12Resource> m_pBackingResource;
	std::vector<ComPtr<ID3D12Resource>> m_LargeResources;
	std::queue<std::pair<uint64_t, uint32_t>> m_FenceOffsets;
	int m_CurrentOffset{ 0 };
	int m_Size{ 0 };
	void* m_pMappedMemory{ nullptr };

	uint64_t m_TotalMemoryAllocationPeak{ 0 };
	uint64_t m_TotalMemoryAllocation{ 0 };
};

