#pragma once
#include<Windows.h>
#include<assert.h>


namespace MRenderer
{
    class GameTimer
    {
    public:
        GameTimer()
        {
            LARGE_INTEGER frequency;
            assert(QueryPerformanceFrequency(&frequency));
            mPerformanceFrequency = 1.0F / frequency.QuadPart;
        }

        float DeltaTime() const
        {
            return mDeltaTime;
        }

        float TotalTime() const
        {
            return mTotalTime;
        }

        void Pause(bool state)
        {
            mPaused = state;

            LARGE_INTEGER counter;
            assert(QueryPerformanceCounter(&counter));
            mLastCounter = counter.QuadPart;
        }

        void Tick()
        {
            LARGE_INTEGER counter;
            assert(QueryPerformanceCounter(&counter));

            mDeltaTime = max((counter.QuadPart - mLastCounter) * mPerformanceFrequency, 0);
            mTotalTime += mDeltaTime;
            mLastCounter = counter.QuadPart;
        }

    private:
        float mPerformanceFrequency;
        float mDeltaTime = 0.0F;
        float mTotalTime = 0.0F;

        INT64 mLastCounter = 0;
        bool mPaused = false;
    };
}