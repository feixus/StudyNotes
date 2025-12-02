#include "stdafx.h"
#include "CommandQueue.h"
#include "Graphics.h"
#include "CommandContext.h"

#include "pix3.h"

Fence::Fence(GraphicsDevice* pGraphicsDevice, uint64_t fenceValue, const char* pName)
	: m_CurrentValue(fenceValue), m_LastCompleted(0), m_LastSignaled(0)
{
	ID3D12Device* pDevice = pGraphicsDevice->GetDevice();
	VERIFY_HR_EX(pDevice->CreateFence(m_LastCompleted, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(m_pFence.GetAddressOf())), pDevice);
	D3D::SetObjectName(m_pFence.Get(), pName);
	m_CompleteEvent = CreateEventExA(nullptr, "Fence Event", 0, EVENT_ALL_ACCESS);
}

Fence::~Fence()
{
	CloseHandle(m_CompleteEvent);
}

uint64_t Fence::Signal(CommandQueue* pQueue)
{
	pQueue->GetCommandQueue()->Signal(m_pFence.Get(), m_CurrentValue);
	m_LastSignaled = m_CurrentValue;
	m_CurrentValue++;
	return m_LastSignaled;
}

void Fence::GpuWait(CommandQueue* pQueue, uint64_t fenceValue)
{
	VERIFY_HR(pQueue->GetCommandQueue()->Wait(m_pFence.Get(), fenceValue));
}

void Fence::CpuWait(uint64_t fenceValue)
{
	if (IsComplete(fenceValue))
	{
		return;
	}

	std::lock_guard lockGuard(m_FenceWaitCS);

	m_pFence->SetEventOnCompletion(fenceValue, m_CompleteEvent);
	DWORD result = WaitForSingleObject(m_CompleteEvent, INFINITE);

#if USE_PIX
	if(result == WAIT_OBJECT_0)
	{
		PIXNotifyWakeFromFenceSignal(m_CompleteEvent); // the event was successfully signaled, so notify PIX
	}
#else
	UNREFERENCED_PARAMETER(result);
#endif

	m_LastCompleted = fenceValue;
}

bool Fence::IsComplete(uint64_t fenceValue)
{
	if (fenceValue <= m_LastCompleted)
	{
		return true;
	}

	m_LastCompleted = Math::Max(m_LastCompleted, m_pFence->GetCompletedValue());
	return fenceValue <= m_LastCompleted;
}


CommandQueue::CommandQueue(GraphicsDevice* pGraphicsDevice, D3D12_COMMAND_LIST_TYPE type)
	: GraphicsObject(pGraphicsDevice), m_Type(type)
{
	D3D12_COMMAND_QUEUE_DESC desc = {};
	desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	desc.NodeMask = 0;
	desc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
	desc.Type = type;

	m_pFence = std::make_unique<Fence>(pGraphicsDevice, (uint64_t)type << 56, "CommandQueue Fence");

	VERIFY_HR_EX(pGraphicsDevice->GetDevice()->CreateCommandQueue(&desc, IID_PPV_ARGS(m_pCommandQueue.GetAddressOf())), GetGraphics()->GetDevice());
	D3D::SetObjectName(m_pCommandQueue.Get(), "Main CommandQueue");
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

	CommandContext* pBarrierCommandlist = nullptr;
	CommandContext* pCurrentContext = nullptr;
	for (uint32_t i = 0; i < numContexts; i++)
	{
		CommandContext* pNextContext = pCommandContexts[i];
		check(pNextContext);

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
				pBarrierCommandlist = GetGraphics()->AllocateCommandContext(m_Type);
				pCurrentContext = pBarrierCommandlist;
			}
			
			barriers.Flush(pCurrentContext->GetCommandList());
		}

		if (pCurrentContext)
		{
			VERIFY_HR_EX(pCurrentContext->GetCommandList()->Close(), GetGraphics()->GetDevice());
			commandLists.push_back((ID3D12CommandList*)pCurrentContext->GetCommandList());
		}
		pCurrentContext = pNextContext;
	}

	VERIFY_HR_EX(pCurrentContext->GetCommandList()->Close(), GetGraphics()->GetDevice());
	commandLists.push_back((ID3D12CommandList*)pCurrentContext->GetCommandList());

	m_pCommandQueue->ExecuteCommandLists((uint32_t)commandLists.size(), commandLists.data());

	std::scoped_lock lock(m_FenceMutex);
	uint64_t fenceValue = m_pFence->Signal(this);
	if (pBarrierCommandlist)
	{
		pBarrierCommandlist->Free(fenceValue);
	}
	return fenceValue;  
}

ID3D12CommandAllocator* CommandQueue::RequestAllocator()
{
	std::scoped_lock lock(m_AllocationMutex);
	
	if (!m_FreeAllocators.empty())
	{
		std::pair<ID3D12CommandAllocator*, uint64_t>& pFirst = m_FreeAllocators.front();
		if (m_pFence->IsComplete(pFirst.second))
		{
			m_FreeAllocators.pop();
			pFirst.first->Reset();
			return pFirst.first;
		}
	}

	ComPtr<ID3D12CommandAllocator> pAllocator;
	GetGraphics()->GetDevice()->CreateCommandAllocator(m_Type, IID_PPV_ARGS(pAllocator.GetAddressOf()));
	D3D::SetObjectName(pAllocator.Get(), Sprintf("Pooled Allocator %d - %s", (int)m_CommandAllocators.size(), D3D::CommandlistTypeToString(m_Type)).c_str());
	m_CommandAllocators.push_back(std::move(pAllocator));
	return m_CommandAllocators.back().Get();
}

void CommandQueue::FreeAllocator(uint64_t fenceValue, ID3D12CommandAllocator* pAllocator)
{
	std::scoped_lock lock(m_AllocationMutex);
	m_FreeAllocators.push(std::pair<ID3D12CommandAllocator*, uint64_t>(pAllocator, fenceValue));
}

void CommandQueue::WaitForFence(uint64_t fenceValue)
{
	m_pFence->CpuWait(fenceValue);
}

void CommandQueue::WaitForIdle()
{
	uint64_t fenceValue = m_pFence->Signal(this);
	m_pFence->CpuWait(fenceValue);
}
