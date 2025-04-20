#include "stdafx.h"
#include "CommandQueue.h"
#include "CommandAllocatorPool.h"

CommandQueueManager::CommandQueueManager(ID3D12Device* pDevice)
	: m_pDevice(pDevice)
{
	m_CommandQueues[D3D12_COMMAND_LIST_TYPE_DIRECT] = std::make_unique<CommandQueue>(pDevice, CommandQueueType::Graphics);
}

CommandQueue* CommandQueueManager::GetMainCommandQueue() const
{
	return m_CommandQueues.at(D3D12_COMMAND_LIST_TYPE_DIRECT).get();
}

CommandContext* CommandQueueManager::AllocatorCommandList(D3D12_COMMAND_LIST_TYPE type)
{
	unsigned long long fenceValue = m_CommandQueues[type]->GetLastCompletedFence();
	ID3D12CommandAllocator* pAllocator = m_CommandQueues[type]->GetAllocatorPool()->GetAllocator(fenceValue);
	if (m_FreeCommandList.size() > 0)
	{
		CommandContext* pCommandContext = m_FreeCommandList.front();
		m_FreeCommandList.pop();
		pCommandContext->pCommandList->Reset(pAllocator, nullptr);
		pCommandContext->pAllocator = pAllocator;

		return pCommandContext;
	}

	ID3D12CommandList* pCommandList;
	m_pDevice->CreateCommandList(0, type, pAllocator, nullptr, IID_PPV_ARGS(&pCommandList));
	std::unique_ptr<ID3D12GraphicsCommandList> pCmd = std::unique_ptr<ID3D12GraphicsCommandList>(static_cast<ID3D12GraphicsCommandList*>(pCommandList));
	
	m_CommandListPool.push_back(CommandContext(std::move(pCmd), pAllocator));
	return &m_CommandListPool.back();
}

void CommandQueueManager::FreeCommandList(CommandContext* pCommandList)
{
	m_FreeCommandList.push(pCommandList);
}

CommandQueue::CommandQueue(ID3D12Device* pDevice, CommandQueueType type)
{
	D3D12_COMMAND_QUEUE_DESC desc = {};
	desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	desc.NodeMask = 0;
	desc.Priority = 0;
	switch (type)
	{
	case CommandQueueType::Graphics:
		desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
		break;
	case CommandQueueType::Compute:
		desc.Type = D3D12_COMMAND_LIST_TYPE_COMPUTE;
		break;
	case CommandQueueType::Copy:
		desc.Type = D3D12_COMMAND_LIST_TYPE_COPY;
		break;
	case CommandQueueType::MAX:
	default:
		return;
		break;
	}

	m_Type = desc.Type;

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

UINT64 CommandQueue::ExecuteCommandList(CommandContext* pCommandContext, bool waitForCompletion)
{  
   HR(pCommandContext->pCommandList->Close());
   ID3D12CommandList* ppCommandLists[] = { pCommandContext->pCommandList.get()};
   m_pCommandQueue->ExecuteCommandLists(1, ppCommandLists);  

   // Use std::scoped_lock instead of std::lock_guard to avoid the deleted copy constructor issue  
   std::scoped_lock lock(m_FenceMutex);  
   m_pCommandQueue->Signal(m_pFence.Get(), m_NextFenceValue);  
   m_pAllocatorPool->FreeAllocator(pCommandContext->pAllocator, m_NextFenceValue);

   if (waitForCompletion)
   {
	   WaitForFenceBlock(m_NextFenceValue);
   }
   m_NextFenceValue++;

   return m_NextFenceValue;  
}

bool CommandQueue::IsFenceComplete(UINT64 fenceValue)
{
	if (fenceValue > m_LastCompletedFenceValue)
	{
		PollCurrentFenceValue();
	}

	return fenceValue <= m_LastCompletedFenceValue;
}

void CommandQueue::InsertWait(UINT64 fenceValue)
{
	m_pCommandQueue->Wait(m_pFence.Get(), fenceValue);
}

void CommandQueue::InsertWaitForQueueFence(CommandQueue* pCommandQueue, UINT64 fenceValue)
{
	m_pCommandQueue->Wait(pCommandQueue->GetFence(), fenceValue);
}

void CommandQueue::InsertWaitForQueue(CommandQueue* pCommandQueue)
{
	m_pCommandQueue->Wait(pCommandQueue->GetFence(), pCommandQueue->GetNextFenceValue() - 1);
}

void CommandQueue::WaitForFenceBlock(UINT64 fenceValue)
{
	if (IsFenceComplete(fenceValue))
	{
		return;
	}

	{
		std::lock_guard<std::mutex> lockGuard(m_EventMutex);

		m_pFence->SetEventOnCompletion(fenceValue, m_pFenceEventHandle);
		WaitForSingleObject(m_pFenceEventHandle, INFINITE);
		m_LastCompletedFenceValue = fenceValue;
	}
}

void CommandQueue::WaitForIdle()
{
	WaitForFenceBlock((UINT64)max(0ll, (INT64)m_NextFenceValue - 1));
}

UINT64 CommandQueue::PollCurrentFenceValue()
{
	m_LastCompletedFenceValue = max(m_LastCompletedFenceValue, m_pFence->GetCompletedValue());
	return m_LastCompletedFenceValue;
}
