#pragma once
#include "Graphics/Core/Graphics.h"

class Buffer;
class CommandContext;
class SwapChain;

#define GPU_PROFILE_BEGIN(name, cmdlist) Profiler::Get()->Begin(name, cmdlist);
#define GPU_PROFILE_END(cmdlist) Profiler::Get()->End(cmdlist);

#define PROFILE_BEGIN(name) Profiler::Get()->Begin(name, nullptr);
#define PROFILE_END() Profiler::Get()->End();

#define GPU_PROFILE_SCOPE(name, cmdlist) ScopeProfiler MACRO_CONCAT(profiler, __COUNTER__)(name, cmdlist); OPTICK_GPU_EVENT(name);
#define PROFILE_SCOPE(name) ScopeProfiler MACRO_CONCAT(profiler, __COUNTER__)(name, nullptr); OPTICK_EVENT_DYNAMIC(name);

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
	float GetTime(const uint64_t* pReadbackData) const;

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

	void PopulateTimes(const uint64_t* pReadbackData, int frameIndex);
	void RenderImGui(int frameIndex);

	bool HashChild(const char* pName);
	ProfileNode* GetChild(const char* pName, int i = -1);
	ProfileNode* GetParent() const { return m_pParent; }
	uint32_t GetChildCount() const { return (uint32_t)m_Children.size(); }
	const ProfileNode* GetChild(uint32_t index) const { return m_Children[index].get(); }

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
	static Profiler* Get();

	void Initialize(GraphicsDevice* pGraphicsDevice);

	void Begin(const char* pName, CommandContext* context = nullptr);
	void End(CommandContext* context = nullptr);
	
	void Resolve(SwapChain* pSwapchain, GraphicsDevice* pGraphicsDevice, int frameIndex);

	float GetGpuTime(const uint64_t* pReadbackData, int timerIndex) const;
	void StartGpuTimer(CommandContext* pContext, int timerIndex);
	void StopGpuTimer(CommandContext* pContext, int timerIndex);

	int32_t GetNextTimerIndex();
	float GetSecondsPerGpuTick() const { return m_SecondsPerGpuTick; }
	float GetSecondsPerCpuTick() const { return m_SecondsPerCpuTick; }
	ID3D12QueryHeap* GetQueryHeap() const { return m_pQueryHeap.Get(); }
	ProfileNode* GetRootNode() const { return m_pRootBlock.get(); }

private:
	Profiler() = default;

	constexpr static int MAX_GPU_TIME_QUERIES = 512;
	constexpr static int QUERY_PAIR_NUM = 2;
	constexpr static int HEAP_SIZE = MAX_GPU_TIME_QUERIES * QUERY_PAIR_NUM;

	std::array<uint64_t, Graphics::FRAME_COUNT> m_FenceValues{};

	float m_SecondsPerGpuTick{ 0.f };
	float m_SecondsPerCpuTick{ 0.f };
	int m_CurrentTimer{0};
	int m_LastTimer{0};
	int m_CurrentReadbackFrame{0};
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
		Profiler::Get()->Begin(pName, pContext);
	}

	~ScopeProfiler()
	{
		Profiler::Get()->End(pContext);
	}

	CommandContext* pContext;
};

class CpuScopeProfiler
{
};
