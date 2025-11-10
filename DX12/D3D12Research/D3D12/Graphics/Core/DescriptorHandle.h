#pragma once

class DescriptorHandle
{
public:
	DescriptorHandle() : CpuHandle(InvalidCPUHandle), GpuHandle(InvalidGPUHandle), HeapIndex(InvalidHeapIndex)
	{}

	DescriptorHandle(D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle, uint32_t heapIndex, D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle = InvalidGPUHandle)
		: CpuHandle(cpuHandle), HeapIndex(heapIndex), GpuHandle(gpuHandle)
	{}

	DescriptorHandle& OffsetInline(uint32_t numDescriptors, uint32_t descriptorSize)
	{
		if (CpuHandle != InvalidCPUHandle)
		{
			CpuHandle.Offset(numDescriptors, descriptorSize);
		}
		if (GpuHandle != InvalidGPUHandle)
		{
			GpuHandle.Offset(numDescriptors, descriptorSize);
		}
		if (HeapIndex != InvalidHeapIndex)
		{
			HeapIndex += numDescriptors;
		}

		return *this;
	}

	DescriptorHandle Offset(uint32_t numDescriptors, uint32_t descriptorSize)
	{
		DescriptorHandle handle = *this;
		return handle.OffsetInline(numDescriptors, descriptorSize);
	}

	void Reset()
	{
		CpuHandle = InvalidCPUHandle;
		GpuHandle = InvalidGPUHandle;
		HeapIndex = InvalidHeapIndex;
	}

	bool IsNull() const { return CpuHandle == InvalidCPUHandle; }
	bool IsShaderVisible() const{ return GpuHandle != InvalidGPUHandle; }

	constexpr static D3D12_CPU_DESCRIPTOR_HANDLE InvalidCPUHandle{~0u};
	constexpr static D3D12_GPU_DESCRIPTOR_HANDLE InvalidGPUHandle{~0u};
	constexpr static uint32_t InvalidHeapIndex{~0u};

	CD3DX12_CPU_DESCRIPTOR_HANDLE CpuHandle;
	CD3DX12_GPU_DESCRIPTOR_HANDLE GpuHandle;
	uint32_t HeapIndex;
};
