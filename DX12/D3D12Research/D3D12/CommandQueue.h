#pragma once

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
public:
	CommandQueue(ID3D12Device* pDevice, CommandQueueType type);
	~CommandQueue();

	UINT64 ExecuteCommandList(ID3D12GraphicsCommandList* pCommandList);
	ID3D12CommandQueue* GetCommandQueue() const { return m_pCommandQueue.Get(); }

	bool IsFenceComplete(UINT64 fenceValue);
	
	void InsertWait(UINT64 fenceValue);
	void InsertWaitForQueueFence(CommandQueue* pCommandQueue, UINT64 fenceValue);
	void InsertWaitForQueue(CommandQueue* pCommandQueue);
	
	void WaitForFenceBlock(UINT64 fenceValue);
	void WaitForIdle();

	UINT64 PollCurrentFenceValue();
	UINT64 GetLastCompletedFence() const { return m_LastCompletedFenceValue; }
	UINT64 GetNextFenceValue() const { return m_NextFenceValue; }
	ID3D12Fence* GetFence() const { return m_pFence.Get(); }

	ID3D12CommandAllocator* GetAllocator();

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