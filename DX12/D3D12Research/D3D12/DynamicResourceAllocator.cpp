#include "stdafx.h"
#include "DynamicResourceAllocator.h"
#include "Graphics.h"

DynamicResourceAllocator::DynamicResourceAllocator(Graphics* pGraphics, bool gpuVisible, int size)
	: m_pGraphics(pGraphics), m_Size(size)
{
	m_pBackingResource = CreateResource(gpuVisible, size, &m_pMappedMemory);
}

DynamicAllocation DynamicResourceAllocator::Allocate(int size, int alignment)
{
	int bufferSize = (size + (alignment - 1)) & ~(alignment - 1);
	DynamicAllocation allocation;
	allocation.Size = bufferSize;

	m_TotalMemoryAllocation += bufferSize;
	m_TotalMemoryAllocationPeak = std::max<uint64_t>(m_TotalMemoryAllocation, m_TotalMemoryAllocationPeak);

	if (bufferSize > m_Size)
	{
		m_LargeResources.emplace_back(CreateResource(true, bufferSize, &allocation.pMappedMemory));

		allocation.pBackingResource = m_LargeResources.back().Get();
		allocation.Offset = 0;
		allocation.GpuHandle = allocation.pBackingResource->GetGPUVirtualAddress();
	}
	else
	{
		allocation.pBackingResource = m_pBackingResource.Get();
		
		m_CurrentOffset = ((size_t)m_CurrentOffset + (alignment - 1)) & ~(alignment - 1);
		
		if (bufferSize + m_CurrentOffset >= m_Size)
		{
			m_CurrentOffset = 0;
			if (m_FenceOffsets.size() > 0)
			{
				int maxOffset = 0;
				while (m_FenceOffsets.size() > 0 && m_pGraphics->IsFenceComplete(m_FenceOffsets.front().first))
				{
					maxOffset = m_FenceOffsets.front().second;
					m_FenceOffsets.pop();
				}
				//assert(bufferSize <= maxOffset);
			}
		}

		allocation.GpuHandle = m_pBackingResource->GetGPUVirtualAddress() + m_CurrentOffset;
		allocation.Offset = m_CurrentOffset;
		allocation.pMappedMemory = static_cast<std::byte*>(m_pMappedMemory) + m_CurrentOffset;
	
		m_CurrentOffset += bufferSize;
	}

	return allocation;
}

void DynamicResourceAllocator::Free(uint64_t fenceValue)
{
	m_FenceOffsets.emplace(fenceValue, m_CurrentOffset);
}

ComPtr<ID3D12Resource> DynamicResourceAllocator::CreateResource(bool gpuVisible, int size, void** pMappedData)
{
	ComPtr<ID3D12Resource> pResource;

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
	// where the resource's memory resides and who(CPU or GPU) can access it.
	//default: GPU-only memory, upload: CPU-accessible memory, readback: CPU-accessible memory
	heapProps.Type = gpuVisible ? D3D12_HEAP_TYPE_UPLOAD : D3D12_HEAP_TYPE_DEFAULT;
	heapProps.VisibleNodeMask = 0;

	// resource state: how the GPU to use a resource. 
	// combine heap type + resource state.  i.e. upload + generic_read, default + copy_dest -> pixel_shader_resource
	HR(m_pGraphics->GetDevice()->CreateCommittedResource(
		&heapProps,
		D3D12_HEAP_FLAG_NONE,
		&desc,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(pResource.GetAddressOf())));

	// map and initialize the constant buffer. dont unmap this until the app closes
	// keeping things mapped for the lifetime of the resource is okay
	D3D12_RANGE readRange; // dont intend to read from this resource on the CPU
	readRange.Begin = 0;
	readRange.End = 0;
	HR(pResource->Map(0, &readRange, pMappedData));

	return pResource;
}
