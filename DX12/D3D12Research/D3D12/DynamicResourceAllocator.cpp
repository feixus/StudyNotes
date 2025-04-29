#include "stdafx.h"
#include "DynamicResourceAllocator.h"
#include <assert.h>

DynamicResourceAllocator::DynamicResourceAllocator(ID3D12Device* pDevice, bool gpuVisible, int size)
	: m_Size(size)
{
	D3D12_RESOURCE_DESC desc = {};
	desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	desc.Alignment = 0;
	desc.DepthOrArraySize = 1;
	desc.Flags = D3D12_RESOURCE_FLAG_NONE;
	desc.Width = size;
	desc.Height = 1;
	desc.Format = DXGI_FORMAT_UNKNOWN;
	desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	desc.MipLevels = 1;
	desc.SampleDesc.Count = 1;
	desc.SampleDesc.Quality = 0;

	D3D12_HEAP_PROPERTIES heapProps{};
	heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
	heapProps.CreationNodeMask = 0;
	heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
	heapProps.Type = gpuVisible ? D3D12_HEAP_TYPE_UPLOAD : D3D12_HEAP_TYPE_DEFAULT;
	heapProps.VisibleNodeMask = 0;

	HR(pDevice->CreateCommittedResource(
		&heapProps,
		D3D12_HEAP_FLAG_NONE,
		&desc,
		D3D12_RESOURCE_STATE_COMMON,
		nullptr,
		IID_PPV_ARGS(m_pBackingResource.GetAddressOf())));

	// map and initialize the constant buffer. dont unmap this until the app closes
	// keeping things mapped for the lifetime of the resource is okay
	D3D12_RANGE readRange; // dont intend to read from this resource on the CPU
	readRange.Begin = 0;
	readRange.End = 0;
	HR(m_pBackingResource->Map(0, &readRange, &m_pMappedMemory));
}

DynamicResourceAllocator::~DynamicResourceAllocator()
{
}

DynamicAllocation DynamicResourceAllocator::Allocate(int size, int alignment)
{
	int bufferSize = (size + (alignment - 1)) & ~(alignment - 1);
	DynamicAllocation allocation;
	allocation.pBackingResource = m_pBackingResource.Get();
	allocation.Size = bufferSize;

	m_CurrentOffset = ((size_t)m_CurrentOffset + (alignment - 1)) & ~(alignment - 1);
	if (bufferSize + m_CurrentOffset >= m_Size)
	{
		m_CurrentOffset = 0;
		if (m_FenceOffsets.size() > 0)
		{
			int maxOffset = m_FenceOffsets.front().second;
			assert(m_CurrentOffset + bufferSize <= maxOffset);
		}
	}

	allocation.GpuHandle = m_pBackingResource->GetGPUVirtualAddress() + m_CurrentOffset;
	allocation.Offset = m_CurrentOffset;
	allocation.pMappedMemory = static_cast<std::byte*>(m_pMappedMemory) + m_CurrentOffset;
	
	m_CurrentOffset += bufferSize;
	return allocation;
}

void DynamicResourceAllocator::Free(uint64_t fenceValue)
{
	while (m_FenceOffsets.size() > 0)
	{
		const auto& offset = m_FenceOffsets.front();
		if (fenceValue > offset.first)
		{
			m_FenceOffsets.pop();
		}
		else
		{
			break;
		}
	}

	m_FenceOffsets.emplace(fenceValue, m_CurrentOffset);
}
