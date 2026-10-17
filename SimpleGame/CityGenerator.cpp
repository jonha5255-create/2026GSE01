#include "stdafx.h"
#include "WorldTypes.h"
#include <algorithm>

namespace
{
    uint64_t Hash(uint64_t x)
    {
        x += 0x9e3779b97f4a7c15ULL;
        x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
        x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
        return x ^ (x >> 31);
    }
} // namespace

bool IsVillageChunk(int64_t x, int64_t y)
{
    return x % 2 == 0 && y % 2 == 0;
}

CityChunk GenerateCityChunk(int64_t x, int64_t y, uint32_t seed)
{
    CityChunk result{};
    result.seed = uint32_t(Hash(uint64_t(x) ^ Hash(uint64_t(y)) ^ seed));
    result.district = int(result.seed % 3);
    result.village = IsVillageChunk(x, y);
    // Axis-specific keys give both neighbors exactly the same road at a shared edge.
    uint64_t column = Hash(uint64_t(x) ^ seed ^ 0x8142), row = Hash(uint64_t(y) ^ seed ^ 0x9217);
    result.roadX = x == 0 ? 90.f : 68.f + float(column % 47);
    result.roadY = y == 0 ? 90.f : 68.f + float(row % 47);
    float verticalWidth = 42.f + float((column >> 8) % 3) * 8;
    float horizontalWidth = 42.f + float((row >> 8) % 3) * 8;
    result.roads.push_back({result.roadX - verticalWidth / 2, 0, verticalWidth, 640, true});
    result.roads.push_back({0, result.roadY - horizontalWidth / 2, 640, horizontalWidth, true});
    bool town = x == 0 && y == 0;
    float end = town ? 510.f : 595.f;
    float splitX = 300.f + float((result.seed >> 4) % 71);
    float splitY = 285.f + float((result.seed >> 12) % 111);
    float alley = 24.f + float((result.seed >> 20) % 3) * 4;
    result.roads.push_back(
        {splitX - alley / 2, result.roadY, alley, end + 12 - result.roadY, false});
    // Alternate a full cross street and a T-junction with a shorter service street.
    float start = result.seed % 2 ? splitX : result.roadX;
    result.roads.push_back({start, splitY - alley / 2, end + 12 - start, alley, false});
    if (town)
    {
        // Keep the story locations and the town's hunting circuit accessible.
        result.roads.push_back({535, 90, 30, 485, false});
        result.roads.push_back({90, 535, 485, 30, false});
    }
    float left = result.roadX + verticalWidth / 2 + 25;
    float top = result.roadY + horizontalWidth / 2 + 25;
    float xs[] = {left, splitX + alley / 2 + 24};
    float ys[] = {top, splitY + alley / 2 + 24};
    float xe[] = {splitX - alley / 2 - 24, end};
    float ye[] = {splitY - alley / 2 - 24, end};
    for (int block = 0; block < 4; ++block)
    {
        uint64_t n = Hash(result.seed + block * 991);
        float bx = xs[block % 2], by = ys[block / 2];
        float width = xe[block % 2] - bx, depth = ye[block / 2] - by;
        bool split = width > 195 && n % 3 != 0;
        int count = split ? 2 : 1;
        float parcel = (width - (split ? 64 : 0)) / count;
        for (int lot = 0; lot < count; ++lot)
        {
            uint64_t detail = Hash(n + lot);
            float setbackX = 4 + float((detail >> 8) % 10);
            float setbackY = 4 + float((detail >> 16) % 12);
            float w = std::max(40.f, parcel - setbackX - 8 - float((detail >> 24) % 17));
            float d = std::max(40.f, depth - setbackY - 8 - float((detail >> 32) % 23));
            result.plots.push_back({bx + lot * (parcel + 64) + setbackX, by + setbackY, w, d});
            float height = result.district == 2   ? 36.f + float(detail % 30)
                           : result.district == 1 ? 48.f + float(detail % 76)
                                                  : 82.f + float(detail % 91);
            result.heights.push_back(height);
        }
    }
    result.styles.assign(result.plots.size(), 0);
    if (!result.village)
    {
        // Preserve the through-roads, replace dense city blocks with open waste and ruins.
        auto plots = result.plots;
        result.plots.clear();
        result.heights.clear();
        result.styles.clear();
        for (size_t i = 0; i < plots.size(); ++i)
        {
            if (Hash(result.seed + i * 7919) % 5 == 0)
            {
                continue;
            }
            auto ruin = plots[i];
            ruin.width = std::min(ruin.width, 125.f);
            ruin.depth = std::min(ruin.depth, 105.f);
            result.plots.push_back(ruin);
            result.heights.push_back(28.f + float(Hash(result.seed + i) % 33));
            result.styles.push_back(1);
        }
    }
    else
    {
        auto wall = [&](float px, float py, float w, float d)
        {
            if (w <= 0 || d <= 0)
            {
                return;
            }
            result.plots.push_back({px, py, w, d});
            result.heights.push_back(42);
            result.styles.push_back(2);
        };
        // Four gates align with the continuous arterial roads; the full road and sidewalks fit.
        float gx = result.roadX, gy = result.roadY;
        float xGap = verticalWidth / 2 + 28, yGap = horizontalWidth / 2 + 28;
        for (float edge : {20.f, 610.f})
        {
            wall(20, edge, gx - xGap - 20, 10);
            wall(gx + xGap, edge, 620 - gx - xGap, 10);
            wall(edge, 20, 10, gy - yGap - 20);
            wall(edge, gy + yGap, 10, 620 - gy - yGap);
        }
    }
    return result;
}
