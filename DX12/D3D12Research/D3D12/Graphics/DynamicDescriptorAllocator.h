#pragma once

#include "DescriptorHandle.h"

class CommandContext;
class Graphics;
class RootSignature;

enum class DescriptorTableType
{
    Graphics,
    Compute,
};

class DynamicDescriptorAllocator
{
public:

	DynamicDescriptorAllocator(Graphics* pGraphics, CommandContext* pContext, D3D12_DESCRIPTOR_HEAP_TYPE type);
	~DynamicDescriptorAllocator() = default;

    void SetDescriptors(uint32_t rootIndex, uint32_t offset, uint32_t numHandles, const D3D12_CPU_DESCRIPTOR_HANDLE* pHandles);
    void UploadAndBindStagedDescriptors(DescriptorTableType descriptorTableType);

    bool HasSpace(int count);
    ID3D12DescriptorHeap* GetHeap();

    void ParseRootSignature(RootSignature* pRootSignature);
    void ReleaseUsedHeaps(uint64_t fenceValue);

private:
    // maximum amount of descriptors pe copyDescriptors call
    static const int MAX_DESCRIPTORS_PER_COPY = 8;
    // the amount of descriptors in each heap
    static const int DESCRIPTORS_PER_HEAP = 64;
	// the max amount of root parameters
    static const int MAX_NUM_ROOT_PARAMETERS = 6;

    inline static std::vector<ComPtr<ID3D12DescriptorHeap>> m_DescriptorHeaps;
    inline static std::array<std::queue<std::pair<uint64_t, ID3D12DescriptorHeap*>>, 2> m_FreeDescriptors;

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

    std::array<RootDescriptorEntry, MAX_NUM_ROOT_PARAMETERS> m_RootDescriptorTable{};
    std::array<D3D12_CPU_DESCRIPTOR_HANDLE, DESCRIPTORS_PER_HEAP> m_HandleCache{};

    BitField32 m_RootDescriptorMask{};
    BitField32 m_StaleRootParameters{};

    Graphics* m_pGraphics;
    CommandContext* m_pOwner;
    DescriptorHandle m_StartHandle{};
    ID3D12DescriptorHeap* m_pCurrentHeap{nullptr};
    D3D12_DESCRIPTOR_HEAP_TYPE m_Type;
    uint32_t m_CurrentOffst{0};
    uint32_t m_DescriptorSize{0};
};