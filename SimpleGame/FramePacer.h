#pragma once
#include <chrono>
#include <windows.h>

// Limits work without relying on a driver's blocking SwapBuffers interval.
class FramePacer
{
  public:
    FramePacer();
    ~FramePacer();
    FramePacer(const FramePacer&) = delete;
    FramePacer& operator=(const FramePacer&) = delete;
    void Reset();
    double Wait(int framesPerSecond);

  private:
    using Clock = std::chrono::steady_clock;
    Clock::time_point next_ = Clock::now();
    HANDLE timer_ = nullptr;
};
