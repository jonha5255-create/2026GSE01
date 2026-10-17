#pragma once
#include <cstdint>
#include <vector>

// Never convert absolute chunk coordinates into floating-point world positions.
struct Location
{
    int64_t cx = 0, cy = 0;
    double x = 90, y = 90;
    void Normalize();
};

struct CityChunk
{
    uint32_t seed;
    std::vector<float> heights;
    float roadX = 90, roadY = 90;
    int district = 0;
    bool village = false;
    std::vector<int> styles; // 0: intact building, 1: ruin, 2: defensive wall.

    struct Plot
    {
        float x, y, width, depth;
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
