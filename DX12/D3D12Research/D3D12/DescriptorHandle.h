#pragma once

class DescriptorHandle
{
public:
	DescriptorHandle()
	{
		m_CpuHandle.ptr = -1;
		m_GpuHandle.ptr = -1;
	}

	DescriptorHandle(D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle)
		: m_CpuHandle(cpuHandle)
	{
		m_GpuHandle.ptr = -1;
	}

	DescriptorHandle(D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle, D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle)
		: m_CpuHandle(cpuHandle), m_GpuHandle(gpuHandle)
	{
	}

	void operator += (uint32_t offsetScaledByDescriptorSize)
	{
		if (m_CpuHandle.ptr != -1)
		{
			m_CpuHandle.ptr += offsetScaledByDescriptorSize;
		}
		if (m_GpuHandle.ptr != -1)
		{
			m_GpuHandle.ptr += offsetScaledByDescriptorSize;
		}
	}

	D3D12_CPU_DESCRIPTOR_HANDLE GetCpuHandle() const { return m_CpuHandle; }
	D3D12_GPU_DESCRIPTOR_HANDLE GetGpuHandle() const { return m_GpuHandle; }

	bool IsNull() const { return m_CpuHandle.ptr == -1; }
	bool IsShaderVisible() const{ return m_GpuHandle.ptr != -1; }

private:
	D3D12_CPU_DESCRIPTOR_HANDLE m_CpuHandle;
	D3D12_GPU_DESCRIPTOR_HANDLE m_GpuHandle;
};
