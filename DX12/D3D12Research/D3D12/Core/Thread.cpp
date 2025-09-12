#include "stdafx.h"
#include "Thread.h"

Thread::~Thread()
{
    StopThread();
}

bool Thread::RunThread(ThreadFunction function, void* pArgs)
{
    if (m_pHandle)
    {
        return false;
    }

    m_pHandle = CreateThread(nullptr, 0, function, pArgs, 0, (DWORD*)&m_Thread);
    if (m_pHandle == nullptr)
    {
        auto error = GetLastError();
        E_LOG(Error, "%d", error);
        return false;
    }

    return true;
}

void Thread::StopThread()
{
    if (!m_pHandle)
    {
        return;
    }

    WaitForSingleObject((HANDLE)m_pHandle, INFINITE);
    if (CloseHandle((HANDLE)m_pHandle) == 0)
    {
        auto error = GetLastError();
        E_LOG(Error, "%d", error);
    }
    m_pHandle = nullptr;
}

bool Thread::SetPriority(const int priority)
{
    if (!m_pHandle)
    {
        return false;
    }

    if (SetThreadPriority((HANDLE)m_pHandle, priority) == 0)
    {
        auto error = GetLastError();
        E_LOG(Error, "%d", error);
        return false;
    }

    return true;
}

void Thread::SetAffinity(const uint64_t affinity)
{
    SetAffinity(m_pHandle, affinity);
}

void Thread::SetAffinity(void* pHandle, const uint64_t affinity)
{
    check(pHandle);
    ::SetThreadAffinityMask((HANDLE*)pHandle, (DWORD)affinity);
}

void Thread::LockToCore(const uint32_t core)
{
    uint32_t affinity = 1ull << core;
    SetAffinity(affinity);
}

void Thread::SetCurrentAffinity(const uint64_t affinity)
{
    SetAffinity(GetCurrentThread(), affinity);
}

void Thread::LockCurrentToCore(const uint32_t core)
{
    uint32_t affinity = 1ull << core;
    SetAffinity(GetCurrentThread(), (uint64_t)affinity);
}

uint32_t Thread::GetCurrentId()
{
    return (uint32_t)::GetCurrentThreadId();
}

void Thread::SetMainThread()
{
    m_MainThread = GetCurrentId();
}

bool Thread::IsMainThread()
{
    return GetCurrentId() == m_MainThread;
}

bool Thread::IsMainThread(uint32_t threadId)
{
    return threadId == m_MainThread;
}

void Thread::SetName(const std::string& name)
{
    wchar_t wideName[256];
    ToWidechar(name.c_str(), wideName, 256);
    SetThreadDescription((HANDLE)m_pHandle, wideName);
}

