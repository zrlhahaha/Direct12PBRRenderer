#pragma once
#include<Windows.h>
#include<assert.h>


namespace MRenderer
{
    class GameTimer
    {
    public:
        GameTimer()
            :mPerformanceFrequency(0.0F), mBaseCounter(0), mDeltaCounter(0), mLastCounter(0)
        {
            LARGE_INTEGER frequency;
            assert(QueryPerformanceFrequency(&frequency));
            mPerformanceFrequency = 1.0F / frequency.QuadPart;

            LARGE_INTEGER counter;
            assert(QueryPerformanceCounter(&counter));
            mBaseCounter = mLastCounter = counter.QuadPart;
        }

        float DeltaTime() const
        {
            return mDeltaCounter * mPerformanceFrequency;
        }

        float TotalTime() const
        {
            return (mLastCounter - mBaseCounter) * mPerformanceFrequency;
        }

        void Tick()
        {
            LARGE_INTEGER counter;
            assert(QueryPerformanceCounter(&counter));

            mDeltaCounter = Max(counter.QuadPart - mLastCounter, 0);
            mLastCounter = counter.QuadPart;

            // Log(TotalTime());
        }

    private:
        float mPerformanceFrequency;
        INT64 mBaseCounter;
        INT64 mDeltaCounter;
        INT64 mLastCounter;
    };
}