#pragma once

class DescriptorHandle
{
public:
	DescriptorHandle()
	{
		m_CpuHandle.ptr = InvalidHandle;
		m_GpuHandle.ptr = InvalidHandle;
	}

	DescriptorHandle(D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle)
		: m_CpuHandle(cpuHandle), m_GpuHandle({InvalidHandle})
	{}

	DescriptorHandle(D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle, D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle)
		: m_CpuHandle(cpuHandle), m_GpuHandle(gpuHandle)
	{
	}

	void operator += (uint32_t offsetScaledByDescriptorSize)
	{
		if (m_CpuHandle.ptr != InvalidHandle)
		{
			m_CpuHandle.ptr += offsetScaledByDescriptorSize;
		}
		if (m_GpuHandle.ptr != InvalidHandle)
		{
			m_GpuHandle.ptr += offsetScaledByDescriptorSize;
		}
	}

	DescriptorHandle operator + (uint32_t offset)
	{
		D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle{};
		D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle{};
		cpuHandle.ptr = m_CpuHandle.ptr + offset;
		gpuHandle.ptr = m_GpuHandle.ptr + offset;
		return { cpuHandle, gpuHandle };
	}

	D3D12_CPU_DESCRIPTOR_HANDLE GetCpuHandle() const { return m_CpuHandle; }
	D3D12_GPU_DESCRIPTOR_HANDLE GetGpuHandle() const { return m_GpuHandle; }

	bool IsNull() const { return m_CpuHandle.ptr == InvalidHandle; }
	bool IsShaderVisible() const{ return m_GpuHandle.ptr != InvalidHandle; }

	constexpr static size_t InvalidHandle = (size_t)-1;

private:
	D3D12_CPU_DESCRIPTOR_HANDLE m_CpuHandle;
	D3D12_GPU_DESCRIPTOR_HANDLE m_GpuHandle;
};
