#include "stdafx.h"
#include "DynamicDescriptorAllocator.h"
#include "Graphics.h"
#include "CommandContext.h"
#include "RootSignature.h"

DynamicDescriptorAllocator::DynamicDescriptorAllocator(Graphics* pGraphics, CommandContext* pContext, D3D12_DESCRIPTOR_HEAP_TYPE type)
    : m_pGraphics(pGraphics), m_pOwner(pContext), m_Type(type)
{
    m_DescriptorSize = pGraphics->GetDevice()->GetDescriptorHandleIncrementSize(type);
}

void DynamicDescriptorAllocator::SetDescriptors(uint32_t rootIndex, uint32_t offset, uint32_t numHandles, const D3D12_CPU_DESCRIPTOR_HANDLE* pHandles)
{
    assert(m_RootDescriptorMask.GetBit(rootIndex));
    assert(numHandles + offset <= m_RootDescriptorTable[rootIndex].TableSize);

    RootDescriptorEntry& entry = m_RootDescriptorTable[rootIndex];
    for (uint32_t i = 0; i < numHandles; i++)
    {
        entry.TableStart[i + offset] = pHandles[i];
        entry.AssignedHandlesBitMap.SetBit(i + offset);
    }
    m_StaleRootParameters.SetBit(rootIndex);
}

void DynamicDescriptorAllocator::UploadAndBindStagedDescriptors(DescriptorTableType descriptorTableType)
{
    if (m_StaleRootParameters.AnyBitSet() == false)
    {
        return;
    }

    uint32_t requireSpace = GetRequiredSpace();
    if (HasSpace(requireSpace) == false)
    {
        ReleaseHeap();
        UnbindAll();
        requireSpace = GetRequiredSpace();
    }
    m_pOwner->SetDescriptorHeap(GetHeap(), m_Type);

    DescriptorHandle gpuHandle = Allocate(requireSpace);

    int sourceRangeCount = 0;
    int destinationRangeCount = 0;

    std::array<D3D12_CPU_DESCRIPTOR_HANDLE, MAX_DESCRIPTORS_PER_COPY> sourceRanges{};
    std::array<D3D12_CPU_DESCRIPTOR_HANDLE, MAX_DESCRIPTORS_PER_COPY> destinationRanges{};
    std::array<uint32_t, MAX_DESCRIPTORS_PER_COPY> sourceRangeSizes{};
    std::array<uint32_t, MAX_DESCRIPTORS_PER_COPY> destinationRangeSizes{};

    for (auto it = m_StaleRootParameters.GetSetBitsIterator(); it.Valid(); ++it)
    {
        // if the range count exceeds the max amount of descriptors per copy, flush
		if (sourceRangeCount >= MAX_DESCRIPTORS_PER_COPY)
		{
			m_pGraphics->GetDevice()->CopyDescriptors(destinationRangeCount, destinationRanges.data(), destinationRangeSizes.data(),
				sourceRangeCount, sourceRanges.data(), sourceRangeSizes.data(), m_Type);

			sourceRangeCount = 0;
			destinationRangeCount = 0;
		}

        uint32_t rootIndex = it.Value();
        RootDescriptorEntry& entry = m_RootDescriptorTable[rootIndex];
        
        uint32_t rangeSize = 0;
        entry.AssignedHandlesBitMap.MostSignificantBit(&rangeSize);
        rangeSize += 1;

		// copy the descriptors one by one because they aren't necessarily contiguous
        for (uint32_t i = 0; i < rangeSize; i++)
        {
            sourceRangeSizes[sourceRangeCount] = 1;
            sourceRanges[sourceRangeCount] = *entry.TableStart;
            ++sourceRangeCount;
        }

        destinationRanges[destinationRangeCount] = gpuHandle.GetCpuHandle();
        destinationRangeSizes[destinationRangeCount] = rangeSize;
        ++destinationRangeCount;

        switch (descriptorTableType)
        {
        case DescriptorTableType::Compute:
            m_pOwner->GetCommandList()->SetComputeRootDescriptorTable(rootIndex, gpuHandle.GetGpuHandle());
            break;
        case DescriptorTableType::Graphics:
            m_pOwner->GetCommandList()->SetGraphicsRootDescriptorTable(rootIndex, gpuHandle.GetGpuHandle());
            break;
        default:
            assert(false);
            break;
        }

        gpuHandle += rangeSize * m_DescriptorSize;
    }

    m_StaleRootParameters.ClearAll();

    m_pGraphics->GetDevice()->CopyDescriptors(destinationRangeCount, destinationRanges.data(), destinationRangeSizes.data(),
        sourceRangeCount, sourceRanges.data(), sourceRangeSizes.data(), m_Type);
}

bool DynamicDescriptorAllocator::HasSpace(int count)
{
    return m_pCurrentHeap && m_CurrentOffst + count <= DESCRIPTORS_PER_HEAP;
}

ID3D12DescriptorHeap* DynamicDescriptorAllocator::GetHeap()
{
    if (m_pCurrentHeap == nullptr)
    {
        m_pCurrentHeap = RequestNewHeap(m_Type);
        m_StartHandle = DescriptorHandle(m_pCurrentHeap->GetCPUDescriptorHandleForHeapStart(), m_pCurrentHeap->GetGPUDescriptorHandleForHeapStart());
    }

    return m_pCurrentHeap;
}

void DynamicDescriptorAllocator::ParseRootSignature(RootSignature* pRootSignature)
{
    m_RootDescriptorMask = m_Type == D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER ? pRootSignature->GetSamplerTableMask() : pRootSignature->GetDescriptorTableMask();
    m_StaleRootParameters.ClearAll();

    uint32_t offset = 0;
    for (auto it = m_RootDescriptorMask.GetSetBitsIterator(); it.Valid(); ++it)
    {
        int rootIndex = it.Value();
        RootDescriptorEntry& entry = m_RootDescriptorTable[rootIndex];
        entry.AssignedHandlesBitMap.ClearAll();
        uint32_t tableSize = pRootSignature->GetDescriptorTableSizes()[rootIndex];
        assert(tableSize > 0);
        entry.TableSize = tableSize;
        entry.TableStart = &m_HandleCache[offset];
        offset += entry.TableSize;
    }
}

void DynamicDescriptorAllocator::ReleaseUsedHeaps(uint64_t fenceValue)
{
    ReleaseHeap();
    for (ID3D12DescriptorHeap* pHeap : m_UsedDescriptorHeaps)
    {
        m_FreeDescriptors[m_Type].emplace(fenceValue, pHeap);
    }
    m_UsedDescriptorHeaps.clear();
}

uint32_t DynamicDescriptorAllocator::GetRequiredSpace()
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

ID3D12DescriptorHeap* DynamicDescriptorAllocator::RequestNewHeap(D3D12_DESCRIPTOR_HEAP_TYPE type)
{
    if (m_FreeDescriptors[m_Type].size() > 0 && m_pGraphics->IsFenceComplete(m_FreeDescriptors[m_Type].front().first))
    {
        ID3D12DescriptorHeap* pHeap = m_FreeDescriptors[m_Type].front().second;
        m_FreeDescriptors[m_Type].pop();
        return pHeap;
    }

    ComPtr<ID3D12DescriptorHeap> pHeap;
    D3D12_DESCRIPTOR_HEAP_DESC desc{};
    desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    desc.NumDescriptors = DESCRIPTORS_PER_HEAP;
    desc.NodeMask = 0;
    desc.Type = type;
    HR(m_pGraphics->GetDevice()->CreateDescriptorHeap(&desc, IID_PPV_ARGS(pHeap.GetAddressOf())));
    m_DescriptorHeaps.push_back(std::move(pHeap));
    return m_DescriptorHeaps.back().Get();
}

void DynamicDescriptorAllocator::ReleaseHeap()
{
    if (m_CurrentOffst == 0)
    {
        assert(m_pCurrentHeap == nullptr);
        return;
    }

    assert(m_pCurrentHeap);
    m_UsedDescriptorHeaps.push_back(m_pCurrentHeap);
    m_pCurrentHeap = nullptr;
    m_CurrentOffst = 0;
}

void DynamicDescriptorAllocator::UnbindAll()
{
    m_StaleRootParameters.ClearAll();
    for (auto it = m_RootDescriptorMask.GetSetBitsIterator(); it.Valid(); ++it)
    {
        uint32_t rootIndex = it.Value();
        if (m_RootDescriptorTable[rootIndex].AssignedHandlesBitMap.AnyBitSet())
        {
            m_StaleRootParameters.SetBit(rootIndex);
        }
    }
}

DescriptorHandle DynamicDescriptorAllocator::Allocate(int descriptorCount)
{
    DescriptorHandle handle = m_StartHandle + m_CurrentOffst * m_DescriptorSize;
    m_CurrentOffst += descriptorCount;
    return handle;
}
