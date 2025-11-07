#pragma once

class DescriptorHandle
{
public:
	DescriptorHandle()
	{
		m_CpuHandle = InvalidCPUHandle;
		m_GpuHandle = InvalidGPUHandle;
	}

	DescriptorHandle(D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle)
		: m_CpuHandle(cpuHandle), m_GpuHandle(InvalidGPUHandle)
	{}

	DescriptorHandle(D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle, D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle)
		: m_CpuHandle(cpuHandle), m_GpuHandle(gpuHandle)
	{
	}

	void operator += (uint32_t offsetScaledByDescriptorSize)
	{
		if (m_CpuHandle != InvalidCPUHandle)
		{
			m_CpuHandle.Offset(1, offsetScaledByDescriptorSize);
		}
		if (m_GpuHandle != InvalidGPUHandle)
		{
			m_GpuHandle.Offset(1, offsetScaledByDescriptorSize);
		}
	}

	DescriptorHandle operator + (uint32_t offset)
	{
		DescriptorHandle ret = *this;
		ret += offset;
		return ret;
	}

	D3D12_CPU_DESCRIPTOR_HANDLE GetCpuHandle() const { return m_CpuHandle; }
	D3D12_GPU_DESCRIPTOR_HANDLE GetGpuHandle() const { return m_GpuHandle; }

	bool IsNull() const { return m_CpuHandle == InvalidCPUHandle; }
	bool IsShaderVisible() const{ return m_GpuHandle != InvalidGPUHandle; }

	constexpr static D3D12_CPU_DESCRIPTOR_HANDLE InvalidCPUHandle = {~0u};
	constexpr static D3D12_GPU_DESCRIPTOR_HANDLE InvalidGPUHandle = {~0u};

private:
	CD3DX12_CPU_DESCRIPTOR_HANDLE m_CpuHandle;
	CD3DX12_GPU_DESCRIPTOR_HANDLE m_GpuHandle;
};
