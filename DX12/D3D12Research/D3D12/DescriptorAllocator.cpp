#include "stdafx.h"
#include "DescriptorAllocator.h"

DescriptorAllocator::DescriptorAllocator(ID3D12Device* pDevice, D3D12_DESCRIPTOR_HEAP_TYPE type)
		: m_pDevice(pDevice), m_Type(type)
{
	m_DescriptorSize = pDevice->GetDescriptorHandleIncrementSize(type);
}

DescriptorAllocator::~DescriptorAllocator()
{
}

D3D12_CPU_DESCRIPTOR_HANDLE DescriptorAllocator::AllocateDescriptor()
{
	if (m_RemainingDescriptors <= 0)
	{
		AllocateNewHeap();
	}

	D3D12_CPU_DESCRIPTOR_HANDLE handle = m_CurrentHandle;
	m_CurrentHandle.Offset(1, m_DescriptorSize);
	--m_RemainingDescriptors;

	return handle;
}

DescriptorHandle DescriptorAllocator::AllocateDescriptorWithGPU()
{
	if (m_RemainingDescriptors <= 0)
	{
		AllocateNewHeap();
	}

	D3D12_CPU_DESCRIPTOR_HANDLE handle = m_CurrentHandle;
	m_CurrentHandle.Offset(1, m_DescriptorSize);

	D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle = m_CurrentGPUHandle;
	m_CurrentGPUHandle.Offset(1, m_DescriptorSize);

	--m_RemainingDescriptors;
	return { handle, gpuHandle };
}

void DescriptorAllocator::AllocateNewHeap()
{
	D3D12_DESCRIPTOR_HEAP_DESC desc{};
	desc.Flags = (m_Type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV || m_Type == D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER) ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	desc.NodeMask = 0;
	desc.NumDescriptors = DESCRIPTORS_PER_HEAP;
	desc.Type = m_Type;
	
	ComPtr<ID3D12DescriptorHeap> pNewHeap;
	m_pDevice->CreateDescriptorHeap(&desc, IID_PPV_ARGS(pNewHeap.GetAddressOf()));
	m_DescriptorHeapPool.push_back(std::move(pNewHeap));

	m_CurrentHandle = m_DescriptorHeapPool.back()->GetCPUDescriptorHandleForHeapStart();
	m_CurrentGPUHandle = m_DescriptorHeapPool.back()->GetGPUDescriptorHandleForHeapStart();

	m_RemainingDescriptors = DESCRIPTORS_PER_HEAP;
}
