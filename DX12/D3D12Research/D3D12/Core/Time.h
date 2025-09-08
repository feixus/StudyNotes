#pragma once

class Time
{
public:
	Time() = default;
	~Time() = default;

	static float TotalTime();
	static float DeltaTime();

	static void Reset();
	static void Start();
	static void Stop();
	static void Tick();
	static bool IsPaused() { return m_IsStopped; }
	static int Ticks() { return m_Ticks; }

	static void CounterBegin();
	static double CounterEnd();

private:
	inline static double m_SecondsPerCount{0.0f};
	inline static double m_DeltaTime{0.0f};

	inline static __int64 m_BaseTime{0};
	inline static __int64 m_PausedTime{0};
	inline static __int64 m_StopTime{0};
	inline static __int64 m_PrevTime{0};
	inline static __int64 m_CurrTime{0};

	inline static double m_SecondsPerCountInCounter{0.0f};
	inline static __int64 m_CounterBeginTime{0};
	inline static __int64 m_CounterEndTime{0};

	inline static bool m_IsStopped{false};

	inline static int m_Ticks{0};
};
