#include "stdafx.h"
#include "Time.h"

float Time::TotalTime()
{
	if (m_IsStopped)
	{
		return (float)(((m_StopTime - m_PausedTime) - m_BaseTime) * m_SecondsPerCount);
	}

	return (float)(((m_CurrTime - m_PausedTime) - m_BaseTime) * m_SecondsPerCount);
}

float Time::DeltaTime()
{
	return (float)m_DeltaTime;
}

void Time::Reset()
{
	__int64 countsPerSec;
	QueryPerformanceFrequency((LARGE_INTEGER*)&countsPerSec);
	m_SecondsPerCount = 1.0f / (double)countsPerSec;

	__int64 curTime;
	QueryPerformanceCounter((LARGE_INTEGER*)&curTime);
	m_BaseTime = curTime;
	m_PrevTime = curTime;
	m_StopTime = 0;
	m_IsStopped = false;
}

void Time::Start()
{
	__int64 startTime;
	QueryPerformanceCounter((LARGE_INTEGER*)&startTime);

	if (m_IsStopped)
	{
		m_PausedTime += (startTime - m_StopTime);
		m_PrevTime = startTime;

		m_StopTime = 0;
		m_IsStopped = false;
	}
}

void Time::Stop()
{
	if (!m_IsStopped)
	{
		__int64 curTime;
		QueryPerformanceCounter((LARGE_INTEGER*)&curTime);
		m_StopTime = curTime;
		m_IsStopped = true;
	}
}

void Time::Tick()
{
	if (m_IsStopped)
	{
		m_DeltaTime = 0.0f;
		return;
	}

	++m_Ticks;

	__int64 curTime;
	QueryPerformanceCounter((LARGE_INTEGER*)&curTime);
	m_DeltaTime = (curTime - m_PrevTime) * m_SecondsPerCount;
	m_CurrTime = curTime;
	m_PrevTime = m_CurrTime;

	if (m_DeltaTime < 0.0f)
	{
		m_DeltaTime = 0.0f;
	}
}

void Time::CounterBegin()
{
	__int64 countsPerSec;
	QueryPerformanceFrequency((LARGE_INTEGER*)&countsPerSec);
	m_SecondsPerCountInCounter = 1.0f / (double)countsPerSec;

	QueryPerformanceCounter((LARGE_INTEGER*)&m_CounterBeginTime);
}

double Time::CounterEnd()
{
	QueryPerformanceCounter((LARGE_INTEGER*)&m_CounterEndTime);

	return (m_CounterEndTime - m_CounterBeginTime) * m_SecondsPerCountInCounter;
}
