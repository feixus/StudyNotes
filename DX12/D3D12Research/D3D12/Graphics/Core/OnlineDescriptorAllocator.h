#pragma once

#include "DescriptorHandle.h"
#include "GraphicsResource.h"

class CommandContext;
class Graphics;
class RootSignature;

enum class DescriptorTableType
{
    Graphics,
    Compute,
};

struct DescriptorHeapBlock
{
    DescriptorHeapBlock(DescriptorHandle startHandle, uint32_t size, uint32_t currentOffset)
        : StartHandle(startHandle), Size(size), CurrentOffset(currentOffset), FenceValue(0)
    {}

    DescriptorHandle StartHandle;
    uint32_t Size;
    uint32_t CurrentOffset;
    uint64_t FenceValue;
};

class GlobalOnlineDescriptorHeap : public GraphicsObject
{
public:
    GlobalOnlineDescriptorHeap(Graphics* pGraphics, D3D12_DESCRIPTOR_HEAP_TYPE type, uint32_t blockSize, uint32_t numDescriptors);

    DescriptorHeapBlock* AllocateBlock();
    void FreeBlock(uint64_t fenceValue, DescriptorHeapBlock* pBlock);

    uint32_t GetDescriptorSize() const { return m_DescriptorSize; }
    ID3D12DescriptorHeap* GetHeap() const { return m_pHeap.Get(); }
    D3D12_DESCRIPTOR_HEAP_TYPE GetType() const { return m_Type; }

private:
    std::mutex m_BlockAllocateMutex;
    D3D12_DESCRIPTOR_HEAP_TYPE m_Type;
    uint32_t m_NumDescriptors;

    uint32_t m_DescriptorSize{0};
    DescriptorHandle m_StartHandle{};

    ComPtr<ID3D12DescriptorHeap> m_pHeap;
    std::vector<std::unique_ptr<DescriptorHeapBlock>> m_HeapBlocks;
    std::vector<DescriptorHeapBlock*> m_ReleasedBlocks;
    std::queue<DescriptorHeapBlock*> m_FreeBlocks;
};

class OnlineDescriptorAllocator : public GraphicsObject
{
public:
	OnlineDescriptorAllocator(GlobalOnlineDescriptorHeap* pGlobalHeap, CommandContext* pContext);
	~OnlineDescriptorAllocator() = default;

    DescriptorHandle Allocate(uint32_t count);

    void SetDescriptors(uint32_t rootIndex, uint32_t offset, uint32_t numHandles, const D3D12_CPU_DESCRIPTOR_HANDLE* pHandles);
    void BindStagedDescriptors(DescriptorTableType descriptorTableType);

    void ParseRootSignature(RootSignature* pRootSignature);
    void ReleaseUsedHeaps(uint64_t fenceValue);

private:
    static const int MAX_NUM_ROOT_PARAMETERS = 16;

    struct RootDescriptorEntry
    {
        uint32_t TableSize{};
        DescriptorHandle GpuHandle;
    };

    std::array<RootDescriptorEntry, MAX_NUM_ROOT_PARAMETERS> m_RootDescriptorTable{};

    BitField32 m_RootDescriptorMask{};
    BitField32 m_StaleRootParameters{};

    GlobalOnlineDescriptorHeap* m_pHeapAllocator;
    DescriptorHeapBlock* m_pCurrentHeapBlock{nullptr};
    std::vector<DescriptorHeapBlock*> m_ReleasedBlocks;

    CommandContext* m_pOwner;
    D3D12_DESCRIPTOR_HEAP_TYPE m_Type;
};

