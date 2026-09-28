#include "stdafx.h"
#include "FrameProfiler.h"
#include "RenderDiagnostics.h"
#include <algorithm>
#include <iomanip>
#include <iostream>
#include <sstream>

FrameProfiler& FrameProfiler::Get()
{
    static FrameProfiler profiler;
    return profiler;
}

void FrameProfiler::StartLogging()
{
    logging_ = true;
    windowStart_ = sessionStart_ = previousEnd_ = Clock::now();
    frames_ = totalDrawCalls_ = maxDrawCalls_ = frameNumber_ = 0;
    minDrawCalls_ = UINT64_MAX;
}

void FrameProfiler::BeginFrame()
{
    counters_ = {};
    renderStart_ = Clock::now();
    renderMs_ = 0;
    updateMs_ = pendingUpdateMs_;
    pendingUpdateMs_ = 0;
}

void FrameProfiler::RecordDrawCall(DrawStage stage)
{
    if (stage == DrawStage::Scene)
    {
        ++counters_.scene;
    }
    else if (stage == DrawStage::UI)
    {
        ++counters_.ui;
    }
    else
    {
        ++counters_.post;
    }
}

void FrameProfiler::EndRender()
{
    renderMs_ = std::chrono::duration<double, std::milli>(Clock::now() - renderStart_).count();
}

void FrameProfiler::EndFrame()
{
    if (!logging_)
    {
        return;
    }
    auto now = Clock::now();
    double frameMs = std::chrono::duration<double, std::milli>(now - previousEnd_).count();
    previousEnd_ = now;
    ++frames_;
    ++frameNumber_;
    auto calls = DrawCalls();
    totalDrawCalls_ += calls;
    minDrawCalls_ = std::min(minDrawCalls_, calls);
    maxDrawCalls_ = std::max(maxDrawCalls_, calls);
    const auto& c = counters_;
    std::ostringstream row;
    row << std::fixed << std::setprecision(4) << frameNumber_ << ','
        << std::chrono::duration<double>(now - sessionStart_).count() << ',' << frameMs << ','
        << renderMs_ << ',' << updateMs_ << ',' << calls << ',' << c.scene << ',' << c.post << ','
        << c.ui << ',' << c.objects << ',' << c.triangles << ',' << c.instanceBytes << ','
        << c.meshBytes << ',' << c.hits << ',' << c.misses << ',' << c.generations << ',' << c.loads
        << ',' << c.rejects << ',' << c.writes << ',' << c.evictions << ',' << c.splits;
    RenderDiagnostics::Get().Frame(row.str());
    if (frameMs > 33.333)
    {
        RenderDiagnostics::Get().Event("slow_frame",
                                       "frame=" + std::to_string(frameNumber_)
                                           + "; frame_ms=" + std::to_string(frameMs)
                                           + "; generations=" + std::to_string(c.generations));
    }
    double seconds = std::chrono::duration<double>(now - windowStart_).count();
    if (seconds < 1)
    {
        return;
    }
    std::ostringstream line;
    line << std::fixed << std::setprecision(1) << "[PERF] FPS=" << double(frames_) / seconds
         << " | FrameMs(avg)=" << seconds * 1000 / frames_ << " | DrawCalls/frame last=" << calls
         << " avg=" << double(totalDrawCalls_) / frames_ << " min=" << minDrawCalls_
         << " max=" << maxDrawCalls_ << " | Scene/Post/UI=" << c.scene << '/' << c.post << '/'
         << c.ui << " | Instances=" << c.triangles << " MeshBuilds=" << c.generations
         << " DiskLoads=" << c.loads << " | RenderCPUms=" << renderMs_
         << " UploadKB=" << (c.instanceBytes + c.meshBytes) / 1024.;
    std::cout << line.str() << std::endl;
    RenderDiagnostics::Get().Flush();
    windowStart_ = now;
    frames_ = totalDrawCalls_ = maxDrawCalls_ = 0;
    minDrawCalls_ = UINT64_MAX;
}
