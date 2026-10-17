#pragma once
#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

class LevelOne;
struct Location;

// Game-thread snapshots, background HTTP delivery. No Actor pointers cross threads.
class WorldMonitor
{
  public:
    static WorldMonitor& Get();
    ~WorldMonitor();
    bool Start(unsigned short port = 8765);
    void Stop();
    void Publish(const LevelOne& level,
                 const Location& player,
                 size_t residentChunks,
                 uint32_t seed,
                 uint64_t worldId);
    static std::string Snapshot(const LevelOne& level,
                                const Location& player,
                                size_t residentChunks,
                                uint32_t seed);
    static bool SelfTest();

  private:
    void Serve();
    std::atomic<bool> running_{false};
    std::thread worker_;
    std::mutex mutex_;
    std::string latest_;
    std::wstring assetDirectory_;
    uintptr_t listener_ = UINTPTR_MAX;
    bool networkInitialized_ = false;
    uint64_t sequence_ = 0, worldEpoch_ = 1;
    std::string sessionId_;
    uint64_t lastWorldId_ = 0;
    std::chrono::steady_clock::time_point nextSnapshot_;
};
