#include "stdafx.h"
#include "FramePacer.h"
#include <algorithm>
#include <cstdint>
#include <thread>

FramePacer::FramePacer()
{
    timer_ = CreateWaitableTimerExW(
        nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_MODIFY_STATE | SYNCHRONIZE);
    if (!timer_)
    {
        timer_ = CreateWaitableTimerExW(nullptr, nullptr, 0, TIMER_MODIFY_STATE | SYNCHRONIZE);
    }
}

FramePacer::~FramePacer()
{
    if (timer_)
    {
        CloseHandle(timer_);
    }
}

void FramePacer::Reset()
{
    next_ = Clock::now();
}

double FramePacer::Wait(int framesPerSecond)
{
    auto start = Clock::now();
    if (framesPerSecond <= 0)
    {
        next_ = start;
        return 0;
    }
    next_ += std::chrono::duration_cast<Clock::duration>(
        std::chrono::duration<double>(1.0 / framesPerSecond));
    if (next_ <= start)
    {
        next_ = start;
        return 0;
    }
    while (Clock::now() < next_)
    {
        LARGE_INTEGER due;
        auto remaining =
            std::chrono::duration_cast<std::chrono::nanoseconds>(next_ - Clock::now()).count();
        if (remaining <= 0)
        {
            break;
        }
        due.QuadPart = -std::max<int64_t>(1, remaining / 100);
        if (timer_ && SetWaitableTimer(timer_, &due, 0, nullptr, nullptr, FALSE))
        {
            if (WaitForSingleObject(timer_, INFINITE) == WAIT_OBJECT_0)
            {
                continue;
            }
        }
        std::this_thread::sleep_until(next_);
    }
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}
