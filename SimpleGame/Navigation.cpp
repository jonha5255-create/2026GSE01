#include "stdafx.h"
#include "Navigation.h"
#include <algorithm>
#include <queue>

bool Navigation::Sight(FarmPoint a, FarmPoint b, const Walkable& walkable, float radius)
{
    FarmPoint delta = Minus(b, a);
    int steps = std::max(1, int(std::ceil(Length(delta) / 5.f)));
    for (int i = 0; i <= steps; ++i)
    {
        float t = float(i) / steps;
        if (!walkable(a.x + delta.x * t, a.y + delta.y * t, radius))
        {
            return false;
        }
    }
    return true;
}

void Navigation::Rebuild(const Walkable& walkable)
{
    std::fill(navigation_.begin(), navigation_.end(), -1);
    constexpr int center = NavWidth / 2;
    std::queue<int> open;
    int start = center * NavWidth + center;
    navigation_[start] = 0;
    open.push(start);
    while (!open.empty())
    {
        int cell = open.front();
        open.pop();
        int x = cell % NavWidth, y = cell / NavWidth;
        const int dx[] = {-1, 1, 0, 0}, dy[] = {0, 0, -1, 1};
        for (int i = 0; i < 4; ++i)
        {
            int nx = x + dx[i], ny = y + dy[i];
            if (nx < 0 || ny < 0 || nx >= NavWidth || ny >= NavWidth)
            {
                continue;
            }
            int next = ny * NavWidth + nx;
            if (navigation_[next] >= 0)
            {
                continue;
            }
            if (!walkable((nx - center) * NavCell, (ny - center) * NavCell, 20))
            {
                continue;
            }
            navigation_[next] = navigation_[cell] + 1;
            open.push(next);
        }
    }
}

FarmPoint Navigation::Direction(FarmPoint position, const Walkable& walkable) const
{
    if (Sight(position, {}, walkable, 20))
    {
        return Unit({-position.x, -position.y});
    }
    int x = int(std::round(position.x / NavCell)) + NavWidth / 2;
    int y = int(std::round(position.y / NavCell)) + NavWidth / 2;
    int best = 100000;
    FarmPoint goal = position;
    for (int dy = -1; dy <= 1; ++dy)
    {
        for (int dx = -1; dx <= 1; ++dx)
        {
            int nx = x + dx, ny = y + dy;
            if (nx < 0 || ny < 0 || nx >= NavWidth || ny >= NavWidth)
            {
                continue;
            }
            int value = navigation_[ny * NavWidth + nx];
            FarmPoint candidate{(nx - NavWidth / 2) * NavCell, (ny - NavWidth / 2) * NavCell};
            if (value >= 0 && value < best && Sight(position, candidate, walkable, 20))
            {
                best = value;
                goal = candidate;
            }
        }
    }
    return Unit(Minus(goal, position));
}

bool Navigation::Reachable(FarmPoint position) const
{
    int x = int(std::round(position.x / NavCell)) + NavWidth / 2;
    int y = int(std::round(position.y / NavCell)) + NavWidth / 2;
    return x >= 0 && y >= 0 && x < NavWidth && y < NavWidth && navigation_[y * NavWidth + x] >= 0;
}
