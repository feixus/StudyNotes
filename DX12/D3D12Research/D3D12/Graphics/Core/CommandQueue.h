#pragma once
#include "GraphicsResource.h"

class Graphics;

class CommandAllocatorPool : public GraphicsObject
{
public:
	CommandAllocatorPool(Graphics* pGraphics, D3D12_COMMAND_LIST_TYPE type);

	ID3D12CommandAllocator* GetAllocator(uint64_t fenceValue);
	void FreeAllocator(ID3D12CommandAllocator* pAllocator, uint64_t fenceValue);

private:
	std::vector<ComPtr<ID3D12CommandAllocator>> m_CommandAllocators;
	std::queue<std::pair<ID3D12CommandAllocator*, uint64_t>> m_FreeAllocators;

	D3D12_COMMAND_LIST_TYPE m_Type;
};

class CommandQueue : public GraphicsObject
{
public:
	CommandQueue(Graphics* pGraphics, D3D12_COMMAND_LIST_TYPE type);
	~CommandQueue();

	uint64_t ExecuteCommandList(ID3D12CommandList* pCommandList);
	ID3D12CommandQueue* GetCommandQueue() const { return m_pCommandQueue.Get(); }

	// inserts a stall/wait in the queue so it blocks the GPU
	void InsertWaitForFence(uint64_t fenceValue);
	void InsertWaitForQueue(CommandQueue* pCommandQueue);

	// block on the CPU side
	void WaitForFence(uint64_t fenceValue);
	void WaitForIdle();

	bool IsFenceComplete(uint64_t fenceValue);
	uint64_t IncrementFence();

	uint64_t GetLastCompletedFence() const { return m_LastCompletedFenceValue; }
	uint64_t GetNextFenceValue() const { return m_NextFenceValue; }
	ID3D12Fence* GetFence() const { return m_pFence.Get(); }

	ID3D12CommandAllocator* RequestAllocator();
	void FreeAllocator(uint64_t fenceValue, ID3D12CommandAllocator* pAllocator);

private:
	std::unique_ptr<CommandAllocatorPool> m_pAllocatorPool;

	ComPtr<ID3D12CommandQueue> m_pCommandQueue;
	std::mutex m_FenceMutex;
	std::mutex m_EventMutex;

	uint64_t m_NextFenceValue = 0;
	uint64_t m_LastCompletedFenceValue = 0;

	ComPtr<ID3D12Fence> m_pFence;
	HANDLE m_pFenceEventHandle = nullptr;
};