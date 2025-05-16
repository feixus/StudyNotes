#pragma once

class CommandAllocatorPool;
class Graphics;

class CommandQueue
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
	Graphics* m_pGraphics;
	std::unique_ptr<CommandAllocatorPool> m_pAllocatorPool;

	ComPtr<ID3D12CommandQueue> m_pCommandQueue;
	D3D12_COMMAND_LIST_TYPE m_Type;
	std::mutex m_FenceMutex;
	std::mutex m_EventMutex;

	uint64_t m_NextFenceValue = 0;
	uint64_t m_LastCompletedFenceValue = 0;

	ComPtr<ID3D12Fence> m_pFence;
	HANDLE m_pFenceEventHandle = nullptr;
};