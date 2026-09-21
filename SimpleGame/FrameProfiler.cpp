#include "stdafx.h"
#include "FrameProfiler.h"
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
    windowStart_ = Clock::now();
    frames_ = totalDrawCalls_ = drawCalls_ = maxDrawCalls_ = 0;
    minDrawCalls_ = UINT64_MAX;
}

void FrameProfiler::BeginFrame()
{
    drawCalls_ = 0;
}

void FrameProfiler::RecordDrawCall()
{
    ++drawCalls_;
}

void FrameProfiler::EndFrame()
{
    if (!logging_)
    {
        return;
    }
    ++frames_;
    totalDrawCalls_ += drawCalls_;
    minDrawCalls_ = std::min(minDrawCalls_, drawCalls_);
    maxDrawCalls_ = std::max(maxDrawCalls_, drawCalls_);
    auto now = Clock::now();
    double seconds = std::chrono::duration<double>(now - windowStart_).count();
    if (seconds < 1.0)
    {
        return;
    }
    std::ostringstream line;
    line << std::fixed << std::setprecision(1) << "[PERF] FPS=" << double(frames_) / seconds
         << " | FrameMs(avg)=" << seconds * 1000 / double(frames_)
         << " | DrawCalls/frame last=" << drawCalls_
         << " avg=" << double(totalDrawCalls_) / double(frames_) << " min=" << minDrawCalls_
         << " max=" << maxDrawCalls_;
    std::cout << line.str() << std::endl;
    windowStart_ = now;
    frames_ = totalDrawCalls_ = maxDrawCalls_ = 0;
    minDrawCalls_ = UINT64_MAX;
}
