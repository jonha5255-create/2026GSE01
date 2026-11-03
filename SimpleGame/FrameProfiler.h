#pragma once
#include <chrono>
#include <cstdint>
enum class DrawStage
{
    Scene,
    Post,
    UI
};

struct RenderFrameCounters
{
    uint64_t scene = 0, post = 0, ui = 0, objects = 0, triangles = 0, instanceBytes = 0,
             meshBytes = 0;
    uint64_t hits = 0, misses = 0, generations = 0, loads = 0, rejects = 0, writes = 0,
             evictions = 0, splits = 0;
    uint64_t poseBuilds = 0, poseHits = 0;
    double artPoseMs = 0;
    uint64_t gpuModelUploads = 0, gpuModelBytes = 0, gpuModelEntries = 0;
};

class FrameProfiler
{
  public:
    using Clock = std::chrono::steady_clock;
    static FrameProfiler& Get();
    void StartLogging();
    void BeginFrame();
    void RecordDrawCall(DrawStage stage = DrawStage::Post);
    void EndRender();

    void RecordUpdate(double milliseconds)
    {
        pendingUpdateMs_ += milliseconds;
    }

    void EndFrame();

    void RecordPresent(double ms)
    {
        presentMs_ = ms;
    }

    void RecordPacing(double ms)
    {
        pacingMs_ = ms;
    }

    uint64_t DrawCalls() const
    {
        return counters_.scene + counters_.post + counters_.ui;
    }

    RenderFrameCounters& Counters()
    {
        return counters_;
    }

  private:
    bool logging_ = false;
    Clock::time_point windowStart_ = Clock::now(), sessionStart_ = Clock::now(),
                      renderStart_ = Clock::now(), previousEnd_ = Clock::now();
    uint64_t frames_ = 0, totalDrawCalls_ = 0, minDrawCalls_ = UINT64_MAX, maxDrawCalls_ = 0,
             frameNumber_ = 0;
    double renderMs_ = 0, pendingUpdateMs_ = 0, updateMs_ = 0, presentMs_ = 0, pacingMs_ = 0;
    RenderFrameCounters counters_;
};
