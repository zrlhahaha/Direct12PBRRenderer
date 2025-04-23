#pragma once
#include <thread>
#include <condition_variable>
#include <vector>
#include <functional>
#include <queue>
#include <future>

#include "Fundation.h"


namespace MRenderer
{

    class TaskQueue
    {
    protected:
        using Task = std::function<void()>;

    public:
        TaskQueue() {};

        // non-copyable
        TaskQueue(const TaskQueue&) = delete;
        TaskQueue& operator=(const TaskQueue&) = delete;

        inline uint32 GetNumTaks()
        { 
            std::lock_guard<std::mutex> lock(mMutexTask);
            return static_cast<uint32>(mTasks.size());
        };

        // schedule function for later execution， @args will be passed by copy or move operation, use std::ref if you want to pass by reference
        template<typename Fn, typename... Args>
        auto Schedule(Fn func, Args&&... args)
        {
            ASSERT(!mClosed);

            using RetureType = std::invoke_result_t<Fn, Args...>;
            using Task = std::packaged_task<RetureType(Args...)>;

            auto ptr = std::make_shared<Task>(func); // packaged_task needs to be alive and unmoved untill the task is done

            while (!mMutexTask.try_lock())
                std::this_thread::yield();// spin lock, for avoid blocking the caller thread

            // we use std::function<void()> to store all the different tasks,
            // so the packaged_task needs to be wrapped in lambda to fit into the std::function<void()> variable
            mTasks.push(
                [=]() mutable
                {
                    (*ptr)(args...);
                }
            );

            mMutexTask.unlock();

            // notify a worker to execute task
            mEventNewTask.notify_all();

            return ptr->get_future();
        }
    
    protected:
        static void TaskWorker(TaskQueue* owner);
    
    protected:
        bool mClosed = false;
        std::queue<Task> mTasks;
        std::mutex mMutexTask;
        std::condition_variable mEventNewTask;
    };


    class ThreadPool : public TaskQueue 
    {
    public:
        ThreadPool(size_t num_thread);
        ~ThreadPool();

        // non-copyable
        ThreadPool(const ThreadPool&) = delete;
        ThreadPool& operator=(const ThreadPool&) = delete;

    protected:
        std::vector<std::thread> mThreads;
    };


    class TaskThread : public TaskQueue 
    {
    public:
        TaskThread();
        ~TaskThread();

        // non-copyable
        TaskThread(const TaskThread&) = delete;
        TaskThread& operator=(const TaskThread&) = delete;

    protected:
        std::thread mThread;
    };

    class TaskScheduler
    {
    protected:
        TaskScheduler();
        TaskScheduler(const TaskScheduler&) = delete;

        TaskScheduler& operator=(const TaskScheduler&) = delete;

    public:
        static TaskScheduler& Instance() 
        {
            static TaskScheduler instance;
            return instance;
        }

        template<typename Fn, typename... Args>
        std::future<std::invoke_result_t<Fn, Args...>> ExecuteOnMainThread(Fn&& func, Args&&... args)
        {
            return mTickThread.Schedule(std::forward<Fn>(func), std::forward<Args>(args)...);
        }

        template<typename Fn, typename... Args>
        std::future<std::invoke_result_t<Fn, Args...>> ExecuteOnRenderThread(Fn&& func, Args&&... args)
        {
            return mDeviceThread.Schedule(std::forward<Fn>(func), std::forward<Args>(args)...);
        }

        template<typename Fn, typename... Args>
        std::future<std::invoke_result_t<Fn, Args...>> ExecuteOnDeviceThread(Fn&& func, Args&&... args)
        {
            return mDeviceThread.Schedule(std::forward<Fn>(func), std::forward<Args>(args)...);
        }

        template<typename Fn, typename... Args>
        void ExecuteOnWorker(Fn&& func, Args&&... args)
        {
            mWorkerThreads.Schedule(std::forward<Fn>(func), std::forward<Args>(args)...);
        }

    private:
        TaskThread mTickThread;
        TaskThread mRenderThread;
        TaskThread mDeviceThread;
        ThreadPool mWorkerThreads;
    };
}