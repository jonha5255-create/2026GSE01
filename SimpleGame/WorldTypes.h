#pragma once
#include <cstdint>
#include <array>
#include <vector>

// Never convert absolute chunk coordinates into floating-point world positions.
struct Location
{
    int64_t cx = 0, cy = 0;
    double x = 90, y = 90;
    void Normalize();
};

struct BossArenaLayout
{
    int cx, cy;
    const char* name;
    const char* boss;
};

inline constexpr std::array<BossArenaLayout, 3> BossArenas{{
    {2, 0, "핏빛 제단", "핏빛 제단의 뿔왕"},
    {0, 2, "독안개 분지", "독안개 포식견"},
    {-2, 0, "붕괴 석정", "붕괴 석정의 거신"},
}};
inline constexpr float ArenaScale = 30.f;
inline constexpr float ArenaCenter = 320.f;
inline constexpr float ArenaHalf = 22.f * ArenaScale;
inline constexpr float ArenaEntry = ArenaCenter + 19.f * ArenaScale;
inline constexpr float ArenaGate = 496.f;

struct CityChunk
{
    uint32_t seed;
    std::vector<float> heights;
    float roadX = 90, roadY = 90;
    int district = 0;
    bool village = false;
    int villageIndex = -1;
    int bossRegion = -1;     // Three authored wilderness arenas around the home village.
    std::vector<int> styles; // 0: intact building, 1: ruin, 2: defensive wall.

    struct Plot
    {
        float x, y, width, depth;
        int homeSlot = -1;
    };

    struct Road
    {
        float x, y, width, depth;
        bool arterial;
    };

    std::vector<Plot> plots;
    std::vector<Road> roads;
};

CityChunk GenerateCityChunk(int64_t x, int64_t y, uint32_t seed);
bool IsVillageChunk(int64_t x, int64_t y);

struct VillageLayout
{
    int x, y;
    const char* name;
};

const std::vector<VillageLayout>& VillageLayouts();
int VillageIndex(int64_t x, int64_t y);
