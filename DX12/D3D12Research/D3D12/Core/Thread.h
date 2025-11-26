#pragma once

class Thread
{
public:
    using ThreadFunction = DWORD(__stdcall *)(void*);

    Thread() = default;
    ~Thread();

    bool SetPriority(const int priority);
    void SetAffinity(const uint64_t affinity);
    void LockToCore(const uint32_t core);
    void SetName(const char* name);

    static void SetCurrentAffinity(const uint64_t affinity);
    static void LockCurrentToCore(const uint32_t core);

    unsigned long GetId() const { return m_Thread; }
    bool IsCurrentThread() const { return GetId() == GetCurrentId();}
    static uint32_t GetCurrentId();
    bool IsRunning() const { return m_pHandle != nullptr; }

    static void SetMainThread();
    static bool IsMainThread();
    static bool IsMainThread(uint32_t threadId);

    bool RunThread(ThreadFunction function, void* pArgs);
    void StopThread();

private:
    static void SetAffinity(void* pHandle, const uint64_t affinity);

    inline static unsigned int m_MainThread{0};
    uint32_t m_Thread{0};
    void* m_pHandle{nullptr};
};