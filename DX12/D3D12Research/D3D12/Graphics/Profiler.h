#pragma once
#include "Graphics.h"

class ReadbackBuffer;
class CommandContext;

class CpuTimer
{
public:
	void Begin();
	void End();
	float GetTime(float ticksPerSecond) const;

private:
	int64_t m_StartTime;
	int64_t m_EndTime;
};

class GpuTimer
{
public:
	GpuTimer();
	void Begin(const char* pName, CommandContext* pContext);
	void End(CommandContext* pContext);
	float GetTime(float ticksPerSecond) const;
	int GetTimerIndex() const { return m_TimerIndex; }

private:
	int m_TimerIndex = 0;
};

class Profiler
{
private:
	struct Block
	{
		Block() : pParent(nullptr), Name{}
		{}
		Block(const char* pName, Block* pParent) : pParent(pParent)
		{
			strcpy_s(Name, pName);
		}

		CpuTimer CpuTimer{};
		GpuTimer GpuTimer{};
		char Name[128];
		Block* pParent{nullptr};
		std::deque<std::unique_ptr<Block>> Children;
	};

public:
	static Profiler* Instance();

	void Initialize(Graphics* pGraphics);

	void Begin(const char* pName, CommandContext* context);
	void End(CommandContext* context);
	
	void BeginReadback(int frameIndex);
	void EndReadBack(int frameIndex);

	int32_t GetNextTimerIndex();

	inline const uint64_t* GetData() const { return m_pCurrentReadBackData; }

private:
	Profiler() = default;

	constexpr static int HEAP_SIZE = 512;

	std::array<uint64_t, Graphics::FRAME_COUNT> m_FenceValues{};
	uint64_t* m_pCurrentReadBackData{nullptr};

	Graphics* m_pGraphics{nullptr};
	float m_SecondsPerGpuTick{ 0.f };
	float m_SecondsPerCpuTick{ 0.f };
	int m_CurrentTimer{0};
	int m_LastFrameTimer{ 0 };
	ComPtr<ID3D12QueryHeap> m_pQueryHeap;
	std::unique_ptr<ReadbackBuffer> m_pReadBackBuffer;

	std::unique_ptr<Block> m_pRootBlock;
	Block* m_pCurrentBlock{nullptr};
};