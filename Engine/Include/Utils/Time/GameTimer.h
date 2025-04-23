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
            kPerformanceFrequency = 1.0F / frequency.QuadPart;
        }

        double DeltaTime() const
        {
            return delta_time_;
        }

        double TotalTime() const
        {
            return total_time_;
        }

        void Pause(bool state)
        {
            paused_ = state;

            LARGE_INTEGER counter;
            assert(QueryPerformanceCounter(&counter));
            last_counter_ = counter.QuadPart;
        }

        void Tick()
        {

            LARGE_INTEGER counter;
            assert(QueryPerformanceCounter(&counter));

            delta_time_ = max((counter.QuadPart - last_counter_) * kPerformanceFrequency, 0);
            total_time_ += delta_time_;

            last_counter_ = counter.QuadPart;
        }

    public:
        double kPerformanceFrequency;

    private:
        double delta_time_ = 0.0F;
        double total_time_ = 0.0F;

        INT64 last_counter_ = 0;
        bool paused_ = false;
    };
}