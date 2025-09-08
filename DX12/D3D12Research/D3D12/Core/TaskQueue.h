#pragma once
#include <atomic>

struct AsyncTask;

struct TaskDistributeArgs
{
    int JobIndex;
    int ThreadIndex;
};

DECLARE_DELEGATE(AsyncTaskDelegate, int);
DECLARE_DELEGATE(AsyncDistributeDelegate, TaskDistributeArgs);

using TaskContext = std::atomic<uint32_t>;

class TaskQueue
{
public:
    TaskQueue() = delete;
    ~TaskQueue();

    static void Initialize(uint32_t threads);
    static void Shutdown();
    
    template<typename Callback>
    static void Execute(Callback&& action, TaskContext& context)
    {
        AddWorkItem(AsyncTaskDelegate::CreateLambda(std::forward<Callback>(action)), context);
    }

    template<typename Callback>
    static void ExecuteMany(Callback&& action, TaskContext& context, uint32_t count, int32_t groupSize = -1)
    {
        Distribute(context, AsyncDistributeDelegate::CreateLambda(std::forward<Callback>(action)), count, groupSize);
    }

    static void Join(TaskContext& context);
    static uint32_t ThreadCount();

private:
    static void Distribute(TaskContext& context, const AsyncDistributeDelegate& action, uint32_t count, int32_t groupSize = -1);
    static void AddWorkItem(const AsyncTaskDelegate& action, TaskContext& context);
    static void CreateThreads(size_t count);
};