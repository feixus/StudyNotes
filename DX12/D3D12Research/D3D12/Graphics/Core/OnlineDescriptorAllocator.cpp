#include "stdafx.h"
#include "OnlineDescriptorAllocator.h"
#include "Graphics.h"
#include "CommandContext.h"
#include "RootSignature.h"

GlobalOnlineDescriptorHeap::GlobalOnlineDescriptorHeap(Graphics* pGraphics, D3D12_DESCRIPTOR_HEAP_TYPE type, uint32_t blockSize, uint32_t numDescriptors)
    : GraphicsObject(pGraphics), m_Type(type), m_NumDescriptors(numDescriptors)
{
    checkf(numDescriptors % blockSize == 0, "number of descriptors must be a multiple of blockSize (%d)", blockSize);

    D3D12_DESCRIPTOR_HEAP_DESC desc{};
    desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    desc.NodeMask = 0;
    desc.NumDescriptors = numDescriptors;
    desc.Type = type;
    VERIFY_HR_EX(GetGraphics()->GetDevice()->CreateDescriptorHeap(&desc, IID_PPV_ARGS(m_pHeap.GetAddressOf())), GetGraphics()->GetDevice());
    D3D::SetObjectName(m_pHeap.Get(), "Global Online Descriptor Heap");

    m_DescriptorSize = pGraphics->GetDevice()->GetDescriptorHandleIncrementSize(type);
    m_StartHandle = DescriptorHandle(m_pHeap->GetCPUDescriptorHandleForHeapStart(), 0, m_pHeap->GetGPUDescriptorHandleForHeapStart());

    uint32_t numBlocks = m_NumDescriptors / blockSize;

    DescriptorHandle currentOffset = m_StartHandle;
    for (uint32_t i = 0; i < numBlocks; i++)
    {
        m_HeapBlocks.emplace_back(std::make_unique<DescriptorHeapBlock>(currentOffset, blockSize, 0));
        m_FreeBlocks.push(m_HeapBlocks.back().get());
        currentOffset.OffsetInline(blockSize, m_DescriptorSize);
    }
}

DescriptorHeapBlock* GlobalOnlineDescriptorHeap::AllocateBlock()
{
    std::lock_guard lock(m_BlockAllocateMutex);

    for (int i = 0; i < m_ReleasedBlocks.size(); i++)
    {
        // check if we can free so finished blocks
        DescriptorHeapBlock* pBlock = m_ReleasedBlocks[i];
        if (GetGraphics()->IsFenceComplete(pBlock->FenceValue))
        {
            std::swap(m_ReleasedBlocks[i], m_ReleasedBlocks.back());
            m_ReleasedBlocks.pop_back();
            m_FreeBlocks.push(pBlock);
            --i;
        }
    }

    checkf(!m_FreeBlocks.empty(), "ran out of descriptor heap space. must increase the number of descriptors.");

    DescriptorHeapBlock* pBlock = m_FreeBlocks.front();
    m_FreeBlocks.pop();
    return pBlock;
}

void GlobalOnlineDescriptorHeap::FreeBlock(uint64_t fenceValue, DescriptorHeapBlock* pBlock)
{
    std::lock_guard lock(m_BlockAllocateMutex);
    pBlock->FenceValue = fenceValue;
    pBlock->CurrentOffset = 0;
    m_ReleasedBlocks.push_back(pBlock);
}

OnlineDescriptorAllocator::OnlineDescriptorAllocator(GlobalOnlineDescriptorHeap* pGlobalHeap, CommandContext* pContext)
    : GraphicsObject(pGlobalHeap->GetGraphics()), m_pOwner(pContext), m_Type(pGlobalHeap->GetType()), m_pHeapAllocator(pGlobalHeap)
{}

void OnlineDescriptorAllocator::SetDescriptors(uint32_t rootIndex, uint32_t offset, uint32_t numHandles, const D3D12_CPU_DESCRIPTOR_HANDLE* pHandles)
{
    RootDescriptorEntry& entry = m_RootDescriptorTable[rootIndex];
    if (!m_StaleRootParameters.GetBit(rootIndex))
    {
        uint32_t tableSize = entry.TableSize;
        entry.Descriptor = Allocate(tableSize);
        m_StaleRootParameters.SetBit(rootIndex);
    }

    DescriptorHandle targetHandle = entry.Descriptor.Offset(offset, m_pHeapAllocator->GetDescriptorSize());
    for (uint32_t i = 0; i < numHandles; i++)
    {
        checkf(pHandles[i].ptr != DescriptorHandle::InvalidCPUHandle.ptr, "Invalid Descriptor provided (RootIndex: %d, Offset: %d)", rootIndex, offset + i);
        
        GetGraphics()->GetDevice()->CopyDescriptorsSimple(1, targetHandle.CpuHandle, pHandles[i], m_Type);
        targetHandle.OffsetInline(1, m_pHeapAllocator->GetDescriptorSize());
    }
}

void OnlineDescriptorAllocator::BindStagedDescriptors(CommandListContext descriptorTableType)
{
    if (m_StaleRootParameters.HasAnyBitSet() == false)
    {
        return;
    }

    for (uint32_t rootIndex : m_StaleRootParameters)
    {
        RootDescriptorEntry& entry = m_RootDescriptorTable[rootIndex];
        switch (descriptorTableType)
        {
        case CommandListContext::Compute:
            m_pOwner->GetCommandList()->SetComputeRootDescriptorTable(rootIndex, entry.Descriptor.GpuHandle);
            break;
        case CommandListContext::Graphics:
            m_pOwner->GetCommandList()->SetGraphicsRootDescriptorTable(rootIndex, entry.Descriptor.GpuHandle);
            break;
        default:
            noEntry();
            break;
        }
    }

    m_StaleRootParameters.ClearAll();
}

void OnlineDescriptorAllocator::ParseRootSignature(RootSignature* pRootSignature)
{
    m_RootDescriptorMask = m_Type == D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER ? pRootSignature->GetSamplerTableMask() : pRootSignature->GetDescriptorTableMask();
    
    m_StaleRootParameters.ClearAll();

    for (uint32_t rootIndex : m_RootDescriptorMask)
    {
        RootDescriptorEntry& entry = m_RootDescriptorTable[rootIndex];
        entry.TableSize = pRootSignature->GetDescriptorTableSizes()[rootIndex];
        entry.Descriptor.Reset();
    }
}

void OnlineDescriptorAllocator::ReleaseUsedHeaps(uint64_t fenceValue)
{
    for (DescriptorHeapBlock* pBlock : m_ReleasedBlocks)
    {
        m_pHeapAllocator->FreeBlock(fenceValue, pBlock);
    }
    m_ReleasedBlocks.clear();
}

DescriptorHandle OnlineDescriptorAllocator::Allocate(uint32_t descriptorCount)
{
    if (!m_pCurrentHeapBlock || m_pCurrentHeapBlock->Size - m_pCurrentHeapBlock->CurrentOffset < descriptorCount)
    {
        if (m_pCurrentHeapBlock)
        {
            m_ReleasedBlocks.push_back(m_pCurrentHeapBlock);
        }
        m_pCurrentHeapBlock = m_pHeapAllocator->AllocateBlock();
    }
    
    DescriptorHandle handle = m_pCurrentHeapBlock->StartHandle.Offset(m_pCurrentHeapBlock->CurrentOffset, m_pHeapAllocator->GetDescriptorSize());;
    m_pCurrentHeapBlock->CurrentOffset += descriptorCount;
    return handle;
}
