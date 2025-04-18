#pragma once
#include "stdafx.h"
#include <mutex>

class CommandAllocatorPool;

enum class CommandQueueType
{
	Graphics,
	Compute,
	Copy,
	MAX
};

class CommandQueue
{
private:
	std::unique_ptr<CommandAllocatorPool> m_pAllocatorPool;

	ComPtr<ID3D12CommandQueue> m_pCommandQueue;
	D3D12_COMMAND_LIST_TYPE m_Type;
	std::mutex m_FenceMutex;
	std::mutex m_EventMutex;

	UINT64 m_NextFenceValue = 0;
	UINT64 m_LastCompletedFenceValue = 0;

	ComPtr<ID3D12Fence> m_pFence;
	HANDLE m_pFenceEventHandle = nullptr;
};