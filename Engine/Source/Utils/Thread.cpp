#include "Utils/Thread.h"

namespace MRenderer 
{
    void TaskQueue::TaskWorker(TaskQueue* owner)
    {
        while (!owner->mClosed)
        {
            std::unique_lock guard(owner->mMutexTask);
            while (owner->mTasks.empty())
            {
                owner->mEventNewTask.wait(guard);

                if (owner->mClosed)
                {
                    return;
                }
            }

            auto task = owner->mTasks.front();
            owner->mTasks.pop();
            guard.unlock();

            task();
        }
    }

    ThreadPool::ThreadPool(size_t num_thread)
    {
        mThreads.resize(num_thread);
        for (size_t i = 0; i < num_thread; i++)
        {
            mThreads[i] = std::thread(TaskWorker, this);
        }
    }

    ThreadPool::~ThreadPool()
    {
        std::unique_lock guard(mMutexTask);

        std::queue<Task>().swap(mTasks);
        mClosed = true;

        guard.unlock();

        // block current thread until workers finish their job
        mEventNewTask.notify_all();

        for (size_t i = 0; i < mThreads.size(); i++)
        {
            if (mThreads[i].joinable())
            {
                mThreads[i].join();
            }
        }
    }


    TaskThread::TaskThread()
    {
        mThread = std::thread(TaskWorker, this);
    }

    TaskThread::~TaskThread()
    {
        std::unique_lock guard(mMutexTask);

        std::queue<Task>().swap(mTasks);
        mClosed = true;

        guard.unlock();

        // block current thread until current task is done
        mEventNewTask.notify_one();
        mThread.join();
    }

    TaskScheduler::TaskScheduler()
        :mWorkerThreads(std::thread::hardware_concurrency())
    {
    }
}