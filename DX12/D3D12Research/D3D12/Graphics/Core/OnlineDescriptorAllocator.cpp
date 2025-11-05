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
    m_StartHandle = DescriptorHandle(m_pHeap->GetCPUDescriptorHandleForHeapStart(), m_pHeap->GetGPUDescriptorHandleForHeapStart());

    uint32_t numBlocks = m_NumDescriptors / blockSize;

    DescriptorHandle currentOffset = m_StartHandle;
    for (uint32_t i = 0; i < numBlocks; i++)
    {
        m_HeapBlocks.emplace_back(std::make_unique<DescriptorHeapBlock>(currentOffset, blockSize, 0));
        m_FreeBlocks.push(m_HeapBlocks.back().get());
        currentOffset += blockSize * m_DescriptorSize;
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

OnlineDescriptorAllocator::OnlineDescriptorAllocator(Graphics* pGraphics, CommandContext* pContext, D3D12_DESCRIPTOR_HEAP_TYPE type)
    : GraphicsObject(pGraphics), m_pOwner(pContext), m_Type(type)
{
    if (type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV)
    {
        m_pHeapAllocator = pGraphics->GetGlobalViewHeap();
    }
    else if (type == D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER)
    {
        m_pHeapAllocator = pGraphics->GetGlobalSamplerHeap();
    }
    else
    {
        noEntry();
    }
}

void OnlineDescriptorAllocator::SetDescriptors(uint32_t rootIndex, uint32_t offset, uint32_t numHandles, const D3D12_CPU_DESCRIPTOR_HANDLE* pHandles)
{
    checkf(m_RootDescriptorMask.GetBit(rootIndex), "RootSignature does not have a DescriptorTable at root index %d", rootIndex);
    check(numHandles + offset <= m_RootDescriptorTable[rootIndex].TableSize);

    RootDescriptorEntry& entry = m_RootDescriptorTable[rootIndex];
    bool dirty = false;
    for (uint32_t i = 0; i < numHandles; i++)
    {
        if (entry.TableStart[i + offset].ptr != pHandles[i].ptr)
        {
            entry.TableStart[i + offset] = pHandles[i];
            entry.AssignedHandlesBitMap.SetBit(i + offset);
            dirty = true;
        }
    }

    if (dirty)
    {
        m_StaleRootParameters.SetBit(rootIndex);
    }
}

void OnlineDescriptorAllocator::UploadAndBindStagedDescriptors(DescriptorTableType descriptorTableType)
{
    if (m_StaleRootParameters.HasAnyBitSet() == false)
    {
        return;
    }

    uint32_t requireSpace = GetRequiredSpace();
    DescriptorHandle gpuHandle = Allocate(requireSpace);

    int sourceRangeCount = 0;
    int destinationRangeCount = 0;

    std::array<D3D12_CPU_DESCRIPTOR_HANDLE, MAX_DESCRIPTORS_PER_TABLE> sourceRanges{};
    std::array<D3D12_CPU_DESCRIPTOR_HANDLE, MAX_DESCRIPTORS_PER_TABLE> destinationRanges{};
    std::array<uint32_t, MAX_DESCRIPTORS_PER_TABLE> sourceRangeSizes{};
    std::array<uint32_t, MAX_DESCRIPTORS_PER_TABLE> destinationRangeSizes{};

    int tableCount = 0;
    std::array<D3D12_GPU_DESCRIPTOR_HANDLE, MAX_NUM_ROOT_PARAMETERS> newDescriptorTables{};

    for (auto it = m_StaleRootParameters.GetSetBitsIterator(); it.Valid(); ++it)
    {
        // if the range count exceeds the max amount of descriptors per copy, flush
		if (sourceRangeCount >= MAX_DESCRIPTORS_PER_TABLE)
		{
			GetGraphics()->GetDevice()->CopyDescriptors(destinationRangeCount, destinationRanges.data(), destinationRangeSizes.data(),
				sourceRangeCount, sourceRanges.data(), sourceRangeSizes.data(), m_Type);

			sourceRangeCount = 0;
			destinationRangeCount = 0;
		}

        uint32_t rootIndex = it.Value();
        RootDescriptorEntry& entry = m_RootDescriptorTable[rootIndex];
        
        uint32_t rangeSize = 0;
        entry.AssignedHandlesBitMap.MostSignificantBit(&rangeSize);
        rangeSize += 1;

        for (uint32_t i = 0; i < rangeSize; i++)
        {
            sourceRangeSizes[sourceRangeCount] = 1;
            check(entry.TableStart[i].ptr);
            sourceRanges[sourceRangeCount] = entry.TableStart[i];
            ++sourceRangeCount;
        }

        destinationRanges[destinationRangeCount] = gpuHandle.GetCpuHandle();
        destinationRangeSizes[destinationRangeCount] = rangeSize;
        ++destinationRangeCount;

		newDescriptorTables[tableCount++] = gpuHandle.GetGpuHandle();
		gpuHandle += rangeSize * m_pHeapAllocator->GetDescriptorSize();
    }

	GetGraphics()->GetDevice()->CopyDescriptors(destinationRangeCount, destinationRanges.data(), destinationRangeSizes.data(),
		sourceRangeCount, sourceRanges.data(), sourceRangeSizes.data(), m_Type);

    int i = 0;
    for (auto it = m_StaleRootParameters.GetSetBitsIterator(); it.Valid(); ++it)
    {
        uint32_t rootIndex = it.Value();
        switch (descriptorTableType)
        {
        case DescriptorTableType::Compute:
            m_pOwner->GetCommandList()->SetComputeRootDescriptorTable(rootIndex, newDescriptorTables[i++]);
            break;
        case DescriptorTableType::Graphics:
            m_pOwner->GetCommandList()->SetGraphicsRootDescriptorTable(rootIndex, newDescriptorTables[i++]);
            break;
        default:
            noEntry();
            break;
        }
    }

    m_StaleRootParameters.ClearAll();
}

bool OnlineDescriptorAllocator::EnsureSpace(uint32_t count)
{
    if (!m_pCurrentHeapBlock || m_pCurrentHeapBlock->Size - m_pCurrentHeapBlock->CurrentOffset < count)
    {
        if (m_pCurrentHeapBlock)
        {
            m_ReleasedBlocks.push_back(m_pCurrentHeapBlock);
        }
        m_pCurrentHeapBlock = m_pHeapAllocator->AllocateBlock();
    }
    return true;
}

void OnlineDescriptorAllocator::ParseRootSignature(RootSignature* pRootSignature)
{
    m_RootDescriptorMask = m_Type == D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER ? pRootSignature->GetSamplerTableMask() : pRootSignature->GetDescriptorTableMask();
    
    m_StaleRootParameters.ClearAll();
    memset(m_HandleCache.data(), 0, m_HandleCache.size() * sizeof(D3D12_CPU_DESCRIPTOR_HANDLE));

    uint32_t offset = 0;
    for (uint32_t rootIndex : m_RootDescriptorMask)
    {
        RootDescriptorEntry& entry = m_RootDescriptorTable[rootIndex];
        entry.AssignedHandlesBitMap.ClearAll();
        uint32_t tableSize = pRootSignature->GetDescriptorTableSizes()[rootIndex];
        checkf(tableSize <= MAX_DESCRIPTORS_PER_TABLE, "the descriptor table at root index %d is too large. size is %d, maximum is %d", rootIndex, tableSize, MAX_DESCRIPTORS_PER_TABLE);
        check(tableSize > 0);
        entry.TableSize = tableSize;
        entry.TableStart = &m_HandleCache[offset];
        offset += entry.TableSize;
        checkf(offset <= m_HandleCache.size(), "Out of DescriptorTable handles!");
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

uint32_t OnlineDescriptorAllocator::GetRequiredSpace()
{
    uint32_t requiredSpace = 0;
    for (auto it = m_StaleRootParameters.GetSetBitsIterator(); it.Valid(); ++it)
    {
        uint32_t rootIndex = it.Value();
        uint32_t maxHandle = 0;
        m_RootDescriptorTable[rootIndex].AssignedHandlesBitMap.MostSignificantBit(&maxHandle);
        requiredSpace += (uint32_t)maxHandle + 1;
    }

    return requiredSpace;
}

void OnlineDescriptorAllocator::UnbindAll()
{
    m_StaleRootParameters.ClearAll();
    for (auto it = m_RootDescriptorMask.GetSetBitsIterator(); it.Valid(); ++it)
    {
        uint32_t rootIndex = it.Value();
        if (m_RootDescriptorTable[rootIndex].AssignedHandlesBitMap.HasAnyBitSet())
        {
            m_StaleRootParameters.SetBit(rootIndex);
        }
    }
}

DescriptorHandle OnlineDescriptorAllocator::Allocate(int descriptorCount)
{
    EnsureSpace(descriptorCount);
    DescriptorHandle handle = m_pCurrentHeapBlock->StartHandle + m_pCurrentHeapBlock->CurrentOffset * m_pHeapAllocator->GetDescriptorSize();
    m_pCurrentHeapBlock->CurrentOffset += descriptorCount;
    return handle;
}