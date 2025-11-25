#pragma once

#include "DescriptorHandle.h"
#include "GraphicsResource.h"

struct Heap
{
    struct Range
    {
        CD3DX12_CPU_DESCRIPTOR_HANDLE Begin;
        CD3DX12_CPU_DESCRIPTOR_HANDLE End;
    };

    ComPtr<ID3D12DescriptorHeap> pHeap;
    std::list<Range> FreeRanges;
};

class OfflineDescriptorAllocator : public GraphicsObject
{
public:
    OfflineDescriptorAllocator(GraphicsDevice* pParent, D3D12_DESCRIPTOR_HEAP_TYPE type, uint32_t descriptorsPerHeap);
    ~OfflineDescriptorAllocator();

    CD3DX12_CPU_DESCRIPTOR_HANDLE AllocateDescriptor();
    void FreeDescriptor(D3D12_CPU_DESCRIPTOR_HANDLE handle);
    D3D12_DESCRIPTOR_HEAP_TYPE GetType() const { return m_Type; }

    int GetNumDescriptors() const { return m_NumDescriptors; }
    int GetNumAllocatedDescriptors() const { return m_NumAllocateDescriptors; }

private:
    void AllocateNewHeap();

    std::vector<std::unique_ptr<Heap>> m_Heaps;
    std::list<int> m_FreeHeaps;
    int m_NumAllocateDescriptors{0};
    int m_NumDescriptors{0};

    uint32_t m_DescriptorsPerHeap;
    uint32_t m_DescriptorSize{0};
    D3D12_DESCRIPTOR_HEAP_TYPE m_Type;
};