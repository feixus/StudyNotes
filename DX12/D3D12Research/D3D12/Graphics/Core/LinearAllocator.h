#pragma once
#include "GraphicsResource.h"

class Graphics;

struct DynamicAllocation
{
	DynamicAllocation(ID3D12Resource* pResource, size_t offset, size_t size):
			Resource(pResource), Offset(offset), Size(size)
	{}

	ID3D12Resource* Resource;
	size_t Offset;
	size_t Size;
	void* pCpuAddress;
	D3D12_GPU_VIRTUAL_ADDRESS GpuAddress;
};

class LinearAllocationPage : public GraphicsResource
{
public:
	LinearAllocationPage(GraphicsDevice* pParnet, ID3D12Resource* pResource, const size_t size, D3D12_RESOURCE_STATES usageState) :
		GraphicsResource(pParnet, pResource, usageState), m_Size(size)
	{
		//m_pResource.Attach(pResource);
		m_pGpuAddress = pResource->GetGPUVirtualAddress();
		Map();
	}

	void Map()
	{
		if (m_pCpuAddress == nullptr)
		{
			m_pResource->Map(0, nullptr, &m_pCpuAddress);
		}
	}

	size_t GetSize() const { return m_Size; }

	void Unmap()
	{
		if (m_pCpuAddress != nullptr)
		{
			m_pResource->Unmap(0, nullptr);
			m_pCpuAddress = nullptr;
		}
	}

	void* m_pCpuAddress;
	D3D12_GPU_VIRTUAL_ADDRESS m_pGpuAddress;
	size_t m_Size;
};

enum class LinearAllocationType
{
	GpuExclusive,
	CpuWrite
};

class LinearAllocatorPageManager
{
public:
	LinearAllocatorPageManager(GraphicsDevice* pGraphicsDevice, const LinearAllocationType allocationType) :
		m_pGraphicsDevice(pGraphicsDevice), m_Type(allocationType)
	{}

	LinearAllocationPage* RequestPage();
	LinearAllocationPage* CreateNewPage(const size_t size);

private:
	GraphicsDevice* m_pGraphicsDevice;

	static const int CPU_PAGE_SIZE = 0x10000;
	static const int GPU_PAGE_SIZE = 0x20000;

	std::vector<std::unique_ptr<LinearAllocationPage>> m_PagePool;
	std::queue<LinearAllocationPage*> m_AvailablePages;
	std::queue<std::pair<UINT64, LinearAllocationPage*>> m_RetiredPages;
	std::queue<std::pair<UINT64, LinearAllocationPage*>> m_DeletionQueue;

	LinearAllocationType m_Type = LinearAllocationType::CpuWrite;
};

class LinearAllocator
{
public:
	LinearAllocator(GraphicsDevice* pGraphicsDevice);
	~LinearAllocator();

	DynamicAllocation Allocate(const LinearAllocationType type, size_t size, const size_t alignment = 0);

private:
	LinearAllocationPage* m_pCurrentPage = nullptr;
	unsigned int m_CurrentOffset = 0;
	unsigned int m_PageSize = 0;

	std::vector<std::unique_ptr<LinearAllocatorPageManager>> m_PagesManagers;
};




