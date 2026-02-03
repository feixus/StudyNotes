#pragma once
#include "GraphicsBuffer.h"

struct DynamicAllocation
{
	GraphicsBuffer* pBackingResource{ nullptr };
	D3D12_GPU_VIRTUAL_ADDRESS GpuHandle{ 0 };
	uint64_t Offset{ 0 };
	uint64_t Size{ 0 };
	void* pMappedMemory{ nullptr };
	void Clear(uint32_t value = 0)
	{
		memset(pMappedMemory, value, Size);
	}
};

class DynamicAllocationManager : public GraphicsObject
{
public:
	DynamicAllocationManager(GraphicsDevice* pParent, BufferFlag bufferFlags);
	~DynamicAllocationManager();

	GraphicsBuffer* AllocatePage(uint64_t size);
	GraphicsBuffer* CreateNewPage(uint64_t size);

	void FreePages(uint64_t fenceValue, const std::vector<GraphicsBuffer*> pPages);
	void FreeLargePages(uint64_t fenceValue, const std::vector<GraphicsBuffer*> pLargePages);
	void CollectGrabage();

private:
	BufferFlag m_BufferFlags;
	std::mutex m_PageMutex;
	std::vector<std::unique_ptr<GraphicsBuffer>> m_Pages;
	std::queue<std::pair<uint64_t, GraphicsBuffer*>> m_FreedPages;
	std::queue<std::pair<uint64_t, std::unique_ptr<GraphicsBuffer>>> m_DeleteQueue;
};

class DynamicResourceAllocator
{
public:
	DynamicResourceAllocator(DynamicAllocationManager* pPageManager);
	
	DynamicAllocation Allocate(uint64_t size, int alignment = 256);
	void Free(uint64_t fenceValue);

private:
	DynamicAllocationManager* m_pPageManager;

	GraphicsBuffer* m_pCurrentPage{ nullptr };
	uint64_t m_CurrentOffset{ 0 };
	std::vector<GraphicsBuffer*> m_UsedPages;
	std::vector<GraphicsBuffer*> m_UsedLargePages;
};

