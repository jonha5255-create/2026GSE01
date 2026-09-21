#pragma once
#include <chrono>
#include <cstdint>

// Single render thread. Counts actual GL draw submissions, not actors or triangles.
class FrameProfiler
{
  public:
    using Clock = std::chrono::steady_clock;
    static FrameProfiler& Get();
    void StartLogging();
    void BeginFrame();
    void RecordDrawCall();
    void EndFrame();

    uint64_t DrawCalls() const
    {
        return drawCalls_;
    }

  private:
    bool logging_ = false;
    Clock::time_point windowStart_ = Clock::now();
    uint64_t drawCalls_ = 0, frames_ = 0, totalDrawCalls_ = 0, minDrawCalls_ = UINT64_MAX,
             maxDrawCalls_ = 0;
};
