#pragma once

#include "DescriptorHandle.h"

class CommandContext;
class Graphics;
class RootSignature;

class DynamicDescriptorAllocator
{
public:
	DynamicDescriptorAllocator(Graphics* pGraphics, CommandContext* pContext, D3D12_DESCRIPTOR_HEAP_TYPE type);
	~DynamicDescriptorAllocator();

    void SetDescriptors(uint32_t rootIndex, uint32_t offset, uint32_t numHandles, const D3D12_CPU_DESCRIPTOR_HANDLE* pHandles);
    void UploadAndBindStagedDescriptors();

    bool HasSpace(int count);
    ID3D12DescriptorHeap* GetHeap();

    void ParseRootSignature(RootSignature* pRootSignature);
    void ReleaseUsedHeaps(uint64_t fenceValue);

private:
    static const int MAX_DESCRIPTORS_PER_COPY = 8;
    static const int DESCRIPTORS_PER_HEAP = 64;
    static const int MAX_DESCRIPTOR_PER_TABLE = 6;

    inline static std::vector<ComPtr<ID3D12DescriptorHeap>> m_DescriptorHeaps;
    inline static std::queue<std::pair<uint64_t, ID3D12DescriptorHeap*>> m_FreeDescriptors;

    uint32_t GetRequiredSpace();
    ID3D12DescriptorHeap* RequestNewHeap(D3D12_DESCRIPTOR_HEAP_TYPE type);
    void ReleaseHeap();
    void UnbindAll();

    DescriptorHandle Allocate(int descriptorCount);

    std::vector<ID3D12DescriptorHeap*> m_UsedDescriptorHeaps;

    struct RootDescriptorEntry
    {
        BitField32 AssignedHandlesBitMap{};
        D3D12_CPU_DESCRIPTOR_HANDLE* TableStart{};
        uint32_t TableSize{};
    };

    std::array<RootDescriptorEntry, MAX_DESCRIPTOR_PER_TABLE> m_RootDescriptorTable{};
    std::array<D3D12_CPU_DESCRIPTOR_HANDLE, DESCRIPTORS_PER_HEAP> m_HandleCache{};

    BitField32 m_RootDescriptorMask{};
    BitField32 m_StaleRootParameters{};

    Graphics* m_pGraphics;
    CommandContext* m_pOwner;
    DescriptorHandle m_StartHandle{};
    ID3D12DescriptorHeap* m_pCurrentHeap{nullptr};
    D3D12_DESCRIPTOR_HEAP_TYPE m_Type;
    int m_CurrentOffst{0};
    uint32_t m_DescriptorSize{0};
};