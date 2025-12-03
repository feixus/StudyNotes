#pragma once
#include "GraphicsResource.h"

class CommandContext;
class CommandQueue;

class Fence : public GraphicsObject
{
public:
	Fence(GraphicsDevice* pGraphicsDevice, uint64_t fenceValue, const char* pName);
	~Fence();

	// Signals on the GPU timeline, increments the current value and return the signaled value.
	uint64_t Signal(CommandQueue* pQueue);

	// Inserts a wait on the GPU timeline.
	void GpuWait(CommandQueue* pQueue, uint64_t fenceValue);

	// Stall CPU until fence value is signaled on the GPU.
	void CpuWait(uint64_t fenceValue);

	// Returns true if the fence value reached value or higher.
	bool IsComplete(uint64_t fenceValue);

	uint64_t GetCurrentValue() const { return m_CurrentValue; }
	uint64_t GetLastSignaledValue() const { return m_LastSignaled; }

	inline ID3D12Fence* GetFence() const { return m_pFence.Get(); }

private:
	ComPtr<ID3D12Fence> m_pFence;
	std::mutex m_FenceWaitCS;
	HANDLE m_CompleteEvent;
	uint64_t m_CurrentValue{0};
	uint64_t m_LastSignaled{0};
	uint64_t m_LastCompleted{0};
};

class CommandQueue : public GraphicsObject
{
public:
	CommandQueue(GraphicsDevice* pGraphicsDevice, D3D12_COMMAND_LIST_TYPE type);

	uint64_t ExecuteCommandList(CommandContext** pCommandContexts, uint32_t numContexts);
	ID3D12CommandQueue* GetCommandQueue() const { return m_pCommandQueue.Get(); }

	// block on the CPU side
	void WaitForFence(uint64_t fenceValue);
	void WaitForIdle();

	Fence* GetFence() const { return m_pFence.get(); }
	D3D12_COMMAND_LIST_TYPE GetType() const { return m_Type; }

	ID3D12CommandAllocator* RequestAllocator();
	void FreeAllocator(uint64_t fenceValue, ID3D12CommandAllocator* pAllocator);

private:
	ComPtr<ID3D12GraphicsCommandList> m_pTransitionCommandList;

	std::vector<ComPtr<ID3D12CommandAllocator>> m_CommandAllocators;
	std::queue<std::pair<ID3D12CommandAllocator*, uint64_t>> m_FreeAllocators;
	std::mutex m_AllocationMutex;

	ComPtr<ID3D12CommandQueue> m_pCommandQueue;
	std::mutex m_FenceMutex;
	std::unique_ptr<Fence> m_pFence;

	D3D12_COMMAND_LIST_TYPE m_Type;
};
