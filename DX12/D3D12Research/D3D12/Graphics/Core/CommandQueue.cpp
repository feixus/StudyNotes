#include "stdafx.h"
#include "CommandQueue.h"
#include "Graphics.h"
#include "CommandContext.h"

#include "pix3.h"

CommandAllocatorPool::CommandAllocatorPool(Graphics* pGraphics, D3D12_COMMAND_LIST_TYPE type)
	: GraphicsObject(pGraphics), m_Type(type)
{}

ID3D12CommandAllocator* CommandAllocatorPool::GetAllocator(uint64_t fenceValue)
{
	std::scoped_lock lock(m_AllocationMutex);

	if (m_FreeAllocators.empty() == false)
	{
		std::pair<ID3D12CommandAllocator*, uint64_t>& pFirst = m_FreeAllocators.front();
		if (pFirst.second <= fenceValue)
		{
			m_FreeAllocators.pop();
			pFirst.first->Reset();
			return pFirst.first;
		}
	}

	ComPtr<ID3D12CommandAllocator> pAllocator;
	m_pGraphics->GetDevice()->CreateCommandAllocator(m_Type, IID_PPV_ARGS(pAllocator.GetAddressOf()));
	D3D::SetD3DObjectName(pAllocator.Get(), "Pooled Allocator");
	m_CommandAllocators.push_back(std::move(pAllocator));

	return m_CommandAllocators.back().Get();
}

void CommandAllocatorPool::FreeAllocator(ID3D12CommandAllocator* pAllocator, uint64_t fenceValue)
{
	std::scoped_lock lock(m_AllocationMutex);
	m_FreeAllocators.push(std::pair<ID3D12CommandAllocator*, uint64_t>(pAllocator, fenceValue));
}

CommandQueue::CommandQueue(Graphics* pGraphics, D3D12_COMMAND_LIST_TYPE type)
	: GraphicsObject(pGraphics),
	m_NextFenceValue((uint64_t)type << 56 | 1),			// set the command list type nested in fence value
	m_LastCompletedFenceValue((uint64_t)type << 56),
	m_Type(type)
{
	m_pAllocatorPool = std::make_unique<CommandAllocatorPool>(pGraphics, type);

	D3D12_COMMAND_QUEUE_DESC desc = {};
	desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	desc.NodeMask = 0;
	desc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
	desc.Type = type;

	VERIFY_HR_EX(pGraphics->GetDevice()->CreateCommandQueue(&desc, IID_PPV_ARGS(m_pCommandQueue.GetAddressOf())), m_pGraphics->GetDevice());
	D3D::SetD3DObjectName(m_pCommandQueue.Get(), "Main CommandQueue");
	VERIFY_HR_EX(pGraphics->GetDevice()->CreateFence(m_LastCompletedFenceValue, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(m_pFence.GetAddressOf())), m_pGraphics->GetDevice());
	D3D::SetD3DObjectName(m_pFence.Get(), "CommandQueue Fence");
	m_pFenceEventHandle = CreateEventExA(nullptr, "CommandQueue Fence", 0, EVENT_ALL_ACCESS);
}

CommandQueue::~CommandQueue()
{
	CloseHandle(m_pFenceEventHandle);
}

uint64_t CommandQueue::ExecuteCommandList(CommandContext** pCommandContexts, uint32_t numContexts)
{
	check(pCommandContexts);
	check(numContexts > 0);

	// commandlist can be recorded in parallel.
	// the before state of a resource transition can't be know so commandlists keep local resource state
	// and insert "pending resource barriers" which are barriers with an unknown before state.
	// during commandlist execution, these pending resource barriers are resolved by inserting new barriers in the previous commndlist before closing it.
	// the first commandlist will resolve the barriers of the next so the first one will just contain resource barriers

	std::vector<ID3D12CommandList*> commandLists;
	commandLists.reserve(numContexts + 1);
	CommandContext* pCurrentContext = nullptr;
	for (uint32_t i = 0; i < numContexts; i++)
	{
		CommandContext* pNextContext = pCommandContexts[i];

		ResourceBarrierBatcher barriers;
		for (const CommandContext::PendingBarrier& pending : pNextContext->GetPendingBarriers())
		{
			uint32_t subResource = pending.SubResource;
			GraphicsResource* pResource = pending.pResource;
			D3D12_RESOURCE_STATES beforeState = pResource->GetResourceState(subResource);
			checkf(CommandContext::IsTransitionAllowed(m_Type, beforeState), "the resource (%s) can not transitions from this state (%s) on this queue (%s). insert a barrier on another queue before executing this one.",
				pResource->GetName().c_str(), D3D::ResourceStateToString(beforeState).c_str(), D3D::CommandlistTypeToString(m_Type));
			barriers.AddTransition(pResource->GetResource(), beforeState, pending.State.Get(subResource), subResource);
			pending.pResource->SetResourceState(pNextContext->GetResourceState(pending.pResource, subResource));
		}
		if (barriers.HasWork())
		{
			if (!pCurrentContext)
			{
				pCurrentContext = m_pGraphics->AllocateCommandContext(m_Type);
				pCurrentContext->Free(m_NextFenceValue);
			}
			barriers.Flush(pCurrentContext->GetCommandList());
		}

		if (pCurrentContext)
		{
			VERIFY_HR_EX(pCurrentContext->GetCommandList()->Close(), m_pGraphics->GetDevice());
			commandLists.push_back((ID3D12CommandList*)pCurrentContext->GetCommandList());
		}
		pCurrentContext = pNextContext;
	}

	VERIFY_HR_EX(pCurrentContext->GetCommandList()->Close(), m_pGraphics->GetDevice());
	commandLists.push_back((ID3D12CommandList*)pCurrentContext->GetCommandList());

	m_pCommandQueue->ExecuteCommandLists((uint32_t)commandLists.size(), commandLists.data());

	std::scoped_lock lock(m_FenceMutex);
	m_pCommandQueue->Signal(m_pFence.Get(), m_NextFenceValue);  

	return m_NextFenceValue++;  
}

bool CommandQueue::IsFenceComplete(uint64_t fenceValue)
{
	if (fenceValue > m_LastCompletedFenceValue)
	{
		m_LastCompletedFenceValue = std::max<uint64_t>(m_LastCompletedFenceValue, m_pFence->GetCompletedValue());
	}

	return fenceValue <= m_LastCompletedFenceValue;
}

void CommandQueue::InsertWaitForFence(uint64_t fenceValue)
{
	CommandQueue* pFenceValueOwner = m_pGraphics->GetCommandQueue((D3D12_COMMAND_LIST_TYPE)(fenceValue >> 56));
	m_pCommandQueue->Wait(pFenceValueOwner->GetFence(), fenceValue);
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

ID3D12CommandAllocator* CommandQueue::RequestAllocator()
{
	uint64_t completedFence = m_pFence->GetCompletedValue();
	return m_pAllocatorPool->GetAllocator(completedFence);
}

void CommandQueue::FreeAllocator(uint64_t fenceValue, ID3D12CommandAllocator* pAllocator)
{
	m_pAllocatorPool->FreeAllocator(pAllocator, fenceValue);
}

void CommandQueue::WaitForFence(uint64_t fenceValue)
{
	if (IsFenceComplete(fenceValue))
	{
		return;
	}

	m_pFence->SetEventOnCompletion(fenceValue, m_pFenceEventHandle);
	DWORD result = WaitForSingleObject(m_pFenceEventHandle, INFINITE);

	if(WAIT_OBJECT_0)
	{
		PIXNotifyWakeFromFenceSignal(m_pFenceEventHandle); // the event was successfully signaled, so notify PIX
	}

	m_LastCompletedFenceValue = fenceValue;
}

void CommandQueue::WaitForIdle()
{
	WaitForFence(IncrementFence());
}
