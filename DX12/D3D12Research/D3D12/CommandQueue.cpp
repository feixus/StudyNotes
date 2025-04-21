#include "stdafx.h"
#include "CommandQueue.h"
#include "CommandAllocatorPool.h"
#include "Graphics.h"

CommandQueue::CommandQueue(ID3D12Device* pDevice, D3D12_COMMAND_LIST_TYPE type)
{
	D3D12_COMMAND_QUEUE_DESC desc = {};
	desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	desc.NodeMask = 0;
	desc.Priority = 0;
	desc.Type = type;

	m_Type = type;

	HR(pDevice->CreateCommandQueue(&desc, IID_PPV_ARGS(m_pCommandQueue.GetAddressOf())));
	HR(pDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(m_pFence.GetAddressOf())));
	m_pFence->Signal(m_LastCompletedFenceValue);

	m_pFenceEventHandle = CreateEventEx(nullptr, nullptr, 0, EVENT_ALL_ACCESS);

	m_pAllocatorPool = std::make_unique<CommandAllocatorPool>(pDevice, m_Type);
}

CommandQueue::~CommandQueue()
{
	CloseHandle(m_pFenceEventHandle);
}

uint64_t CommandQueue::ExecuteCommandList(CommandContext* pCommandContext, bool waitForCompletion)
{  
   HR(pCommandContext->pCommandList->Close());
   ID3D12CommandList* ppCommandLists[] = { pCommandContext->pCommandList};
   m_pCommandQueue->ExecuteCommandLists(1, ppCommandLists);  

   // Use std::scoped_lock instead of std::lock_guard to avoid the deleted copy constructor issue  
   std::scoped_lock lock(m_FenceMutex);  
   m_pCommandQueue->Signal(m_pFence.Get(), m_NextFenceValue);  
   m_pAllocatorPool->FreeAllocator(pCommandContext->pAllocator, m_NextFenceValue);

   if (waitForCompletion)
   {
	   WaitForFenceBlock(m_NextFenceValue);
   }

   return m_NextFenceValue++;  
}

bool CommandQueue::IsFenceComplete(uint64_t fenceValue)
{
	if (fenceValue > m_LastCompletedFenceValue)
	{
		m_LastCompletedFenceValue = (std::max)(m_LastCompletedFenceValue, m_pFence->GetCompletedValue());
	}

	return fenceValue <= m_LastCompletedFenceValue;
}

void CommandQueue::InsertWait(uint64_t fenceValue)
{
	m_pCommandQueue->Wait(m_pFence.Get(), fenceValue);
}

void CommandQueue::InsertWaitForQueueFence(CommandQueue* pCommandQueue, uint64_t fenceValue)
{
	m_pCommandQueue->Wait(pCommandQueue->GetFence(), fenceValue);
}

void CommandQueue::InsertWaitForQueue(CommandQueue* pCommandQueue)
{
	m_pCommandQueue->Wait(pCommandQueue->GetFence(), pCommandQueue->GetNextFenceValue() - 1);
}

uint64_t CommandQueue::IncrementFence()
{
	std::scoped_lock lock(m_FenceMutex);
	m_pCommandQueue->Signal(m_pFence.Get(), m_NextFenceValue);
	return m_NextFenceValue++;
}

void CommandQueue::WaitForFenceBlock(uint64_t fenceValue)
{
	if (IsFenceComplete(fenceValue))
	{
		return;
	}

	{
		std::lock_guard lockGuard(m_EventMutex);

		m_pFence->SetEventOnCompletion(fenceValue, m_pFenceEventHandle);
		WaitForSingleObject(m_pFenceEventHandle, INFINITE);
		m_LastCompletedFenceValue = fenceValue;
	}
}

void CommandQueue::WaitForIdle()
{
	WaitForFenceBlock(IncrementFence());
}

uint64_t CommandQueue::PollCurrentFenceValue()
{
	m_LastCompletedFenceValue = max(m_LastCompletedFenceValue, m_pFence->GetCompletedValue());
	return m_LastCompletedFenceValue;
}
