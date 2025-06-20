#include "stdafx.h"
#include "OfflineDescriptorAllocator.h"

OfflineDescriptorAllocator::OfflineDescriptorAllocator(ID3D12Device* pDevice, D3D12_DESCRIPTOR_HEAP_TYPE type)
        : m_pDevice(pDevice), m_Type(type)
{
    m_DescriptorSize = pDevice->GetDescriptorHandleIncrementSize(type);
}

OfflineDescriptorAllocator::~OfflineDescriptorAllocator()
{
}

D3D12_CPU_DESCRIPTOR_HANDLE OfflineDescriptorAllocator::AllocateDescriptor()
{
    if (m_FreeHeaps.size() == 0)
    {
        AllocateNewHeap();
    }

    std::list<Heap::Range>& freeRange = m_Heaps[m_FreeHeaps.front()]->FreeRanges;
    Heap::Range& range = freeRange.front();
    D3D12_CPU_DESCRIPTOR_HANDLE outHandle = range.Begin;
    range.Begin.Offset(m_DescriptorSize);
    if (range.Begin == range.End)
    {
        freeRange.erase(freeRange.begin());
        if (freeRange.size() == 0)
        {
            m_FreeHeaps.remove(m_FreeHeaps.front());
        }
    }
    ++m_NumAllocateDescriptors;
    return outHandle;
}

void OfflineDescriptorAllocator::FreeDescriptor(D3D12_CPU_DESCRIPTOR_HANDLE handle)
{
    int heapIndex = -1;
    for (size_t i = 0; i < m_Heaps.size(); i++)
    {
        D3D12_CPU_DESCRIPTOR_HANDLE start = m_Heaps[i]->pHeap->GetCPUDescriptorHandleForHeapStart();
        if (handle.ptr >= start.ptr && handle.ptr < start.ptr + m_DescriptorSize * DESCRIPTOR_PER_HEAP)
        {
            heapIndex = (int)i;
            break;
        }
    }
    
    assert(heapIndex >= 0);
    Heap* pHeap = m_Heaps[heapIndex].get();
    Heap::Range newRange {
        CD3DX12_CPU_DESCRIPTOR_HANDLE(handle),
        CD3DX12_CPU_DESCRIPTOR_HANDLE(handle, m_DescriptorSize)
    };

    bool found = false;
    for (auto range = pHeap->FreeRanges.begin(); range != pHeap->FreeRanges.end() && found == false; ++range)
    {
        if (range->Begin.ptr == handle.ptr + m_DescriptorSize)
        {
            range->Begin = handle;
            found = true;
        }
        else if (range->End.ptr == handle.ptr)
        {
            range->End.ptr += m_DescriptorSize;
            found = true;
        }
        else
        {
            if (range->Begin.ptr > handle.ptr)
            {
                pHeap->FreeRanges.insert(range, newRange);
                found = true;
            }
        }
    }

    if (!found)
    {
        if (pHeap->FreeRanges.size() == 0)
        {
            m_FreeHeaps.push_back(heapIndex);
        }
        pHeap->FreeRanges.push_back(newRange);
    }
    --m_NumAllocateDescriptors;
}

void OfflineDescriptorAllocator::AllocateNewHeap()
{
    D3D12_DESCRIPTOR_HEAP_DESC desc{};
    desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    desc.NodeMask = 0;
    desc.NumDescriptors = DESCRIPTOR_PER_HEAP;
    desc.Type = m_Type;

    std::unique_ptr<Heap> pHeap = std::make_unique<Heap>();
    m_pDevice->CreateDescriptorHeap(&desc, IID_PPV_ARGS(pHeap->pHeap.GetAddressOf()));
    CD3DX12_CPU_DESCRIPTOR_HANDLE begin = CD3DX12_CPU_DESCRIPTOR_HANDLE(pHeap->pHeap->GetCPUDescriptorHandleForHeapStart());
    pHeap->FreeRanges.push_back(Heap::Range{ begin, CD3DX12_CPU_DESCRIPTOR_HANDLE(begin, DESCRIPTOR_PER_HEAP, m_DescriptorSize)});
    m_Heaps.push_back(std::move(pHeap));
    m_FreeHeaps.push_back((int)m_Heaps.size() - 1);

    m_NumDescriptors += DESCRIPTOR_PER_HEAP;
}
