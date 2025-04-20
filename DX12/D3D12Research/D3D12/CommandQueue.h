#pragma once

class CommandAllocatorPool;
class CommandContext;

class CommandQueue
{
public:
	CommandQueue(ID3D12Device* pDevice, D3D12_COMMAND_LIST_TYPE type);
	~CommandQueue();

	uint64_t ExecuteCommandList(CommandContext* pCommandList, bool waitForCompletion = false);
	ID3D12CommandQueue* GetCommandQueue() const { return m_pCommandQueue.Get(); }

	bool IsFenceComplete(uint64_t fenceValue);
	
	void InsertWait(uint64_t fenceValue);
	void InsertWaitForQueueFence(CommandQueue* pCommandQueue, uint64_t fenceValue);
	void InsertWaitForQueue(CommandQueue* pCommandQueue);
	uint64_t IncrementFence();
	void WaitForFenceBlock(uint64_t fenceValue);
	void WaitForIdle();

	uint64_t PollCurrentFenceValue();
	uint64_t GetLastCompletedFence() const { return m_LastCompletedFenceValue; }
	uint64_t GetNextFenceValue() const { return m_NextFenceValue; }
	ID3D12Fence* GetFence() const { return m_pFence.Get(); }

	CommandAllocatorPool* GetAllocatorPool() const { return m_pAllocatorPool.get(); };

private:
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