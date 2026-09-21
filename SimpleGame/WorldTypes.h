#pragma once
#include <cstdint>

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
    float heights[4];

    struct Plot
    {
        float x, y, width, depth;
    } plots[4];
};
