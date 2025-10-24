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

class DynamicAllocationManager : public GraphicsObject
{
public:
	DynamicAllocationManager(Graphics* pGraphics, BufferFlag bufferFlags);
	~DynamicAllocationManager();

	Buffer* AllocatePage(size_t size);
	Buffer* CreateNewPage(size_t size);

	void FreePages(uint64_t fenceValue, const std::vector<Buffer*> pPages);
	void FreeLargePages(uint64_t fenceValue, const std::vector<Buffer*> pLargePages);
	void CollectGrabage();

	uint64_t GetMemoryUsage() const;

private:
	BufferFlag m_BufferFlags;
	std::mutex m_PageMutex;
	std::vector<std::unique_ptr<Buffer>> m_Pages;
	std::queue<std::pair<uint64_t, Buffer*>> m_FreedPages;
	std::queue<std::pair<uint64_t, std::unique_ptr<Buffer>>> m_DeleteQueue;
};

class DynamicResourceAllocator
{
public:
	DynamicResourceAllocator(DynamicAllocationManager* pPageManager);
	
	DynamicAllocation Allocate(size_t size, int alignment = 256);
	void Free(uint64_t fenceValue);

private:
	DynamicAllocationManager* m_pPageManager;

	Buffer* m_pCurrentPage{ nullptr };
	size_t m_CurrentOffset{ 0 };
	std::vector<Buffer*> m_UsedPages;
	std::vector<Buffer*> m_UsedLargePages;
};

