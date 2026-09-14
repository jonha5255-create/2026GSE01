#include "stdafx.h"
#include "PrototypeWorld.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <sstream>
#include <vector>
#include <iostream>
#include <iomanip>
#include <queue>

namespace
{
    constexpr float Side = 640, Pi = 3.14159265f;

    uint64_t Mix(uint64_t x)
    {
        x += 0x9e3779b97f4a7c15ULL;
        x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
        x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
        return x ^ (x >> 31);
    }

    Point Lerp(Point a, Point b, float t)
    {
        return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t};
    }

    float Distance(float x, float y)
    {
        return std::sqrt(x * x + y * y);
    }
} // namespace

void Location::Normalize()
{
    // Guard at the representable edge rather than overflowing signed integers.
    while (x < 0)
    {
        if (cx == INT64_MIN)
        {
            x = 0;
            break;
        }
        --cx;
        x += Side;
    }
    while (x >= Side)
    {
        if (cx == INT64_MAX)
        {
            x = Side - 1;
            break;
        }
        ++cx;
        x -= Side;
    }
    while (y < 0)
    {
        if (cy == INT64_MIN)
        {
            y = 0;
            break;
        }
        --cy;
        y += Side;
    }
    while (y >= Side)
    {
        if (cy == INT64_MAX)
        {
            y = Side - 1;
            break;
        }
        ++cy;
        y -= Side;
    }
}

CityChunk PrototypeWorld::Generate(Coord c, uint32_t seed)
{
    CityChunk chunk = {};
    chunk.seed = static_cast<uint32_t>(
        Mix(static_cast<uint64_t>(c.first) ^ Mix(static_cast<uint64_t>(c.second)) ^ seed));
    for (int i = 0; i < 4; ++i)
    {
        uint64_t n = Mix(chunk.seed + i);
        chunk.heights[i] = 55.f + float(n % 75);
        // Buildings stay inside disjoint plots. Wide connected roads remain
        // open around every plot and across all four chunk boundaries.
        chunk.plots[i] = {175.f + (i % 2) * 205 + float((n >> 8) % 18),
                          170.f + (i / 2) * 205 + float((n >> 16) % 18),
                          84.f + float((n >> 24) % 30),
                          84.f + float((n >> 32) % 30)};
    }
    return chunk;
}

PrototypeWorld::PrototypeWorld(uint32_t seed) : seed_(seed), level_(seed)
{
    Stream();
}

void PrototypeWorld::Stream()
{
    std::map<Coord, CityChunk> next;
    for (int y = -2; y <= 2; ++y)
    {
        for (int x = -2; x <= 2; ++x)
        {
            if ((x < 0 && player_.cx < INT64_MIN - x) || (x > 0 && player_.cx > INT64_MAX - x)
                || (y < 0 && player_.cy < INT64_MIN - y) || (y > 0 && player_.cy > INT64_MAX - y))
            {
                continue;
            }
            Coord c = {player_.cx + x, player_.cy + y};
            auto it = chunks_.find(c);
            next.emplace(c, it != chunks_.end() ? it->second : Generate(c, seed_));
        }
    }
    chunks_.swap(next);
}

bool PrototypeWorld::Blocked(const Location& p, float radius) const
{
    // Collision uses the same footprints as rendering, with a player-radius margin.
    auto it = chunks_.find({p.cx, p.cy});
    CityChunk chunk = it == chunks_.end() ? Generate({p.cx, p.cy}, seed_) : it->second;
    for (int i = 0; i < 4; ++i)
    {
        const auto& b = chunk.plots[i];
        double dx = std::max(double(b.x) - p.x, std::max(0., p.x - (b.x + b.width)));
        double dy = std::max(double(b.y) - p.y, std::max(0., p.y - (b.y + b.depth)));
        if (dx * dx + dy * dy < radius * radius)
        {
            return true;
        }
    }
    return false;
}

bool PrototypeWorld::Walkable(float x, float y, float radius) const
{
    Location p = player_;
    p.x += x;
    p.y += y;
    p.Normalize();
    return !Blocked(p, radius);
}

void PrototypeWorld::Move(float dx, float dy)
{
    auto before = Coord(player_.cx, player_.cy);
    Location p = player_;
    p.x += dx;
    p.Normalize();
    if (!Blocked(p))
    {
        player_ = p;
        cameraLag_.x += dx * .85f;
        cameraLag_.y += dx * .425f;
        level_.Shift(dx, 0);
    }
    p = player_;
    p.y += dy;
    p.Normalize();
    if (!Blocked(p))
    {
        player_ = p;
        cameraLag_.x -= dy * .85f;
        cameraLag_.y += dy * .425f;
        level_.Shift(0, dy);
    }
    if (before != Coord(player_.cx, player_.cy))
    {
        Stream();
    }
}

void PrototypeWorld::Key(unsigned char key, bool down)
{
    if (key >= 'A' && key <= 'Z')
    {
        key += 32;
    }
    bool pressed = down && !keys_[key];
    keys_[key] = down;
    if (!pressed)
    {
        return;
    }
    if (key == 'q')
    {
        weapon_ = !weapon_;
        transform_ = 1;
    }
    if (key == 'c')
    {
        level_.ToggleRange();
    }
    if (key == 'v')
    {
        distortion_ = !distortion_;
    }
    if (key == 'g')
    {
        debug_ = !debug_;
    }
    if (key == 'r')
    {
        *this = PrototypeWorld(seed_);
        return;
    }
    if (key == 'e')
    {
        level_.Interact(
            [&](float x, float y, float radius)
            {
                return Walkable(x, y, radius);
            });
    }
}

void PrototypeWorld::Update(float dt)
{
    dt = std::min(dt, .05f);
    time_ += dt;
    attack_ = std::max(0.f, attack_ - dt);
    transform_ = std::max(0.f, transform_ - dt * 1.5f);
    float sx = float(keys_['d']) - float(keys_['a']), sy = float(keys_['s']) - float(keys_['w']);
    float length = Distance(sx, sy);
    if (length > 0 && level_.state != RunState::Lost)
    {
        sx /= length;
        sy /= length;
        facing_ = {sx, sy};
        walk_ += dt * 11;
        // Invert the isometric projection: WASD remains screen-relative.
        float dx = sx + sy * 2, dy = -sx + sy * 2;
        float norm = Distance(dx, dy);
        Move(dx / norm * level_.MoveSpeed() * dt, dy / norm * level_.MoveSpeed() * dt);
    }
    float follow = 1 - std::exp(-10 * dt);
    cameraLag_ = Lerp(cameraLag_, {0, 0}, follow);
    level_.Update(dt,
                  weapon_,
                  [&](float x, float y, float radius)
                  {
                      return Walkable(x, y, radius);
                  });
}

Point PrototypeWorld::Project(float x, float y, float z) const
{
    return {center_.x + ((x - y) * .85f + cameraLag_.x) * zoom_,
            center_.y + ((x + y) * .425f - z + cameraLag_.y) * zoom_};
}

void PrototypeWorld::LocalMesh(PrototypeRenderer& r,
                               const std::string& key,
                               float x,
                               float y,
                               float opacity,
                               const std::function<void()>& build)
{
    Point origin = Project(x, y);
    float scale = zoom_;
    r.CachedMesh(key,
                 origin,
                 scale,
                 opacity,
                 [&]()
                 {
                     Point savedCenter = center_, savedLag = cameraLag_;
                     float savedZoom = zoom_;
                     center_ = {0, 0};
                     cameraLag_ = {0, 0};
                     zoom_ = 1;
                     build();
                     center_ = savedCenter;
                     cameraLag_ = savedLag;
                     zoom_ = savedZoom;
                 });
}

void PrototypeWorld::Floor(PrototypeRenderer& r, float x, float y, const CityChunk& chunk)
{
    LocalMesh(r,
              "floor:" + std::to_string(chunk.seed) + ":" + std::to_string(debug_),
              x,
              y,
              1,
              [&]()
              {
                  FloorGeometry(r, 0, 0, chunk);
              });
}

void PrototypeWorld::FloorGeometry(PrototypeRenderer& r, float x, float y, const CityChunk& chunk)
{
    auto p = [&](float a, float b)
    {
        return Project(x + a, y + b);
    };
    r.Quad(p(0, 0), p(Side, 0), p(Side, Side), p(0, Side), Color(16, 26, 33));
    r.Quad(p(137, 135), p(560, 135), p(560, 558), p(137, 558), Color(30, 42, 48));
    for (int i = 0; i <= 8; ++i)
    {
        float s = i * 80.f;
        r.Line(p(s, 0), p(s, Side), 1, Color(52, 66, 69, .28f));
        r.Line(p(0, s), p(Side, s), 1, Color(52, 66, 69, .28f));
    }
    r.Line(p(132, 132), p(563, 132), 2, Color(77, 99, 97));
    r.Line(p(132, 132), p(132, 563), 2, Color(77, 99, 97));
    r.Line(p(563, 132), p(563, 563), 2, Color(53, 67, 73));
    r.Line(p(132, 563), p(563, 563), 2, Color(53, 67, 73));
    for (int i = 0; i < 10; ++i)
    {
        float a = i * 64.f;
        r.Line(p(a, 54), p(a + 29, 54), 2, Color(154, 142, 95, .55f));
        r.Line(p(54, a), p(54, a + 29), 2, Color(154, 142, 95, .55f));
    }
    for (int i = 0; i < 7; ++i)
    {
        float a = 143.f + i * 10;
        r.Quad(p(a, 10), p(a + 5, 10), p(a + 5, 115), p(a, 115), Color(109, 128, 128, .45f));
        r.Quad(p(10, a), p(115, a), p(115, a + 5), p(10, a + 5), Color(109, 128, 128, .45f));
    }
    for (int i = 0; i < 12; ++i)
    {
        uint64_t n = Mix(chunk.seed + i * 17);
        float a = float(n % 600), b = 20.f + float((n >> 12) % 75);
        Point q = p(a, b);
        r.Line(q, {q.x + 25 * zoom_, q.y - 2 * zoom_}, 2, Color(84, 132, 137, .22f));
        if (i < 3)
        {
            r.Ellipse(p(a + 25, b + 10), 12 * zoom_, 4 * zoom_, Color(90, 28, 41, .55f));
        }
    }
    if (debug_)
    {
        r.Line(p(0, 0), p(Side, 0), 2, Color(67, 223, 192, .8f));
        r.Line(p(0, 0), p(0, Side), 2, Color(67, 223, 192, .8f));
    }
}

void PrototypeWorld::Building(
    PrototypeRenderer& r, float x, float y, float w, float d, float h, uint32_t seed)
{
    auto p = [&](float a, float b, float z = 0)
    {
        return Project(x + a, y + b, z);
    };
    Point a = p(0, 0, h), b = p(w, 0, h), c = p(w, d, h), e = p(0, d, h);
    bool haunted = (seed % 5 == 0) && distortion_;
    float skew = haunted ? std::sin(time_ * .65f + float(seed % 19)) * 12 * zoom_ : 0;
    a.x += skew;
    b.x += skew;
    c.x += skew;
    e.x += skew;
    // Fade foreground buildings when they occlude the player.
    float alpha = 1;
    Point feet = Project(0, 0);
    float left = std::min(a.x, std::min(b.x, std::min(c.x, e.x)));
    float right = std::max(a.x, std::max(b.x, std::max(c.x, e.x)));
    float top = std::min(a.y, std::min(b.y, std::min(c.y, e.y)));
    if (x + y > 0 && feet.x + 14 * zoom_ > left && feet.x - 14 * zoom_ < right && feet.y > top
        && feet.y - 55 * zoom_ < p(w, d).y)
    {
        alpha = .3f;
    }
    if (!haunted)
    {
        std::string key = "building:" + std::to_string(seed) + ":" + std::to_string(w) + ":"
                          + std::to_string(d) + ":" + std::to_string(h);
        LocalMesh(r,
                  key,
                  x,
                  y,
                  alpha,
                  [&]()
                  {
                      BuildingGeometry(r, 0, 0, w, d, h, seed, 1, false);
                  });
        return;
    }
    BuildingGeometry(r, x, y, w, d, h, seed, alpha, true);
}

void PrototypeWorld::BuildingGeometry(PrototypeRenderer& r,
                                      float x,
                                      float y,
                                      float w,
                                      float d,
                                      float h,
                                      uint32_t seed,
                                      float alpha,
                                      bool haunted)
{
    auto p = [&](float a, float b, float z = 0)
    {
        return Project(x + a, y + b, z);
    };
    Point a = p(0, 0, h), b = p(w, 0, h), c = p(w, d, h), e = p(0, d, h);
    float skew = haunted ? std::sin(time_ * .65f + float(seed % 19)) * 12 * zoom_ : 0;
    a.x += skew;
    b.x += skew;
    c.x += skew;
    e.x += skew;
    auto ink = [&](int red, int green, int blue)
    {
        return Color(red, green, blue, alpha);
    };
    r.Quad(p(0, d), p(w, d), p(w + 55, d + 40), p(20, d + 40), Color(0, 0, 0, .22f));
    r.Quad(e, c, p(w, d), p(0, d), ink(30, 46, 53));
    r.Quad(b, c, p(w, d), p(w, 0), ink(20, 32, 42));
    r.Quad(a, b, c, e, haunted ? ink(51, 47, 65) : ink(48, 66, 72));
    r.Line(a, b, 2, ink(111, 131, 133));
    r.Line(a, e, 2, ink(100, 124, 123));
    r.Line(e, c, 2, ink(68, 91, 97));
    r.Line(b, c, 2, ink(59, 79, 88));
    // Window strips run along both visible walls.
    for (float z = 24; z < h - 12; z += 30)
    {
        for (int col = 0; col < 5; ++col)
        {
            float s = 8 + col * (w - 24) / 5.f;
            float t = 8 + col * (d - 24) / 5.f;
            uint64_t n = Mix(seed + col + int(z) * 31);
            Ink light = n % 4 == 0   ? ink(157, 139, 111)
                        : n % 3 == 0 ? ink(64, 145, 151)
                                     : ink(17, 32, 39);
            Point f1 = p(s, d + .2f, z), f2 = p(s + 16, d + .2f, z);
            r.Quad(f1, f2, p(s + 16, d + .2f, z + 15), p(s, d + .2f, z + 15), light);
            r.Quad(p(w + .2f, t, z),
                   p(w + .2f, t + 12, z),
                   p(w + .2f, t + 12, z + 15),
                   p(w + .2f, t, z + 15),
                   light);
        }
    }
    // Rooftop air conditioner and antenna.
    r.Quad(p(28, 30, h + 1), p(72, 30, h + 1), p(72, 66, h + 1), p(28, 66, h + 1), ink(21, 35, 43));
    for (int i = 0; i < 5; ++i)
    {
        r.Line(p(33, 34 + i * 6.f, h + 2), p(67, 34 + i * 6.f, h + 2), 1, ink(80, 101, 108));
    }
    r.Line(p(w - 25, 25, h), p(w - 25, 25, h + 38), 2, ink(102, 120, 122));
    r.Line(p(w - 25, 25, h + 29), p(w - 5, 25, h + 29), 1, ink(102, 120, 122));
    Point sign = p(w * .5f, d + 1, 40);
    r.Rect(sign.x - 37 * zoom_, sign.y - 12 * zoom_, 74 * zoom_, 20 * zoom_, ink(9, 23, 29));
    Ink neon = Emissive(haunted ? ink(239, 81, 122) : ink(104, 223, 204), 2.5f);
    r.Line({sign.x - 36 * zoom_, sign.y + 8 * zoom_},
           {sign.x + 36 * zoom_, sign.y + 8 * zoom_},
           2,
           neon);
    const char* names[] = {"CLINIC", "MOTEL", "RECORDS", "NO SIGNAL"};
    r.Text(sign.x - 32 * zoom_, sign.y - 11 * zoom_, names[seed % 4], neon, .62f * zoom_);
    if (haunted)
    {
        Point crack = p(w, d, h * .8f);
        r.Line(crack, p(w - 22, d, h * .5f), 3, Emissive(Color(214, 69, 115, alpha), 4));
        r.Glow(crack, 30 * zoom_, Color(214, 69, 115, alpha));
    }
}

void PrototypeWorld::Player(PrototypeRenderer& r)
{
    Point p = Project(0, 0);
    float z = zoom_, step = std::sin(walk_) * 3;
    r.Ellipse({p.x, p.y + 2 * z}, 20 * z, 7 * z, Color(0, 0, 0, .65f));
    r.Glow({p.x, p.y - 20 * z}, 44 * z, Color(64, 217, 194, .3f));
    r.Line({p.x - 5 * z, p.y - 18 * z}, {p.x - 7 * z, p.y - step * z}, 6 * z, Color(12, 18, 28));
    r.Line({p.x + 5 * z, p.y - 18 * z}, {p.x + 7 * z, p.y + step * z}, 6 * z, Color(12, 18, 28));
    r.Quad({p.x - 9 * z, p.y - 41 * z},
           {p.x + 8 * z, p.y - 41 * z},
           {p.x + 15 * z, p.y - 13 * z},
           {p.x - 14 * z, p.y - 15 * z},
           Color(45, 63, 80));
    r.Line({p.x - 7 * z, p.y - 38 * z}, {p.x - 10 * z, p.y - 16 * z}, 3 * z, Color(85, 124, 137));
    r.Rect(p.x - 7 * z, p.y - 54 * z, 14 * z, 14 * z, Color(197, 181, 170));
    r.Quad({p.x - 9 * z, p.y - 56 * z},
           {p.x + 8 * z, p.y - 58 * z},
           {p.x + 10 * z, p.y - 47 * z},
           {p.x - 8 * z, p.y - 48 * z},
           Color(17, 22, 34));
    r.Rect(p.x - 8 * z, p.y - 42 * z, 17 * z, 4 * z, Color(174, 54, 74));
    r.Triangle({p.x - 8 * z, p.y - 41 * z},
               {p.x - 28 * z, p.y - 29 * z + std::sin(time_ * 4) * 4 * z},
               {p.x - 9 * z, p.y - 34 * z},
               Color(141, 38, 64));
    if (weapon_)
    {
        Point hand{p.x + 9 * z, p.y - 29 * z};
        float dx = (level_.aim.x - level_.aim.y) * .85f;
        float dy = (level_.aim.x + level_.aim.y) * .425f;
        float length = std::max(.01f, Distance(dx, dy));
        dx /= length;
        dy /= length;
        Point tip{hand.x + dx * 43 * z, hand.y + dy * 43 * z};
        r.Line({hand.x - dx * 12 * z, hand.y - dy * 12 * z}, tip, 12 * z, Color(33, 64, 80));
        r.Line(hand, tip, 5 * z, Emissive(Color(101, 233, 215), 2));
        r.Line({hand.x - dy * 8 * z, hand.y + dx * 8 * z},
               {tip.x - dy * 8 * z, tip.y + dx * 8 * z},
               2 * z,
               Color(220, 181, 120));
        r.Ellipse(hand, 6 * z, 6 * z, Color(167, 137, 105));
        if (level_.muzzleFlash > 0)
        {
            r.Glow(tip, 38 * z, Emissive(Color(124, 255, 232), 5));
            r.Ellipse(tip, 8 * z, 8 * z, Emissive(Color(199, 255, 237), 6));
        }
    }
    else
    {
        Point ghost{p.x + 43 * z, p.y - 44 * z + std::sin(time_ * 3) * 6 * z};
        r.Ellipse({ghost.x, p.y + 5 * z}, 12 * z, 4 * z, Color(0, 0, 0, .3f));
        r.Glow(ghost, 46 * z, Color(45, 239, 202));
        r.Quad({ghost.x, ghost.y - 15 * z},
               {ghost.x + 13 * z, ghost.y},
               {ghost.x, ghost.y + 21 * z},
               {ghost.x - 13 * z, ghost.y},
               Emissive(Color(84, 214, 187), 2.5f));
        r.Triangle({ghost.x - 9 * z, ghost.y - 5 * z},
                   {ghost.x - 13 * z, ghost.y - 23 * z},
                   {ghost.x - 2 * z, ghost.y - 12 * z},
                   Emissive(Color(179, 255, 225), 3));
        r.Triangle({ghost.x + 9 * z, ghost.y - 5 * z},
                   {ghost.x + 13 * z, ghost.y - 23 * z},
                   {ghost.x + 2 * z, ghost.y - 12 * z},
                   Emissive(Color(179, 255, 225), 3));
        r.Rect(ghost.x - 7 * z, ghost.y - 3 * z, 4 * z, 3 * z, Color(8, 35, 49));
        r.Rect(ghost.x + 3 * z, ghost.y - 3 * z, 4 * z, 3 * z, Color(8, 35, 49));
    }
    if (transform_ > 0)
    {
        float radius = (1 - transform_) * 85 * z;
        for (int i = 0; i < 32; ++i)
        {
            float a = i * Pi / 16;
            r.Line(
                {p.x + std::cos(a) * radius, p.y - 24 * z + std::sin(a) * radius * .5f},
                {p.x + std::cos(a + .1f) * radius, p.y - 24 * z + std::sin(a + .1f) * radius * .5f},
                2 * z,
                Color(145, 255, 222, transform_));
        }
    }
}

void PrototypeWorld::Rift(PrototypeRenderer& r, float x, float y)
{
    Point base = Project(x, y);
    float z = zoom_;
    Ink red = Emissive(memory_ ? Color(97, 225, 193) : Color(237, 61, 103), 4);
    r.Ellipse(base, 46 * z, 15 * z, Color(101, 21, 48, .6f));
    r.Glow({base.x, base.y - 54 * z}, 100 * z, red);
    float wobble = distortion_ ? std::sin(time_ * 2) * 8 : 0;
    Point top{base.x + (15 + wobble) * z, base.y - 129 * z};
    r.Quad({base.x - 13 * z, base.y - 10 * z},
           {base.x - 18 * z, base.y - 63 * z},
           top,
           {base.x + 17 * z, base.y - 48 * z},
           Color(5, 6, 18));
    r.Line({base.x - 13 * z, base.y - 10 * z}, {base.x - 18 * z, base.y - 63 * z}, 3 * z, red);
    r.Line({base.x - 18 * z, base.y - 63 * z}, top, 3 * z, red);
    r.Line(top, {base.x + 17 * z, base.y - 48 * z}, 2 * z, Emissive(Color(255, 161, 165), 4));
    r.Line({base.x + 17 * z, base.y - 48 * z}, {base.x - 13 * z, base.y - 10 * z}, 2 * z, red);
    for (int i = 0; i < 14; ++i)
    {
        float t = time_ * .25f + i * .42f;
        Point q{base.x + std::sin(t * 2) * 40 * z, base.y - std::fmod(t * 30, 150.f) * z};
        r.Rect(q.x, q.y, 2 * z, 5 * z, Color(255, 138, 151, .65f));
    }
    r.Text(base.x - 40 * z, base.y + 21 * z, "E / 보스 소환", red, .7f * z);
}

void PrototypeWorld::Hud(PrototypeRenderer& r)
{
    float uiScale = std::min(1.f, std::min(r.Width() / 1280.f, r.Height() / 800.f));
    r.SetScale(uiScale);
    float w = r.Width() / uiScale, h = r.Height() / uiScale;
    Ink muted = Color(144, 164, 176), white = Color(231, 241, 235), cyan = Color(109, 234, 207);
    Ink red = Color(245, 97, 133), gold = Color(243, 196, 113);
    r.Rect(0, 0, w, 93, Color(8, 15, 23, .97f));
    r.Text(27, 10, "LEVEL 01 / 영혼의 파밍", muted, .7f);
    r.Text(27, 35, "이름 없는 거리", white, 1.35f);
    r.Text(500,
           10,
           "체력 " + std::to_string(int(level_.health)) + " / "
               + std::to_string(int(level_.MaximumHealth())),
           white,
           .7f);
    r.Rect(500, 36, 240, 9, Color(52, 40, 54));
    r.Rect(500, 36, 240 * level_.health / level_.MaximumHealth(), 9, red);
    std::string xp = level_.level >= 10 ? "최대 레벨"
                                        : "경험치 " + std::to_string(level_.experience) + " / "
                                              + std::to_string(level_.ExperienceRequired());
    r.Text(500, 51, "LV." + std::to_string(level_.level) + "   " + xp, cyan, .68f);
    r.Rect(500, 76, 240, 4, Color(34, 55, 62));
    r.Rect(
        500,
        76,
        240 * (level_.level >= 10 ? 1.f : float(level_.experience) / level_.ExperienceRequired()),
        4,
        cyan);
    std::ostringstream seed;
    seed << "SEED " << seed_;
    r.Text(w - 302, 16, seed.str(), muted, .68f);
    r.Text(w - 302,
           42,
           "영혼포 +" + std::to_string(level_.weaponRank) + " / 처치 "
               + std::to_string(level_.kills),
           gold,
           .83f);
    r.Rect(26, 111, 354, 137, Color(8, 17, 24, .94f));
    r.Rect(26, 111, 3, 137, cyan);
    r.Text(42, 120, "목표 / 파밍 후 수문장에게 도전", cyan, .78f);
    r.Text(42, 149, level_.Objective(), white, .66f);
    r.Text(42,
           174,
           "처치 " + std::to_string(level_.kills) + "/12   레벨 " + std::to_string(level_.level)
               + "/3   강화 " + std::to_string(level_.weaponRank) + "/1",
           gold,
           .67f);
    std::ostringstream stats;
    stats << std::fixed << std::setprecision(2) << "공격 " << int(level_.Damage()) << "   간격 "
          << level_.Cooldown() << "초   사거리 " << int(level_.Range());
    r.Text(42, 199, stats.str(), muted, .66f);
    r.Text(42,
           223,
           level_.magnetTime > 0
               ? "자석 " + std::to_string(int(level_.magnetTime) + 1) + "초 / 획득 범위 440"
               : "기본 자동 줍기 / 회복은 체력이 부족할 때",
           cyan,
           .65f);

    r.Rect(w - 190, 110, 164, 176, Color(8, 17, 24, .94f));
    r.Text(w - 177, 119, "주변 적 / 전리품", muted, .67f);
    float mx = w - 108, my = 207;
    r.Line({mx - 66, my}, {mx + 66, my}, 1, Color(40, 66, 71));
    r.Line({mx, my - 55}, {mx, my + 55}, 1, Color(40, 66, 71));
    for (const auto& e : level_.Enemies())
    {
        float x = std::max(-65.f, std::min(65.f, e.position.x * .12f));
        float y = std::max(-54.f, std::min(54.f, e.position.y * .12f));
        r.Ellipse({mx + x, my + y}, e.boss ? 5.f : 3.f, e.boss ? 5.f : 3.f, red);
    }
    for (const auto& item : level_.Loot())
    {
        if (Distance(item.position.x, item.position.y) < 500)
        {
            r.Rect(mx + item.position.x * .12f, my + item.position.y * .12f, 2, 2, gold);
        }
    }
    if (level_.state == RunState::BossReady)
    {
        float d = Distance(level_.portal.x, level_.portal.y);
        float factor = .12f * std::min(1.f, 480.f / std::max(1.f, d));
        r.Ellipse({mx + level_.portal.x * factor, my + level_.portal.y * factor}, 5, 5, red);
        r.Text(w - 177, 260, "균열까지 " + std::to_string(int(d)), red, .65f);
    }
    r.Ellipse({mx, my}, 4, 4, cyan);
    for (const auto& enemy : level_.Enemies())
    {
        if (enemy.boss)
        {
            float bx = w * .5f - 200;
            r.Rect(bx - 12, 104, 424, 57, Color(15, 10, 21, .95f));
            r.Text(bx, 111, "보스 / 균열의 수문장", red, .85f);
            r.Rect(bx, 140, 400, 8, Color(52, 26, 40));
            r.Rect(bx, 140, 400 * enemy.health / enemy.maximumHealth, 8, red);
        }
    }
    float foot = h - 146;
    r.Rect(26, foot, 300, 77, Color(8, 17, 24, .96f));
    r.Text(43, foot + 9, "요괴 / 원거리 자동 조준", muted, .7f);
    r.Text(
        43, foot + 34, weapon_ ? "영혼포 / 자동 사격 중" : "동행 중 / Q로 무기 변신", cyan, .84f);
    r.Rect(345, foot, w - 371, 77, Color(8, 17, 24, .96f));
    r.Text(363, foot + 9, "동료의 조언", muted, .67f);
    r.Text(363, foot + 32, level_.Dialogue(weapon_), white, .78f);
    r.Rect(0, h - 49, w, 49, Color(8, 15, 23, .99f));
    r.Text(27,
           h - 40,
           "WASD 이동   Q 무기 변신 / 자동 사격   C 사거리   E 보스 소환   R 처음부터   ESC 종료",
           muted,
           .68f);
    const PostSettings& post = r.Effects();
    r.Text(27,
           h - 19,
           std::string("H 후처리 ") + (post.enabled ? "ON" : "OFF")
               + "   B 블룸   N 비네트   M 흐림   [ / ] 노출   0 효과 초기화",
           cyan,
           .6f);
    if (debug_)
    {
        std::ostringstream s;
        s << "청크 " << player_.cx << "," << player_.cy << " / " << chunks_.size() << "개";
        r.Text(32, 262, s.str(), cyan, .75f);
    }
    if (level_.state == RunState::Won || level_.state == RunState::Lost)
    {
        float cx = w * .5f, cy = h * .44f;
        r.Rect(cx - 265, cy - 58, 530, 132, Color(6, 13, 23, .96f));
        r.Text(cx - 215,
               cy - 42,
               level_.state == RunState::Won ? "레벨 1 완료 / 수문장 격파" : "쓰러졌습니다",
               level_.state == RunState::Won ? cyan : red,
               1.2f);
        r.Text(cx - 215, cy + 2, "R을 누르면 같은 맵에서 다시 시작합니다.", white, .8f);
        r.Text(cx - 215, cy + 35, "새 실행에서는 다른 시드의 맵이 생성됩니다.", muted, .73f);
    }
    r.SetScale(1);
}

void PrototypeWorld::Draw(PrototypeRenderer& r)
{
    zoom_ = std::min(r.Width() / 1280.f, r.Height() / 800.f);
    center_ = {r.Width() * .48f, r.Height() * .54f};

    struct Item
    {
        float x, y, w, d, h, depth;
        uint32_t seed;
        int kind;
    };

    std::vector<Item> items;
    for (const auto& entry : chunks_)
    {
        float x = float(entry.first.first - player_.cx) * Side - float(player_.x);
        float y = float(entry.first.second - player_.cy) * Side - float(player_.y);
        Floor(r, x, y, entry.second);
        for (int i = 0; i < 4; ++i)
        {
            const auto& plot = entry.second.plots[i];
            float bx = x + plot.x, by = y + plot.y;
            Point p = Project(bx + 72, by + 70);
            if (p.x < -250 || p.x > r.Width() + 250 || p.y < 0 || p.y > r.Height() + 350)
            {
                continue;
            }
            items.push_back({bx,
                             by,
                             plot.width,
                             plot.depth,
                             entry.second.heights[i],
                             bx + by + plot.width + plot.depth,
                             entry.second.seed + uint32_t(i),
                             0});
        }
        items.push_back({x + 123, y + 20, 0, 0, 95, x + y + 143, 0, 1});
    }
    items.push_back({0, 0, 0, 0, 0, 0, 0, 2});
    if (weapon_ && level_.showRange)
    {
        Ring(r, 0, 0, level_.Range(), Color(90, 220, 199, .4f));
    }
    for (size_t n = 0; n < level_.Enemies().size(); ++n)
    {
        const auto& e = level_.Enemies()[n];
        items.push_back(
            {e.position.x, e.position.y, 0, 0, 0, e.position.x + e.position.y, uint32_t(n), 4});
        if (e.windup > 0)
        {
            Ring(
                r, e.attackPosition.x, e.attackPosition.y, 105, Emissive(Color(255, 60, 90), 2), 3);
            Ring(r,
                 e.attackPosition.x,
                 e.attackPosition.y,
                 105 * (1 - e.windup / 1.1f),
                 Color(255, 130, 120),
                 2);
        }
    }
    for (size_t n = 0; n < level_.Loot().size(); ++n)
    {
        const auto& l = level_.Loot()[n];
        items.push_back(
            {l.position.x, l.position.y, 0, 0, 0, l.position.x + l.position.y, uint32_t(n), 5});
    }
    if (level_.state == RunState::BossReady)
    {
        float x = level_.portal.x, y = level_.portal.y;
        items.push_back({x, y, 0, 0, 0, x + y, 0, 3});
    }
    std::sort(items.begin(),
              items.end(),
              [](const Item& a, const Item& b)
              {
                  return a.depth < b.depth;
              });
    for (const Item& i : items)
    {
        if (i.kind == 0)
        {
            Building(r, i.x, i.y, i.w, i.d, i.h, i.seed);
        }
        else if (i.kind == 2)
        {
            Player(r);
        }
        else if (i.kind == 3)
        {
            Rift(r, i.x, i.y);
        }
        else if (i.kind == 4)
        {
            Enemy(r, level_.Enemies()[i.seed]);
        }
        else if (i.kind == 5)
        {
            Loot(r, level_.Loot()[i.seed]);
        }
        else
        {
            Point a = Project(i.x, i.y), b = Project(i.x, i.y, 95), c{b.x + 24 * zoom_, b.y};
            r.Ellipse(a, 43 * zoom_, 15 * zoom_, Color(79, 199, 177, .09f));
            r.Line(a, b, 3 * zoom_, Color(69, 90, 100));
            r.Line(b, c, 3 * zoom_, Color(114, 148, 146));
            r.Glow(c, 65 * zoom_, Color(87, 218, 190, .9f));
            r.Line(c, {c.x - 13 * zoom_, c.y}, 3 * zoom_, Emissive(Color(183, 251, 215), 5));
        }
    }
    for (const auto& bullet : level_.Bullets())
    {
        Point p = Project(bullet.position.x, bullet.position.y, 29);
        Point tail = Project(bullet.position.x - bullet.velocity.x * .025f,
                             bullet.position.y - bullet.velocity.y * .025f,
                             29);
        r.Line(tail, p, 4 * zoom_, Emissive(Color(136, 255, 225), 5));
        r.Glow(p, 15 * zoom_, Color(94, 244, 215));
    }
    for (const auto& effect : level_.Effects())
    {
        Point p = Project(effect.position.x, effect.position.y, 65 + (1 - effect.life) * 25);
        r.Text(p.x, p.y, effect.text, Color(255, 225, 173, effect.life), .7f * zoom_);
    }
    // Rain is decorative, deterministic and independent of world generation.
    for (int i = 0; i < 145; ++i)
    {
        float x = std::fmod(float(Mix(i) % 10000) + time_ * 42, float(r.Width()));
        float y = std::fmod(float(Mix(i + 150) % 10000) + time_ * 380, float(r.Height()));
        r.Line({x, y}, {x - 5, y + 16}, 1, Color(117, 155, 171, .16f));
    }
}

void PrototypeWorld::Ring(
    PrototypeRenderer& r, float x, float y, float radius, Ink ink, float width)
{
    for (int i = 0; i < 64; ++i)
    {
        float a = i * Pi / 32, b = (i + 1) * Pi / 32;
        r.Line(Project(x + std::cos(a) * radius, y + std::sin(a) * radius),
               Project(x + std::cos(b) * radius, y + std::sin(b) * radius),
               width * zoom_,
               ink);
    }
}

void PrototypeWorld::Enemy(PrototypeRenderer& r, const FarmEnemy& enemy)
{
    Point p = Project(enemy.position.x, enemy.position.y);
    float z = zoom_ * (enemy.boss ? 1.8f : 1.f);
    Ink flesh = enemy.hitFlash > 0 ? Color(255, 220, 211) : Color(126, 49, 69);
    r.Ellipse(p, 18 * z, 6 * z, Color(0, 0, 0, .7f));
    r.Line({p.x - 5 * z, p.y - 21 * z}, {p.x - 11 * z, p.y}, 5 * z, Color(57, 31, 45));
    r.Line({p.x + 5 * z, p.y - 21 * z}, {p.x + 11 * z, p.y}, 5 * z, Color(57, 31, 45));
    r.Quad({p.x - 12 * z, p.y - 43 * z},
           {p.x + 12 * z, p.y - 43 * z},
           {p.x + 8 * z, p.y - 15 * z},
           {p.x - 8 * z, p.y - 15 * z},
           flesh);
    r.Ellipse({p.x, p.y - 49 * z}, 11 * z, 12 * z, flesh);
    r.Triangle({p.x - 10 * z, p.y - 50 * z},
               {p.x - 18 * z, p.y - 72 * z},
               {p.x - 2 * z, p.y - 57 * z},
               Color(222, 180, 154));
    r.Triangle({p.x + 10 * z, p.y - 50 * z},
               {p.x + 18 * z, p.y - 72 * z},
               {p.x + 2 * z, p.y - 57 * z},
               Color(222, 180, 154));
    r.Line({p.x - 7 * z, p.y - 50 * z},
           {p.x + 7 * z, p.y - 50 * z},
           3 * z,
           Emissive(Color(255, 91, 105), 3));
    r.Line({p.x - 11 * z, p.y - 39 * z}, {p.x - 22 * z, p.y - 16 * z}, 5 * z, flesh);
    r.Line({p.x + 11 * z, p.y - 39 * z}, {p.x + 22 * z, p.y - 16 * z}, 5 * z, flesh);
    r.Rect(p.x - 17 * z, p.y - 80 * z, 34 * z, 3 * z, Color(40, 19, 32));
    r.Rect(p.x - 17 * z,
           p.y - 80 * z,
           34 * z * enemy.health / enemy.maximumHealth,
           3 * z,
           Color(247, 94, 117));
}

void PrototypeWorld::Loot(PrototypeRenderer& r, const FarmLoot& item)
{
    Point p = Project(item.position.x, item.position.y, 10 + std::sin(item.age * 3) * 3);
    float z = zoom_;
    Ink color = item.kind == LootKind::Upgrade  ? Color(255, 192, 90)
                : item.kind == LootKind::Heal   ? Color(111, 250, 135)
                : item.kind == LootKind::Magnet ? Color(207, 124, 255)
                                                : Color(90, 222, 255);
    r.Glow(p, 20 * z, color);
    r.Quad({p.x, p.y - 8 * z},
           {p.x + 7 * z, p.y},
           {p.x, p.y + 8 * z},
           {p.x - 7 * z, p.y},
           Emissive(color, 2));
    const char* label = item.kind == LootKind::Upgrade  ? "강화"
                        : item.kind == LootKind::Heal   ? "회복"
                        : item.kind == LootKind::Magnet ? "자석"
                                                        : "영혼석";
    r.Text(p.x - 15 * z, p.y + 12 * z, label, color, .5f * z);
}

void PrototypeWorld::DrawUI(PrototypeRenderer& r)
{
    Hud(r);
}

bool PrototypeWorld::SelfTest()
{
    auto require = [](bool pass, const char* name)
    {
        std::cout << (pass ? "PASS " : "FAIL ") << name << '\n';
        return pass;
    };
    bool ok = LevelOne::SelfTest();
    Location negative;
    negative.x = -1;
    negative.y = -641;
    negative.Normalize();
    ok &= require(negative.cx == -1 && negative.cy == -2 && negative.x == 639 && negative.y == 639,
                  "negative chunk normalization");
    auto a = Generate({-41, 9000000000000LL}), b = Generate({-41, 9000000000000LL});
    ok &= require(a.seed == b.seed && a.heights[2] == b.heights[2],
                  "deterministic distant generation");
    auto different = Generate({-41, 9000000000000LL}, 2027);
    ok &= require(a.seed != different.seed && a.plots[0].width != different.plots[0].width,
                  "different seeds change building footprints");
    PrototypeWorld world;
    uint32_t first = world.chunks_.at({0, 0}).seed;
    world.player_.cx = 8000000000000LL;
    world.Stream();
    ok &= require(world.chunks_.size() == 25 && world.chunks_.count({0, 0}) == 0,
                  "bounded resident chunks after travel");
    world.player_ = Location{};
    world.Stream();
    ok &= require(world.chunks_.at({0, 0}).seed == first, "return regenerates same chunk");
    auto plot = world.chunks_.at({0, 0}).plots[0];
    Location wall;
    wall.x = plot.x + 20;
    wall.y = plot.y + 20;
    ok &= require(world.Blocked(wall) && !world.Blocked(world.player_),
                  "building collision and clear spawn");
    double before = world.player_.x;
    world.Key('d', true);
    world.Update(.04f);
    world.Key('d', false);
    ok &= require(world.player_.x != before, "input movement");
    world.Key('q', true);
    world.Key('q', true);
    ok &= require(world.weapon_, "transform is edge triggered");
    world.Key('q', false);
    world.level_.level = 5;
    world.level_.health = 12;
    world.Key('r', true);
    ok &= require(world.level_.level == 1 && world.level_.health == 100 && !world.weapon_,
                  "restart resets progression and player");

    bool connected = true;
    for (uint32_t seed = 1; seed <= 128 && connected; ++seed)
    {
        PrototypeWorld sample(seed);
        for (int cy = -1; cy <= 1 && connected; ++cy)
        {
            for (int cx = -1; cx <= 1 && connected; ++cx)
            {
                bool free[1024] = {}, seen[1024] = {};
                int total = 0;
                for (int y = 0; y < 32; ++y)
                {
                    for (int x = 0; x < 32; ++x)
                    {
                        Location p;
                        p.cx = cx;
                        p.cy = cy;
                        p.x = x * 20 + 10;
                        p.y = y * 20 + 10;
                        free[y * 32 + x] = !sample.Blocked(p, 20);
                        total += free[y * 32 + x] ? 1 : 0;
                    }
                }
                std::queue<int> queue;
                queue.push(0);
                seen[0] = true;
                int count = 0;
                while (!queue.empty())
                {
                    int current = queue.front();
                    queue.pop();
                    ++count;
                    const int dx[] = {-1, 1, 0, 0}, dy[] = {0, 0, -1, 1};
                    for (int i = 0; i < 4; ++i)
                    {
                        int nx = current % 32 + dx[i], ny = current / 32 + dy[i];
                        if (nx < 0 || ny < 0 || nx >= 32 || ny >= 32)
                        {
                            continue;
                        }
                        int next = ny * 32 + nx;
                        if (free[next] && !seen[next])
                        {
                            seen[next] = true;
                            queue.push(next);
                        }
                    }
                }
                connected = count == total;
                for (int i = 0; i < 32; ++i)
                {
                    connected = connected && free[i] && free[31 * 32 + i] && free[i * 32]
                                && free[i * 32 + 31];
                }
            }
        }
    }
    ok &= require(connected, "128 seeds / 1152 chunks: all walkable cells and exits connected");
    return ok;
}

void PrototypeWorld::PreviewBoss()
{
    // Only invoked by the screenshot harness, never by a gameplay key.
    level_ = LevelOne(seed_);
    level_.level = 4;
    level_.weaponRank = 3;
    level_.kills = 12;
    level_.pickups = 24;
    level_.health = level_.MaximumHealth();
    level_.state = RunState::BossReady;
    level_.portal = {80, 0};
    level_.Interact(
        [&](float x, float y, float radius)
        {
            return Walkable(x, y, radius);
        });
    weapon_ = true;
}
