#pragma once
#include "GraphicsBuffer.h"

class Graphics;

struct DynamicAllocation
{
	Buffer* pBackingResource{ nullptr };
	D3D12_GPU_VIRTUAL_ADDRESS GpuHandle{ 0 };
	size_t Offset{ 0 };
	size_t Size{ 0 };
	void* pMappedMemory{ nullptr };
};

class AllocationPage : public Buffer
{
public:
	AllocationPage(Graphics* pGraphics);
	void Create(uint64_t size);
	inline void* GetMappedData() const { return m_pMappedData; }

private:
	void* m_pMappedData{nullptr};
};

class DynamicAllocationManager : public GraphicsObject
{
public:
	DynamicAllocationManager(Graphics* pGraphics);
	~DynamicAllocationManager();

	AllocationPage* AllocatePage(size_t size);
	AllocationPage* CreateNewPage(size_t size);

	void FreePages(uint64_t fenceValue, const std::vector<AllocationPage*> pPages);
	void FreeLargePages(uint64_t fenceValue, const std::vector<AllocationPage*> pLargePages);
	void FlushAll();

	uint64_t GetMemoryUsage() const;

private:
	std::mutex m_PageMutex;
	std::vector<std::unique_ptr<AllocationPage>> m_Pages;
	std::queue<std::pair<uint64_t, AllocationPage*>> m_FreedPages;
	std::queue<std::pair<uint64_t, std::unique_ptr<AllocationPage>>> m_DeleteQueue;
};

class DynamicResourceAllocator
{
public:
	DynamicResourceAllocator(DynamicAllocationManager* pPageManager);
	
	DynamicAllocation Allocate(size_t size, int alignment = 256);
	void Free(uint64_t fenceValue);

private:
	DynamicAllocationManager* m_pPageManager;

	AllocationPage* m_pCurrentPage{ nullptr };
	size_t m_CurrentOffset{ 0 };
	std::vector<AllocationPage*> m_UsedPages;
	std::vector<AllocationPage*> m_UsedLargePages;
};

