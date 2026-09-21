#pragma once
#include "Actor.h"
#include <functional>
#include <cmath>

using FarmPoint = ActorPosition;
using Walkable = std::function<bool(float, float, float)>;
enum class LootKind
{
    SoulStone,
    Upgrade,
    Heal,
    Magnet
};
enum class RunState
{
    Farming,
    BossReady,
    BossFight,
    Won,
    Lost
};

inline float Length(FarmPoint p)
{
    return std::sqrt(p.x * p.x + p.y * p.y);
}

inline FarmPoint Minus(FarmPoint a, FarmPoint b)
{
    return {a.x - b.x, a.y - b.y};
}

inline FarmPoint Unit(FarmPoint p)
{
    float length = Length(p);
    return length > .001f ? FarmPoint{p.x / length, p.y / length} : FarmPoint{};
}
