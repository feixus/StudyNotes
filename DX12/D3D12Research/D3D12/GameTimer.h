#pragma once

class GameTimer
{
public:
	GameTimer();
	~GameTimer();

	static float GameTime();
	static float DeltaTime();

	static void Reset();
	static void Start();
	static void Stop();
	static void Tick();
	static bool IsPaused() { return m_IsStopped; }
	static int Ticks() { return m_Ticks; }

private:
	inline static double m_SecondsPerCount{ 0.0f };
	inline static double m_DeltaTime{ 0.0f };

	inline static __int64 m_BaseTime{ 0 };
	inline static __int64 m_PausedTime{ 0 };
	inline static __int64 m_StopTime{0};
	inline static __int64 m_PrevTime{0};
	inline static __int64 m_CurrTime{0};

	inline static bool m_IsStopped{false};

	inline static int m_Ticks{0};
};
