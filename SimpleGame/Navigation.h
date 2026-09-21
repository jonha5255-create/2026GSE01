#pragma once
#include "GameTypes.h"
#include <vector>

class Navigation
{
  public:
    Navigation() : navigation_(NavWidth * NavWidth, -1)
    {
    }

    static bool Sight(FarmPoint a, FarmPoint b, const Walkable& walkable, float radius = 3);
    void Rebuild(const Walkable& walkable);
    FarmPoint Direction(FarmPoint position, const Walkable& walkable) const;
    bool Reachable(FarmPoint position) const;

  private:
    static constexpr int NavWidth = 65;
    static constexpr float NavCell = 20;
    std::vector<int> navigation_;
};
