#pragma once
#include "Graphics/Core/Graphics.h"

class Buffer;
class CommandContext;

#define GPU_PROFILE_BEGIN(name, cmdlist) Profiler::Instance()->Begin(name, cmdlist);
#define GPU_PROFILE_END(cmdlist) Profiler::Instance()->End(cmdlist);

#define PROFILE_BEGIN(name) Profiler::Instance()->Begin(name, nullptr);
#define PROFILE_END() Profiler::Instance()->End();

#define GPU_PROFILE_SCOPE(name, cmdlist) ScopeProfiler profiler ## __COUNTER__(name, cmdlist);
#define PROFILE_SCOPE(name) ScopeProfiler profiler ## __COUNTER__(name, nullptr);

class CpuTimer
{
public:
	void Begin();
	void End();
	float GetTime() const;

private:
	int64_t m_StartTime{0};
	float m_TotalTime{0};
};

class GpuTimer
{
public:
	GpuTimer();
	void Begin(CommandContext* pContext);
	void End(CommandContext* pContext);
	float GetTime() const;

private:
	int m_TimerIndex{-1};
};

template<typename T, size_t SIZE>
class TimeHistory
{
public:
	TimeHistory() = default;
	~TimeHistory() = default;

	void AddTime(T time)
	{
		m_History[m_Entries % SIZE] = time;
		++m_Entries;
	}

	T GetAverage() const
	{
		T average = 0;
		uint32_t count = Math::Min<uint32_t>(m_Entries, SIZE);
		for (uint32_t i = 0; i < count; i++)
		{
			average += m_History[i];
		}
		return average / count;
	}

	T GetLast() const
	{
		return m_History[m_Entries % SIZE];
	}

	T GetMax() const
	{
		T max = (T)0;
		uint32_t count = Math::Min<uint32_t>(m_Entries, SIZE);
		for (uint32_t i = 0; i < count; i++)
		{
			max = Math::Max(max, m_History[i]);
		}
		return max;
	}

	void GetHistory(const T** pData, uint32_t* count, uint32_t* dataOffset) const
	{
		*count = Math::Min<uint32_t>(m_Entries, SIZE);
		*dataOffset = m_Entries % SIZE;
		*pData = m_History.data();
	}

private:
	uint32_t m_Entries{0};
	std::array<T, SIZE> m_History{};
};

class ProfileNode
{
public:
	ProfileNode(const char* pName, StringHash hash, ProfileNode* pParent)
		: m_Hash(hash), m_pParent(pParent)
	{
		strcpy_s(m_Name, pName);
	}

	void StartTimer(CommandContext* pContext);
	void EndTimer(CommandContext* pContext);

	void PopulateTimes(int frameIndex);
	void RenderImGui(int frameIndex);

	bool HashChild(const char* pName);
	ProfileNode* GetChild(const char* pName, int i = -1);
	ProfileNode* GetParent() const { return m_pParent; }
	size_t GetChildCount() const { return m_Children.size(); }
	const ProfileNode* GetChild(int index) const { return m_Children[index].get(); }

private:
	void RenderNodeImGui(int frameIndex);

	bool m_Processed{true};
	CpuTimer m_CpuTimer{};
	GpuTimer m_GpuTimer{};
	TimeHistory<float, 128> m_CpuTimeHistory;
	TimeHistory<float, 128> m_GpuTimeHistory;

	int m_LastProcessedFrame{-1};
	char m_Name[128];
	StringHash m_Hash;
	ProfileNode* m_pParent{nullptr};
	std::vector<std::unique_ptr<ProfileNode>> m_Children;
	std::unordered_map<StringHash, ProfileNode*> m_Map;
};

class Profiler
{
public:
	static Profiler* Instance();

	void Initialize(Graphics* pGraphics);

	void Begin(const char* pName, CommandContext* context = nullptr);
	void End(CommandContext* context = nullptr);
	
	void BeginReadback(int frameIndex);
	void EndReadBack(int frameIndex);

	float GetGpuTime(int timerIndex) const;
	void StartGpuTimer(CommandContext* pContext, int timerIndex);
	void StopGpuTimer(CommandContext* pContext, int timerIndex);

	int32_t GetNextTimerIndex();
	float GetSecondsPerGpuTick() const { return m_SecondsPerGpuTick; }
	float GetSecondsPerCpuTick() const { return m_SecondsPerCpuTick; }
	inline const uint64_t* GetData() const { return m_pCurrentReadBackData; }
	ID3D12QueryHeap* GetQueryHeap() const { return m_pQueryHeap.Get(); }
	ProfileNode* GetRootNode() const { return m_pRootBlock.get(); }

private:
	Profiler() = default;

	constexpr static int HEAP_SIZE = 512;

	std::array<uint64_t, Graphics::FRAME_COUNT> m_FenceValues{};
	uint64_t* m_pCurrentReadBackData{nullptr};

	Graphics* m_pGraphics{nullptr};
	float m_SecondsPerGpuTick{ 0.f };
	float m_SecondsPerCpuTick{ 0.f };
	int m_CurrentTimer{0};
	int m_LastTimer{0};
	ComPtr<ID3D12QueryHeap> m_pQueryHeap;
	std::unique_ptr<Buffer> m_pReadBackBuffer;

	std::unique_ptr<ProfileNode> m_pRootBlock;
	ProfileNode* m_pPreviousBlock{nullptr};
	ProfileNode* m_pCurrentBlock{nullptr};
};

struct ScopeProfiler
{
	ScopeProfiler(const char* pName, CommandContext* pContext = nullptr)
		: pContext(pContext)
	{
		Profiler::Instance()->Begin(pName, pContext);
	}

	~ScopeProfiler()
	{
		Profiler::Instance()->End(pContext);
	}

	CommandContext* pContext;
};

class CpuScopeProfiler
{
};