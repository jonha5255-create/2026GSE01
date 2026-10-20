#include "stdafx.h"
#include "PrototypeWorld.h"
#include "RenderDiagnostics.h"
#include "WorldMonitor.h"
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

    uint64_t NextWorldId()
    {
        static uint64_t id = 0;
        return ++id;
    }

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
    auto chunk = GenerateCityChunk(c.first, c.second, seed);
    chunk.bossRegion = c == Coord{2, 0} ? 0 : c == Coord{0, 2} ? 1 : c == Coord{-2, 0} ? 2 : -1;
    if (chunk.bossRegion >= 0)
    {
        // Preserve roads, but reserve a broad, accessible clearing for the encounter.
        for (size_t i = chunk.plots.size(); i-- > 0;)
        {
            const auto& lot = chunk.plots[i];
            if (lot.x < 510 && lot.x + lot.width > 130 && lot.y < 510 && lot.y + lot.depth > 130)
            {
                chunk.plots.erase(chunk.plots.begin() + i);
                chunk.heights.erase(chunk.heights.begin() + i);
                chunk.styles.erase(chunk.styles.begin() + i);
            }
        }
    }
    return chunk;
}

PrototypeWorld::PrototypeWorld(uint32_t seed, bool living)
    : monitorWorldId_(NextWorldId()), seed_(seed), level_(seed)
{
    level_.Graph().Spawn<RainActor>(NoActor);
    level_.Graph().Spawn<HudActor>(NoActor);
    Stream();
    if (living)
    {
        level_.society.Initialize(level_.Graph(), seed);
        level_.Player().SelectYokai(YokaiForm::Sword);
        Stream();
    }
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
    auto& graph = level_.Graph();
    for (auto it = chunkActors_.begin(); it != chunkActors_.end();)
    {
        if (!chunks_.count(it->first))
        {
            graph.Destroy(it->second);
            it = chunkActors_.erase(it);
        }
        else
        {
            ++it;
        }
    }
    graph.CollectDestroyed();
    for (const auto& entry : chunks_)
    {
        ActorId id = NoActor;
        auto found = chunkActors_.find(entry.first);
        if (found != chunkActors_.end() && graph.Get(found->second))
        {
            id = found->second;
        }
        else
        {
            auto& chunk = graph.Spawn<ChunkActor>(NoActor, entry.second);
            RenderDiagnostics::Get().Event(
                "city_chunk",
                "x=" + std::to_string(entry.first.first)
                    + "; y=" + std::to_string(entry.first.second)
                    + "; buildings=" + std::to_string(entry.second.plots.size())
                    + "; roads=" + std::to_string(entry.second.roads.size())
                    + "; district=" + std::to_string(entry.second.district));
            id = chunk.Id();
            chunkActors_[entry.first] = id;
            graph.Spawn<FloorActor>(id, entry.second);
            for (size_t i = 0; i < entry.second.plots.size(); ++i)
            {
                const auto& plot = entry.second.plots[i];
                auto& building = graph.Spawn<BuildingActor>(id,
                                                            plot.width,
                                                            plot.depth,
                                                            entry.second.heights[i],
                                                            entry.second.seed + uint32_t(i));
                building.local = {plot.x, plot.y};
                building.style = entry.second.styles[i];
                building.villageIndex = entry.second.villageIndex;
                building.homeSlot = plot.homeSlot;
                chunk.buildings.push_back(building.Id());
            }
            if (entry.second.village)
            {
                graph.Spawn<LampActor>(id).local = {entry.second.roadX + 42,
                                                    entry.second.roadY + 42};
            }
        }
        graph.Get(id)->SetPosition(
            {float(entry.first.first - player_.cx) * Side - float(player_.x),
             float(entry.first.second - player_.cy) * Side - float(player_.y)});
    }
    if (level_.society.enabled)
    {
        for (const auto& entry : chunkActors_)
        {
            auto* chunk = graph.Get<ChunkActor>(entry.second);
            if (chunk && !chunk->populated)
            {
                PopulateChunk(entry.first, *chunk);
                chunk->populated = true;
            }
        }
    }
}

void PrototypeWorld::PopulateChunk(Coord coord, ChunkActor& chunk)
{
    auto& graph = level_.Graph();
    bool village = IsVillageChunk(coord.first, coord.second);
    if (village && coord == Coord{0, 0})
    {
        return;
    }
    uint64_t random = Mix(chunk.data.seed ^ 0x73829ULL);
    auto next = [&]()
    {
        random = Mix(random);
        return uint32_t(random);
    };
    ActorId parent = chunk.Id();
    if (village)
    {
        // Towns and their residents are persistent roots, not streamed chunk children.
        return;
    }
    int count = 3 + int(next() % 3);
    if (chunk.data.bossRegion >= 0)
    {
        int region = chunk.data.bossRegion;
        const char* names[] = {"핏빛 제단의 뿔왕", "독안개 포식견", "붕괴 석정의 거신"};
        auto& boss = graph.Spawn<NpcActor>(parent, names[region], NpcJob::Wraith);
        boss.local = boss.home = {320, 320};
        boss.wilderness = boss.fieldBoss = true;
        boss.bossRegion = boss.monsterForm = region;
        boss.health = boss.maximumHealth = 420 + region * 90.f;
        boss.gold = 85;
        boss.goods = 6;
        RenderDiagnostics::Get().Event("quest_boss_spawn",
                                       "region=" + std::to_string(region)
                                           + "; actor=" + std::to_string(boss.Id()));
    }
    int spawned = 0;
    for (int attempt = 0; attempt < 80 && spawned < count; ++attempt)
    {
        FarmPoint point = {40.f + float(next() % 560), 40.f + float(next() % 560)};
        auto origin = chunk.Position();
        auto position = FarmPoint{origin.x + point.x, origin.y + point.y};
        if (!Walkable(position.x, position.y, 16))
        {
            continue;
        }
        bool boss = chunk.data.bossRegion < 0 && spawned == 0 && chunk.data.seed % 3 == 0;
        const char* monsterNames[] = {"뿔 잔향귀", "시체 사냥개", "폐허 석괴"};
        int form = int((chunk.data.seed + uint32_t(spawned)) % 3);
        auto& monster = graph.Spawn<NpcActor>(parent,
                                              boss ? std::string("폐허의 포식자")
                                                   : std::string(monsterNames[form]) + " "
                                                         + std::to_string(spawned + 1),
                                              NpcJob::Wraith);
        monster.local = monster.home = point;
        monster.wilderness = true;
        monster.monsterForm = form;
        monster.health = monster.maximumHealth = 65 + float(next() % 46);
        monster.gold = 12;
        monster.goods = 1;
        if (boss)
        {
            monster.fieldBoss = true;
            monster.health = monster.maximumHealth = 360;
            monster.gold = 65;
            monster.goods = 5;
            RenderDiagnostics::Get().Event("field_boss_spawn",
                                           "actor=" + std::to_string(monster.Id()));
        }
        ++spawned;
    }
    RenderDiagnostics::Get().Event("wild_spawn",
                                   "chunk=" + std::to_string(coord.first) + ","
                                       + std::to_string(coord.second)
                                       + "; count=" + std::to_string(spawned));
}

bool PrototypeWorld::Blocked(const Location& p, float radius) const
{
    // Resident collision is controlled by the same actors as rendering.
    auto placed = chunkActors_.find({p.cx, p.cy});
    if (placed != chunkActors_.end())
    {
        const auto& graph = level_.Graph();
        auto* chunk = graph.Get<ChunkActor>(placed->second);
        if (!chunk || !graph.Active(chunk->Id()))
        {
            return false;
        }
        float x = float(p.cx - player_.cx) * Side + float(p.x - player_.x);
        float y = float(p.cy - player_.cy) * Side + float(p.y - player_.y);
        for (auto id : chunk->buildings)
        {
            auto* building = graph.Get<BuildingActor>(id);
            if (!building || !graph.Active(id))
            {
                continue;
            }
            auto position = building->Position();
            float dx = std::max(position.x - x, std::max(0.f, x - position.x - building->width));
            float dy = std::max(position.y - y, std::max(0.f, y - position.y - building->depth));
            if (dx * dx + dy * dy < radius * radius)
            {
                return true;
            }
        }
        return false;
    }
    // Unloaded procedural chunks still support navigation/collision queries.
    auto it = chunks_.find({p.cx, p.cy});
    if (it == chunks_.end() && !collisionChunks_.count({p.cx, p.cy}))
    {
        if (collisionChunks_.size() >= 256)
        {
            collisionChunks_.clear();
        }
        collisionChunks_.emplace(Coord{p.cx, p.cy}, Generate({p.cx, p.cy}, seed_));
    }
    const auto& chunk = it == chunks_.end() ? collisionChunks_.at({p.cx, p.cy}) : it->second;
    for (size_t i = 0; i < chunk.plots.size(); ++i)
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
    if (key >= '1' && key <= '3' && level_.state != RunState::Lost)
    {
        level_.Player().SelectYokai(static_cast<YokaiForm>(key - '1'));
        level_.Weapon().skillTime = -1;
        level_.Weapon().muzzleFlash = 0;
        level_.Weapon().swingTime = 0;
        level_.society.dialogue = std::string(level_.Player().YokaiName())
                                  + " 동행 상태로 전환했습니다. Q로 무기화하세요.";
    }
    if (key == 'q')
    {
        level_.Player().weapon = !level_.Player().weapon;
        level_.Weapon().skillTime = -1;
        level_.Player().transformPhase = 1;
        level_.Weapon().swingTime = 0;
    }
    if (key == 'c')
    {
        level_.ToggleRange();
    }
    if (key == 'v')
    {
        distortion_ = !distortion_;
    }
    if (key == 'm')
    {
        mapExpanded_ = !mapExpanded_;
    }
    if (key == 'g')
    {
        debug_ = !debug_;
    }
    if (key == 'r')
    {
        *this = PrototypeWorld(seed_, level_.society.enabled);
        return;
    }
    if (level_.society.enabled)
    {
        auto context = level_.Context(
            [&](float x, float y, float radius)
            {
                return Walkable(x, y, radius);
            });
        if (key == 'e')
        {
            level_.society.Interact(context);
        }
        if (key == 't')
        {
            level_.society.PlayerTrade(false, context);
        }
        if (key == 'y')
        {
            level_.society.PlayerTrade(true, context);
        }
        if (key == 'p')
        {
            level_.society.TogglePK();
        }
        if (key == 'f')
        {
            level_.society.PlayerStrike(true, context);
        }
        if (key == 'j')
        {
            level_.society.PlayerStrike(false, context);
        }
        if (key == '\t')
        {
            level_.society.panel = !level_.society.panel;
        }
    }
    else if (key == 'e')
    {
        level_.Interact(
            [&](float x, float y, float radius)
            {
                return Walkable(x, y, radius);
            });
    }
}

void PrototypeWorld::MouseSkill()
{
    if (!level_.Player().enabled || mapExpanded_)
    {
        return;
    }
    auto context = level_.Context(
        [&](float x, float y, float radius)
        {
            return Walkable(x, y, radius);
        });
    level_.Weapon().CastSkill(context);
}

void PrototypeWorld::MouseAttack(int x, int y, int width, int height)
{
    if (mapExpanded_ || width <= 0 || height <= 0 || x < 0 || y < 0 || x >= width || y >= height
        || level_.state == RunState::Lost || level_.state == RunState::Won
        || !level_.Player().enabled)
    {
        return;
    }
    // Match scene projection and logical HUD dimensions, including resized windows.
    zoom_ = std::min(width / 1280.f, height / 800.f);
    center_ = {width * .48f, height * .54f};
    float uiScale = std::min(1.f, zoom_);
    float ux = x / uiScale, uy = y / uiScale;
    float w = width / uiScale, h = height / uiScale;
    if (level_.society.enabled
        && (uy <= 91 || uy >= h - 148 || (ux >= 20 && ux <= 360 && uy >= 109 && uy <= 495)
            || (ux >= w - 328 && ux <= w - 20 && uy >= 109 && uy <= 331)
            || (level_.society.panel && ux >= w - 328 && ux <= w - 20 && uy >= 341 && uy <= 625)))
    {
        return;
    }
    auto context = level_.Context(
        [&](float px, float py, float radius)
        {
            return Walkable(px, py, radius);
        });
    Actor* selected = nullptr;
    float closest = 100000;
    auto pick = [&](Actor& actor)
    {
        Point p = Project(actor.Position().x, actor.Position().y);
        float dx = (x - p.x) / zoom_, dy = (y - p.y) / zoom_;
        auto* pickedNpc = dynamic_cast<NpcActor*>(&actor);
        if (pickedNpc && (pickedNpc->storyGuardian || pickedNpc->fieldBoss))
        {
            dx /= 1.6f;
            dy /= 1.6f;
        }
        if (std::abs(dx) > 22 || dy < -60 || dy > 8)
        {
            return;
        }
        float distance = dx * dx + (dy + 27) * (dy + 27);
        if (distance < closest)
        {
            selected = &actor;
            closest = distance;
        }
    };
    if (level_.society.enabled)
    {
        for (auto* npc : level_.Graph().Query<NpcActor>())
        {
            if (npc->health > 0 && level_.Graph().Visible(npc->Id()))
            {
                pick(*npc);
            }
        }
        if (auto* npc = dynamic_cast<NpcActor*>(selected))
        {
            level_.society.PlayerStrike(npc->job != NpcJob::Wraith, context, npc->Id(), true);
        }
    }
    else
    {
        for (auto* enemy : level_.Enemies())
        {
            if (enemy->health > 0 && level_.Graph().Visible(enemy->Id()))
            {
                pick(*enemy);
            }
        }
        if (auto* enemy = dynamic_cast<EnemyActor*>(selected))
        {
            auto& weapon = level_.Weapon();
            if (level_.Player().weapon && level_.Player().yokai == YokaiForm::Ranged)
            {
                weapon.ShootAt(*enemy, context);
            }
            else if (weapon.shotTimer_ <= 0
                     && Length(enemy->Position()) < level_.Player().StrikeRange()
                     && Navigation::Sight({}, enemy->Position(), context.walkable))
            {
                enemy->TakeDamage(level_.Player().weapon ? level_.Player().StrikeDamage()
                                                         : level_.Damage(),
                                  context);
                weapon.StartSwing(enemy->Position());
                weapon.shotTimer_ = level_.Player().StrikeCooldown();
            }
        }
    }
    if (!selected && level_.Player().weapon && level_.Player().yokai != YokaiForm::Ranged
        && level_.Weapon().shotTimer_ <= 0)
    {
        float sx = ((x - center_.x) / zoom_ - cameraLag_.x) / .85f;
        float sy = ((y - center_.y) / zoom_ - cameraLag_.y) / .425f;
        level_.Weapon().StartSwing({(sx + sy) * .5f, (sy - sx) * .5f});
        level_.Weapon().shotTimer_ = level_.Player().StrikeCooldown();
    }
}

void PrototypeWorld::Update(float dt)
{
    dt = std::max(0.f, std::min(dt, .05f));
    time_ += dt;
    level_.Player().moveInput = {float(keys_['d']) - float(keys_['a']),
                                 float(keys_['s']) - float(keys_['w'])};
    level_.Update(
        dt,
        [&](float x, float y, float radius)
        {
            return Walkable(x, y, radius);
        },
        [&](float dx, float dy)
        {
            Move(dx, dy);
        });
    float follow = 1 - std::exp(-10 * dt);
    cameraLag_ = Lerp(cameraLag_, {0, 0}, follow);
    WorldMonitor::Get().Publish(level_, player_, chunks_.size(), seed_, monitorWorldId_);
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
              "floor:city-v11:" + std::to_string(chunk.bossRegion) + ":"
                  + std::to_string(chunk.seed) + ":" + std::to_string(chunk.roadX) + ":"
                  + std::to_string(chunk.roadY) + ":" + std::to_string(debug_),
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
    auto rect = [&](float a, float b, float w, float d, Ink color)
    {
        float x0 = std::max(0.f, a), y0 = std::max(0.f, b);
        float x1 = std::min(Side, a + w), y1 = std::min(Side, b + d);
        if (x1 > x0 && y1 > y0)
        {
            r.Quad(p(x0, y0), p(x1, y0), p(x1, y1), p(x0, y1), color);
        }
    };
    rect(0, 0, Side, Side, chunk.village ? Color(44, 57, 48) : Color(49, 43, 37));
    if (chunk.bossRegion >= 0)
    {
        const Ink earth[] = {Color(75, 30, 32), Color(34, 62, 44), Color(49, 49, 72)};
        const Ink marks[] = {Color(173, 63, 60), Color(112, 160, 76), Color(115, 132, 192)};
        int region = chunk.bossRegion;
        for (int i = 0; i < 48; ++i)
        {
            float a = i * Pi / 24, b = (i + 1) * Pi / 24;
            float radius = 182 + std::sin(i * 2.3f) * 16;
            r.Triangle(p(320, 320),
                       p(320 + std::cos(a) * radius, 320 + std::sin(a) * radius),
                       p(320 + std::cos(b) * radius, 320 + std::sin(b) * radius),
                       earth[region]);
            r.Line(p(320 + std::cos(a) * 125, 320 + std::sin(a) * 125),
                   p(320 + std::cos(b) * 125, 320 + std::sin(b) * 125),
                   2,
                   marks[region]);
        }
        for (int i = 0; i < 14; ++i)
        {
            float a = i * 2.4f;
            float px = 320 + std::cos(a) * (50 + i * 8.f);
            float py = 320 + std::sin(a) * (50 + i * 8.f);
            rect(px, py, region == 2 ? 23.f : 9.f, region == 1 ? 20.f : 7.f, marks[region]);
        }
    }
    if (!chunk.village)
    {
        for (int i = 0; i < 65; ++i)
        {
            auto detail = Mix(chunk.seed + i * 7919);
            float a = 32 + float(detail % 576), b = 32 + float((detail >> 16) % 576);
            if (chunk.bossRegion >= 0 && Distance(a - 320, b - 320) < 175)
            {
                continue;
            }
            float radius = 9 + float((detail >> 8) % 21);
            Ink soil = i % 3 == 0 ? Color(43, 47, 36) : Color(43, 38, 33);
            for (int edge = 0; edge < 7; ++edge)
            {
                float angle = edge * Pi * 2 / 7;
                float next = (edge + 1) * Pi * 2 / 7;
                r.Triangle(p(a, b),
                           p(a + std::cos(angle) * radius, b + std::sin(angle) * radius * .6f),
                           p(a + std::cos(next) * radius, b + std::sin(next) * radius * .6f),
                           soil);
            }
            if (i % 3 == 0)
            {
                r.Line(p(a - 7, b - 4), p(a, b + 2), 1, Color(28, 27, 26));
                r.Line(p(a, b + 2), p(a + 12, b - 1), 1, Color(28, 27, 26));
                r.Triangle(p(a + 3, b + 5), p(a + 8, b + 3), p(a + 10, b + 8), Color(76, 71, 61));
            }
        }
        // Draw every shoulder before pavement so borders cannot cross junctions.
        for (const auto& road : chunk.roads)
        {
            if (!road.arterial)
            {
                continue;
            }
            rect(road.x - 9, road.y - 9, road.width + 18, road.depth + 18, Color(52, 46, 38));
            rect(road.x - 4, road.y - 4, road.width + 8, road.depth + 8, Color(65, 58, 46));
        }
        auto junctionCorners = [&](float reach, Ink ink)
        {
            for (size_t i = 0; i < chunk.roads.size(); ++i)
            {
                const auto& a = chunk.roads[i];
                if (!a.arterial)
                {
                    continue;
                }
                for (size_t j = i + 1; j < chunk.roads.size(); ++j)
                {
                    const auto& b = chunk.roads[j];
                    if (!b.arterial || (a.width > a.depth) == (b.width > b.depth))
                    {
                        continue;
                    }
                    float x0 = std::max(a.x, b.x), x1 = std::min(a.x + a.width, b.x + b.width);
                    float y0 = std::max(a.y, b.y), y1 = std::min(a.y + a.depth, b.y + b.depth);
                    if (x0 >= x1 || y0 >= y1)
                    {
                        continue;
                    }
                    for (int sx : {-1, 1})
                    {
                        for (int sy : {-1, 1})
                        {
                            float cx = sx < 0 ? x0 : x1, cy = sy < 0 ? y0 : y1;
                            r.Triangle(
                                p(cx, cy), p(cx + sx * reach, cy), p(cx, cy + sy * reach), ink);
                        }
                    }
                }
            }
        };
        junctionCorners(18, Color(52, 46, 38));
        junctionCorners(14, Color(65, 58, 46));
        for (const auto& road : chunk.roads)
        {
            if (road.arterial)
            {
                rect(road.x, road.y, road.width, road.depth, Color(36, 43, 44));
            }
        }
        junctionCorners(10, Color(36, 43, 44));
        return;
    }
    for (size_t index = 0; index < chunk.plots.size(); ++index)
    {
        const auto& lot = chunk.plots[index];
        if (chunk.styles[index] == 2)
        {
            continue;
        }
        rect(lot.x - 5,
             lot.y - 5,
             lot.width + 10,
             lot.depth + 10,
             chunk.village ? Color(54, 61, 55) : Color(48, 43, 38));
        // Each lot has its own forecourt rather than a repeated full-chunk grid.
        r.Line(p(lot.x, lot.y - 8), p(lot.x + lot.width, lot.y - 8), 1, Color(106, 110, 98));
    }
    for (const auto& road : chunk.roads)
    {
        rect(road.x - 13,
             road.y - 13,
             road.width + 26,
             road.depth + 26,
             chunk.village ? Color(48, 55, 50) : Color(53, 47, 39));
        rect(road.x - 6,
             road.y - 6,
             road.width + 12,
             road.depth + 12,
             chunk.village ? Color(58, 62, 56) : Color(57, 51, 43));
    }
    for (const auto& road : chunk.roads)
    {
        rect(road.x,
             road.y,
             road.width,
             road.depth,
             chunk.village   ? Color(36, 43, 44)
             : road.arterial ? Color(39, 40, 37)
                             : Color(59, 51, 41));
        if (!chunk.village)
        {
            bool horizontal = road.width > road.depth;
            for (int i = 0; i < 12; ++i)
            {
                auto detail = Mix(chunk.seed + i * 1337 + (horizontal ? 9 : 1));
                float t = i * 56.f, length = 18 + float(detail % 25);
                float offset = float((detail >> 12) % 7) - 3;
                rect(horizontal ? t : road.x + offset,
                     horizontal ? road.y + offset : t,
                     horizontal ? length : 5,
                     horizontal ? 5 : length,
                     Color(58, 51, 42));
                rect(horizontal ? t : road.x + road.width - 4 + offset,
                     horizontal ? road.y + road.depth - 4 + offset : t,
                     horizontal ? length : 5,
                     horizontal ? 5 : length,
                     Color(56, 49, 41));
            }
        }
    }
    for (const auto& road : chunk.roads)
    {
        if (!road.arterial)
        {
            continue;
        }
        bool horizontal = road.width > road.depth;
        float center = horizontal ? road.y + road.depth / 2 : road.x + road.width / 2;
        for (int i = 0; i < 12; ++i)
        {
            float t = i * 56.f;
            float junction = horizontal ? chunk.roadX : chunk.roadY;
            if (std::abs(t - junction) < 50)
            {
                continue;
            }
            if (horizontal)
            {
                rect(t, center - 1, chunk.village ? 22.f : 12.f, 1.5f, Color(111, 103, 73));
            }
            else
            {
                rect(center - 1, t, 1.5f, chunk.village ? 22.f : 12.f, Color(111, 103, 73));
            }
        }
    }
    if (!chunk.village)
    {
        return;
    }
    // Worn paving joins each gate to the road, without changing its collision clearance.
    for (float edge : {24.f, 612.f})
    {
        for (int row = 0; row < 3; ++row)
        {
            for (int col = -1; col <= 1; ++col)
            {
                float offset = row % 2 ? 4.f : 0.f;
                rect(chunk.roadX + col * 13 - 6 + offset,
                     edge + row * 8 - 12,
                     11,
                     6,
                     Color(65, 68, 60));
                rect(edge + row * 8 - 12,
                     chunk.roadY + col * 13 - 6 + offset,
                     6,
                     11,
                     Color(65, 68, 60));
            }
        }
    }
    float vw = chunk.roads[0].width, hw = chunk.roads[1].depth;
    for (int i = 0; i < 6; ++i)
    {
        float offset = i * 6.f;
        rect(chunk.roadX + vw / 2 + 9 + offset,
             chunk.roadY - hw / 2 + 3,
             3,
             hw - 6,
             Color(160, 170, 159));
        rect(chunk.roadX - vw / 2 + 3,
             chunk.roadY + hw / 2 + 9 + offset,
             vw - 6,
             3,
             Color(160, 170, 159));
    }
    // Drain covers and occasional curb-side parking bays follow the main street.
    for (int i = 0; i < 5; ++i)
    {
        float t = 190.f + i * 83;
        rect(t, chunk.roadY - hw / 2 - 9, 11, 4, Color(20, 30, 31));
        if ((chunk.seed + i) % 3 == 0)
        {
            r.Line(p(t, chunk.roadY + hw / 2 + 3),
                   p(t, chunk.roadY + hw / 2 + 12),
                   1,
                   Color(115, 123, 112));
            r.Line(p(t, chunk.roadY + hw / 2 + 12),
                   p(t + 35, chunk.roadY + hw / 2 + 12),
                   1,
                   Color(115, 123, 112));
        }
    }
    if (debug_)
    {
        r.Line(p(0, 0), p(Side, 0), 2, Color(67, 223, 192, .8f));
        r.Line(p(0, 0), p(0, Side), 2, Color(67, 223, 192, .8f));
    }
}

void PrototypeWorld::Building(PrototypeRenderer& r,
                              float x,
                              float y,
                              float w,
                              float d,
                              float h,
                              uint32_t seed,
                              bool residential)
{
    auto p = [&](float a, float b, float z = 0)
    {
        return Project(x + a, y + b, z);
    };
    Point a = p(0, 0, h), b = p(w, 0, h), c = p(w, d, h), e = p(0, d, h);
    bool haunted = !residential && (seed % 5 == 0) && distortion_;
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
        std::string key = "building:city-v3:" + std::to_string(residential) + ":"
                          + std::to_string(seed) + ":" + std::to_string(w) + ":" + std::to_string(d)
                          + ":" + std::to_string(h);
        LocalMesh(r,
                  key,
                  x,
                  y,
                  alpha,
                  [&]()
                  {
                      BuildingGeometry(r, 0, 0, w, d, h, seed, 1, false, residential);
                  });
        return;
    }
    BuildingGeometry(r, x, y, w, d, h, seed, alpha, true, residential);
}

void PrototypeWorld::BuildingGeometry(PrototypeRenderer& r,
                                      float x,
                                      float y,
                                      float w,
                                      float d,
                                      float h,
                                      uint32_t seed,
                                      float alpha,
                                      bool haunted,
                                      bool residential)
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
    const int tones[][3] = {{58, 65, 67}, {75, 58, 54}, {53, 65, 73}, {65, 69, 57}};
    const auto& facade = tones[seed % 4];
    r.Quad(e, c, p(w, d), p(0, d), ink(facade[0], facade[1], facade[2]));
    r.Quad(b, c, p(w, d), p(w, 0), ink(facade[0] - 15, facade[1] - 15, facade[2] - 12));
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
    r.Quad(p(w * .25f, d * .25f, h + 1),
           p(w * .65f, d * .25f, h + 1),
           p(w * .65f, d * .55f, h + 1),
           p(w * .25f, d * .55f, h + 1),
           ink(21, 35, 43));
    for (int i = 0; i < 5; ++i)
    {
        r.Line(p(w * .28f, d * (.28f + i * .05f), h + 2),
               p(w * .62f, d * (.28f + i * .05f), h + 2),
               1,
               ink(80, 101, 108));
    }
    r.Line(p(w - 25, 25, h), p(w - 25, 25, h + 38), 2, ink(102, 120, 122));
    r.Line(p(w - 25, 25, h + 29), p(w - 5, 25, h + 29), 1, ink(102, 120, 122));
    r.Quad(p(w * .4f, d + .5f, 0),
           p(w * .6f, d + .5f, 0),
           p(w * .6f, d + .5f, 25),
           p(w * .4f, d + .5f, 25),
           ink(16, 25, 30));
    if (h < 85)
    {
        r.Quad(p(4, d, 29),
               p(w - 4, d, 29),
               p(w - 4, d + 12, 25),
               p(4, d + 12, 25),
               seed % 2 ? ink(103, 57, 54) : ink(50, 91, 87));
    }
    Point sign = p(w * .5f, d + 1, std::min(40.f, h - 14));
    r.Rect(sign.x - 37 * zoom_, sign.y - 12 * zoom_, 74 * zoom_, 20 * zoom_, ink(9, 23, 29));
    Ink neon = Emissive(haunted ? ink(239, 81, 122) : ink(104, 223, 204), 2.5f);
    r.Line({sign.x - 36 * zoom_, sign.y + 8 * zoom_},
           {sign.x + 36 * zoom_, sign.y + 8 * zoom_},
           2,
           neon);
    const char* names[] = {"CLINIC", "MOTEL", "RECORDS", "NO SIGNAL"};
    r.Text(sign.x - 32 * zoom_,
           sign.y - 11 * zoom_,
           residential ? "주택" : names[seed % 4],
           neon,
           .62f * zoom_);
    if (haunted)
    {
        Point crack = p(w, d, h * .8f);
        r.Line(crack, p(w - 22, d, h * .5f), 3, Emissive(Color(214, 69, 115, alpha), 4));
        r.Glow(crack, 30 * zoom_, Color(214, 69, 115, alpha));
    }
}

void PrototypeWorld::Player(PrototypeRenderer& r, const PlayerActor& actor)
{
    auto position = actor.Position();
    Point p = Project(position.x, position.y);
    float z = zoom_, step = std::sin(level_.Player().walkPhase) * 3;
    const auto& weapon = level_.Weapon();
    auto facing =
        Unit({(weapon.aim.x - weapon.aim.y) * .85f, (weapon.aim.x + weapon.aim.y) * .425f});
    p.x += facing.x * weapon.SwingLunge() * z;
    p.y += facing.y * weapon.SwingLunge() * z;
    r.Ellipse({p.x, p.y + 2 * z}, 20 * z, 7 * z, Color(0, 0, 0, .65f));
    r.Glow({p.x, p.y - 20 * z}, 34 * z, Color(64, 217, 194, .14f));
    r.Line({p.x - 5 * z, p.y - 18 * z}, {p.x - 7 * z, p.y - step * z}, 6 * z, Color(12, 18, 28));
    r.Line({p.x + 5 * z, p.y - 18 * z}, {p.x + 7 * z, p.y + step * z}, 6 * z, Color(12, 18, 28));
    r.Quad({p.x - 9 * z, p.y - 41 * z},
           {p.x + 8 * z, p.y - 41 * z},
           {p.x + 15 * z, p.y - 13 * z},
           {p.x - 14 * z, p.y - 15 * z},
           Color(45, 63, 80));
    r.Line({p.x - 7 * z, p.y - 38 * z}, {p.x - 10 * z, p.y - 16 * z}, 3 * z, Color(85, 124, 137));
    r.Ellipse({p.x, p.y - 48 * z}, 7 * z, 9 * z, Color(205, 183, 165));
    r.Line({p.x + 5 * z, p.y - 49 * z}, {p.x + 3 * z, p.y - 42 * z}, 2 * z, Color(163, 134, 122));
    r.Ellipse({p.x - 7 * z, p.y - step * z}, 5 * z, 3 * z, Color(29, 36, 46));
    r.Ellipse({p.x + 7 * z, p.y + step * z}, 5 * z, 3 * z, Color(29, 36, 46));
    r.Quad({p.x + 3 * z, p.y - 39 * z},
           {p.x + 8 * z, p.y - 41 * z},
           {p.x + 15 * z, p.y - 13 * z},
           {p.x + 5 * z, p.y - 16 * z},
           Color(29, 43, 61));
    r.Line({p.x - 9 * z, p.y - 36 * z}, {p.x - 15 * z, p.y - 25 * z}, 7 * z, Color(39, 56, 72));
    r.Line({p.x - 15 * z, p.y - 25 * z}, {p.x - 11 * z, p.y - 18 * z}, 5 * z, Color(57, 77, 93));
    r.Ellipse({p.x - 11 * z, p.y - 17 * z}, 3 * z, 3 * z, Color(186, 166, 150));
    r.Line({p.x - 3 * z, p.y - 45 * z}, {p.x - 1 * z, p.y - 45 * z}, 1.4f * z, Color(34, 36, 42));
    r.Line({p.x + 2 * z, p.y - 45 * z}, {p.x + 4 * z, p.y - 45 * z}, 1.4f * z, Color(34, 36, 42));
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
}

void PrototypeWorld::Weapon(PrototypeRenderer& r, const WeaponActor& actor)
{
    auto* owner = level_.Graph().Get<PlayerActor>(actor.Parent());
    if (!owner)
    {
        return;
    }
    auto position = actor.Position();
    Point p = Project(position.x, position.y);
    int evolution = owner->Evolution();
    float z = zoom_ * (1 + evolution * .07f);
    Ink evolutionInk = owner->yokai == YokaiForm::Sword    ? Color(124, 208, 255)
                       : owner->yokai == YokaiForm::Ranged ? Color(255, 186, 82)
                                                           : Color(208, 126, 255);
    auto facing = Unit({(actor.aim.x - actor.aim.y) * .85f, (actor.aim.x + actor.aim.y) * .425f});
    p.x += facing.x * actor.SwingLunge() * z;
    p.y += facing.y * actor.SwingLunge() * z;
    if (actor.skillTime >= 0)
    {
        float t = actor.skillTime;
        float charge = std::min(1.f, t / .25f);
        float fade = std::max(0.f, 1 - t);
        Ink energy = Emissive(evolutionInk, 2);
        energy.a = fade;
        if (t < .25f)
        {
            Ring(r, position.x, position.y, 20 + charge * 35, energy, 2);
        }
        else if (actor.skillForm == YokaiForm::Sword)
        {
            float radius = 50 + std::min(1.f, (t - .25f) / .3f) * 95;
            Ring(r, position.x, position.y, radius, energy, 4);
            for (int i = 0; i < 24; ++i)
            {
                float a = i * Pi / 12 + t * 7;
                auto from = Project(
                    position.x + std::cos(a) * radius, position.y + std::sin(a) * radius, 14);
                auto to = Project(position.x + std::cos(a + .2f) * radius,
                                  position.y + std::sin(a + .2f) * radius,
                                  22);
                r.Line(from, to, 3 * z, energy);
            }
        }
        else if (actor.skillForm == YokaiForm::Claw)
        {
            for (int hit = 0; hit < 3; ++hit)
            {
                float elapsed = t - .25f - hit * .14f;
                if (elapsed < 0 || elapsed > .25f)
                {
                    continue;
                }
                float reach = 35 + elapsed * 200;
                Point center{p.x + facing.x * reach * z, p.y + facing.y * reach * z - 20 * z};
                for (int claw = -1; claw <= 1; ++claw)
                {
                    r.Line({center.x - 25 * z, center.y - (18 + claw * 8) * z},
                           {center.x + 25 * z, center.y + (18 - claw * 8) * z},
                           3 * z,
                           energy);
                }
            }
        }
        else
        {
            r.Glow(
                {p.x + facing.x * 30 * z, p.y - 25 * z + facing.y * 30 * z}, 45 * fade * z, energy);
            Ring(r, position.x, position.y, 40 + t * 50, energy, 2);
        }
    }
    // Both forms inherit the same carapace, ivory horns and soul-core palette.
    Ink shell = owner->yokai == YokaiForm::Sword    ? Color(42, 69, 91)
                : owner->yokai == YokaiForm::Ranged ? Color(92, 65, 36)
                                                    : Color(65, 41, 82);
    Ink ivory = Color(215, 219, 216);
    Ink core = Emissive(evolutionInk, 1.5f);
    float phase = actor.SwingProgress();
    float attack = actor.swingTime > 0 ? std::sin(phase * Pi) : 0;
    float heading = std::atan2(facing.y, facing.x);
    auto polar = [&](Point origin, float angle, float radius)
    {
        return Point{origin.x + std::cos(angle) * radius * z,
                     origin.y + std::sin(angle) * radius * z};
    };
    if (owner->weapon && owner->yokai == YokaiForm::Sword)
    {
        float sweep = phase < .2f    ? -1.45f - phase
                      : phase < .65f ? -1.65f + (phase - .2f) / .45f * 3.1f
                                     : 1.45f + (phase - .65f) * .25f;
        float angle = actor.swingTime > 0 ? heading + sweep * actor.swingSide : -.87f;
        Point shoulder{p.x + 4 * z, p.y - 32 * z};
        Point hand = polar(shoulder, angle, 13);
        Point tip = polar(hand, angle, 47);
        r.Line(shoulder, hand, 6 * z, Color(76, 91, 105));
        if (actor.swingTime > 0 && phase > .2f)
        {
            for (int i = 1; i <= 12; ++i)
            {
                float a0 = angle - actor.swingSide * i * .08f;
                r.Line(polar(shoulder, a0, 59),
                       polar(shoulder, a0 + actor.swingSide * .08f, 59),
                       (4 - i * .2f) * z,
                       Emissive(Color(159, 239, 219, attack * (1 - i / 14.f)), 2));
            }
        }
        r.Line(hand, tip, 7 * z, Color(25, 34, 42));
        r.Line(hand, tip, 2 * z, Emissive(Color(157, 236, 218), 3));
        r.Line(polar(hand, angle + Pi / 2, 7),
               polar(hand, angle - Pi / 2, 7),
               3 * z,
               Color(204, 159, 94));
        if (evolution > 0)
        {
            auto blade = [&](float x, float y)
            {
                return polar(polar(hand, angle, x), angle + Pi / 2, y);
            };
            float width = 3 + evolution * 2.f;
            float reach = 47 + evolution * 5.f;
            r.Quad(blade(4, -width),
                   blade(reach - 12, -width * .65f),
                   blade(reach, 0),
                   blade(4, width),
                   shell);
            r.Triangle(blade(4, width), blade(reach, 0), blade(reach - 12, 2), ivory);
            r.Line(blade(6, 0), blade(reach - 5, 0), 2 * z, core);
            for (int side : {-1, 1})
            {
                r.Triangle(blade(-3, side * 3.f),
                           blade(12, side * (7 + evolution * 3.f)),
                           blade(3, side * 7.f),
                           ivory);
                if (evolution >= 2)
                {
                    r.Triangle(blade(20, side * width),
                               blade(30, side * (width + 5)),
                               blade(36, side * width),
                               shell);
                }
            }
            r.Ellipse(blade(2, 0), 4 * z, 4 * z, core);
            if (evolution == 3)
            {
                r.Quad(blade(12, -3), blade(20, 0), blade(12, 3), blade(8, 0), core);
            }
        }
    }
    else if (owner->weapon && owner->yokai == YokaiForm::Claw)
    {
        for (int hand = -1; hand <= 1; hand += 2)
        {
            float extension = actor.swingSide == hand ? attack * 23 : attack * 6;
            float angle =
                actor.swingTime > 0 ? heading + hand * (phase * 2.8f - 1.4f) : -Pi / 2 + hand * .5f;
            Point shoulder{p.x + hand * 7 * z, p.y - 29 * z};
            Point wrist = actor.swingTime > 0 ? polar(shoulder, angle, 13 + extension)
                                              : Point{p.x + hand * 16 * z, p.y - 26 * z};
            r.Line(shoulder, wrist, 6 * z, Color(71, 83, 99));
            r.Line(polar(wrist, angle, -5), polar(wrist, angle, 6), 12 * z, Color(39, 31, 53));
            r.Line(polar(wrist, angle + Pi / 2, -6),
                   polar(wrist, angle + Pi / 2, 6),
                   3 * z,
                   Color(158, 105, 189));
            if (evolution > 0)
            {
                r.Ellipse(wrist, (8 + evolution) * z, (7 + evolution) * z, shell);
                r.Ellipse(wrist, 3 * z, 3 * z, core);
                for (int plate = 0; plate < evolution; ++plate)
                {
                    Point back = polar(wrist, angle, -plate * 5.f);
                    r.Triangle(polar(back, angle + Pi / 2, hand * 6.f),
                               polar(back, angle + Pi / 2, hand * (12.f + plate * 2)),
                               polar(back, angle, -7),
                               ivory);
                }
            }
            for (int blade = 0; blade < 3; ++blade)
            {
                Point root = polar(wrist, angle + Pi / 2, (blade - 1) * 5.f);
                Point knuckle = polar(root, angle, 6);
                Point bend =
                    polar(knuckle, angle - hand * (.12f + evolution * .08f), 15 + evolution * 3.f);
                Point tip = polar(bend,
                                  angle + hand * (.32f + evolution * .16f),
                                  (blade == 1 ? 14.f : 11.f) + evolution * 2.f);
                r.Quad(polar(knuckle, angle + Pi / 2, -2),
                       polar(knuckle, angle + Pi / 2, 2),
                       polar(bend, angle + Pi / 2, 1.5f),
                       tip,
                       Color(183, 166, 203));
                r.Line(knuckle, bend, 1.3f * z, Emissive(Color(209, 140, 255), 1.5f));
                r.Line(bend, tip, 1 * z, Emissive(Color(232, 198, 255), 1.5f));
                if (actor.swingTime > 0 && actor.swingSide == hand)
                {
                    for (int i = 1; i <= 8; ++i)
                    {
                        float a0 = angle - hand * i * .09f;
                        r.Line(polar(shoulder, a0, 40.f + blade * 6.f),
                               polar(shoulder, a0 + hand * .09f, 40.f + blade * 6.f),
                               2 * z,
                               Emissive(Color(220, 167, 255, attack * (1 - i / 10.f)), 2));
                    }
                }
            }
        }
    }
    else if (owner->weapon)
    {
        Point hand{p.x + 9 * z, p.y - 29 * z};
        float dx = (actor.aim.x - actor.aim.y) * .85f;
        float dy = (actor.aim.x + actor.aim.y) * .425f;
        float length = std::max(.01f, Distance(dx, dy));
        dx /= length;
        dy /= length;
        float kick = std::min(1.f, owner->firingHold / .16f);
        hand.x -= dx * kick * 6 * z;
        hand.y -= (dy * kick * 6 + kick * 2) * z;
        auto gun = [&](float x, float y)
        {
            return Point{hand.x + (dx * x - dy * y) * z, hand.y + (dy * x + dx * y) * z};
        };
        Point tip{hand.x + dx * 43 * z, hand.y + dy * 43 * z};
        // Distinct stock, receiver, pistol grip, magazine and exposed barrel.
        r.Quad(gun(-24, -5), gun(-8, -3), gun(-8, 4), gun(-24, 9), shell);
        r.Line(gun(-24, -5), gun(-24, 9), 3 * z, Color(23, 26, 31));
        r.Quad(gun(-6, 3), gun(1, 3), gun(-2, 16), gun(-9, 15), Color(31, 33, 40));
        r.Quad(gun(7, 4), gun(16, 4), gun(13, 19), gun(6, 17), shell);
        r.Line(gun(12, 0), tip, 5 * z, Color(109, 118, 126));
        r.Quad(gun(-10, -7), gun(23, -7), gun(23, 5), gun(-10, 5), shell);
        r.Line(gun(-8, -6), gun(25, -6), 2 * z, ivory);
        r.Line(gun(4, -9), gun(12, -9), 3 * z, Color(25, 29, 35));
        r.Line(gun(37, -6), gun(37, 0), 2 * z, ivory);
        r.Line(gun(41, -4), gun(41, 4), 4 * z, Color(28, 31, 38));
        r.Ellipse(gun(1, -1), 3 * z, 3 * z, core);
        r.Line({p.x - 5 * z, p.y - 30 * z}, gun(17, 6), 5 * z, Color(72, 86, 100));
        r.Ellipse(gun(17, 6), 3 * z, 3 * z, Color(190, 160, 141));
        r.Ellipse(gun(-5, 8), 3 * z, 3 * z, Color(190, 160, 141));
        if (evolution > 0)
        {
            // The companion's segmented shell becomes the cannon housing.
            for (int plate = 0; plate <= evolution; ++plate)
            {
                float x = plate * 7.f;
                float width = 6 + evolution - plate * .5f;
                r.Quad(gun(x - 8, -width),
                       gun(x + 5, -width + 3),
                       gun(x + 5, width - 3),
                       gun(x - 8, width),
                       shell);
                r.Line(gun(x - 6, -width), gun(x + 4, -width + 3), 2 * z, ivory);
            }
            r.Ellipse(gun(0, 0), (5 + evolution) * z, (5 + evolution) * z, core);
            for (int side : {-1, 1})
            {
                r.Line(gun(15, side * 6.f), gun(43, side * (6.f + evolution)), 4 * z, ivory);
                if (evolution >= 2)
                {
                    r.Triangle(gun(-7, side * 8.f),
                               gun(-20, side * (12.f + evolution * 2)),
                               gun(4, side * 11.f),
                               shell);
                }
            }
            if (evolution == 3)
            {
                r.Line(gun(8, 0), gun(43, 0), 3 * z, core);
                r.Ellipse(tip, 7 * z, 7 * z, shell);
                r.Ellipse(tip, 3 * z, 3 * z, core);
            }
        }
        if (actor.muzzleFlash > 0)
        {
            r.Glow(tip, 38 * z, Emissive(Color(124, 255, 232), 5));
            r.Ellipse(tip, 8 * z, 8 * z, Emissive(Color(199, 255, 237), 6));
        }
    }
    else
    {
        Point ghost{p.x + 43 * z, p.y - 44 * z + std::sin(time_ * 3) * 6 * z};
        auto body = [&](float x, float y)
        {
            return Point{ghost.x + x * z, ghost.y + y * z};
        };
        r.Ellipse({ghost.x, p.y + 5 * z}, 12 * z, 4 * z, Color(0, 0, 0, .3f));
        r.Glow(ghost, 27 * z, evolutionInk);
        if (owner->yokai == YokaiForm::Sword)
        {
            // A horned mask grows into an armored blade spirit, not a winged orb.
            float width = 10 + evolution * 3.f;
            r.Quad(body(0, -18),
                   body(width, -1),
                   body(0, 24 + evolution * 3.f),
                   body(-width, -1),
                   shell);
            r.Triangle(body(-width, -1), body(0, 24 + evolution * 3.f), body(-3, 0), ivory);
            for (int side : {-1, 1})
            {
                r.Triangle(body(side * 6.f, -10),
                           body(side * (12.f + evolution * 3), -24 - evolution * 3.f),
                           body(side * 11.f, -2),
                           ivory);
                if (evolution >= 1)
                {
                    r.Quad(body(side * 10.f, 0),
                           body(side * (18.f + evolution * 2), -7),
                           body(side * 20.f, 8),
                           body(side * 8.f, 12),
                           shell);
                }
                if (evolution >= 2)
                {
                    r.Triangle(
                        body(side * 13.f, 7), body(side * 22.f, 21), body(side * 7.f, 17), ivory);
                }
                r.Line(body(side * 3.f, -7), body(side * 8.f, -9), 2 * z, core);
            }
            r.Line(body(0, 0), body(0, 21 + evolution * 3.f), 2 * z, core);
            if (evolution == 3)
            {
                r.Quad(body(0, -29), body(5, -19), body(0, -13), body(-5, -19), core);
            }
        }
        else if (owner->yokai == YokaiForm::Ranged)
        {
            // An armored lantern beast: shell ribs become barrel plates; eye becomes chamber.
            float width = 14 + evolution * 3.f;
            r.Ellipse(ghost, width * z, (14 + evolution * 2.f) * z, shell);
            for (int plate = 0; plate <= evolution; ++plate)
            {
                float y = -12 + plate * 7.f;
                r.Quad(body(-width, y), body(0, y - 5), body(width, y), body(0, y + 4), shell);
                r.Line(body(-width, y), body(0, y - 5), 2 * z, ivory);
                r.Line(body(0, y - 5), body(width, y), 1 * z, ivory);
            }
            for (int side : {-1, 1})
            {
                r.Triangle(body(side * 8.f, 10),
                           body(side * (13.f + evolution * 3), 24),
                           body(side * 3.f, 15),
                           ivory);
                if (evolution >= 2)
                {
                    r.Triangle(body(side * width, -2),
                               body(side * (width + 9), -20),
                               body(side * 8.f, -12),
                               shell);
                }
            }
            r.Ellipse(body(0, 5), (7 + evolution) * z, (7 + evolution) * z, Color(25, 24, 32));
            r.Ellipse(body(0, 5), (4 + evolution) * z, (4 + evolution) * z, core);
            if (evolution == 3)
            {
                r.Line(body(-12, 5), body(-12, 17), 3 * z, core);
                r.Line(body(12, 5), body(12, 17), 3 * z, core);
            }
        }
        else
        {
            // A crouching predator develops armored forepaws and hooked talons.
            r.Ellipse(body(0, 4), (12 + evolution * 2.f) * z, 13 * z, shell);
            r.Line(body(8, 10), body(21 + evolution * 3.f, 20), 5 * z, shell);
            r.Line(body(21 + evolution * 3.f, 20), body(28 + evolution * 3.f, 7), 3 * z, ivory);
            r.Quad(body(-12, -13), body(12, -13), body(9, 1), body(0, 7), shell);
            for (int side : {-1, 1})
            {
                r.Triangle(
                    body(side * 4.f, -12), body(side * 14.f, -25), body(side * 12.f, -5), ivory);
                float paw = 12 + evolution * 4.f;
                r.Line(body(side * 7.f, 4), body(side * paw, 14), (7 + evolution * 2.f) * z, shell);
                for (int claw = 0; claw < 3; ++claw)
                {
                    float x = side * (paw - 5 + claw * 4);
                    r.Line(body(x, 14), body(x + side * 3, 23 + evolution * 2.f), 2 * z, ivory);
                }
                for (int plate = 0; plate < evolution; ++plate)
                {
                    r.Triangle(body(side * paw, 8 - plate * 4.f),
                               body(side * (paw + 7), 2 - plate * 5.f),
                               body(side * (paw - 4), 4 - plate * 4.f),
                               ivory);
                }
                r.Line(body(side * 3.f, -6), body(side * 8.f, -8), 2 * z, core);
                if (evolution == 3)
                {
                    r.Ellipse(body(side * paw, 13), 3 * z, 3 * z, core);
                }
            }
        }
        r.Text(ghost.x - 28 * z, ghost.y + 38 * z, owner->YokaiName(), evolutionInk, .55f * z);
    }
    if (owner->transformPhase > 0)
    {
        float radius = (1 - owner->transformPhase) * 85 * z;
        for (int i = 0; i < 32; ++i)
        {
            float a = i * Pi / 16;
            r.Line(
                {p.x + std::cos(a) * radius, p.y - 24 * z + std::sin(a) * radius * .5f},
                {p.x + std::cos(a + .1f) * radius, p.y - 24 * z + std::sin(a + .1f) * radius * .5f},
                2 * z,
                Color(145, 255, 222, owner->transformPhase));
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
    if (level_.society.enabled)
    {
        LivingHud(r);
        return;
    }
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
           "체력 " + std::to_string(int(level_.Player().health)) + " / "
               + std::to_string(int(level_.MaximumHealth())),
           white,
           .7f);
    r.Rect(500, 36, 240, 9, Color(52, 40, 54));
    r.Rect(500, 36, 240 * level_.Player().health / level_.MaximumHealth(), 9, red);
    std::string xp = level_.Player().level >= 10
                         ? "최대 레벨"
                         : "경험치 " + std::to_string(level_.Player().experience) + " / "
                               + std::to_string(level_.ExperienceRequired());
    r.Text(500, 51, "LV." + std::to_string(level_.Player().level) + "   " + xp, cyan, .68f);
    r.Rect(500, 76, 240, 4, Color(34, 55, 62));
    r.Rect(500,
           76,
           240
               * (level_.Player().level >= 10
                      ? 1.f
                      : float(level_.Player().experience) / level_.ExperienceRequired()),
           4,
           cyan);
    std::ostringstream seed;
    seed << "SEED " << seed_;
    r.Text(w - 302, 16, seed.str(), muted, .68f);
    r.Text(w - 302,
           42,
           "영혼포 +" + std::to_string(level_.Player().weaponRank) + " / 처치 "
               + std::to_string(level_.kills),
           gold,
           .83f);
    r.Rect(26, 111, 354, 137, Color(8, 17, 24, .94f));
    r.Rect(26, 111, 3, 137, cyan);
    r.Text(42, 120, "목표 / 파밍 후 수문장에게 도전", cyan, .78f);
    r.Text(42, 149, level_.Objective(), white, .66f);
    r.Text(42,
           174,
           "처치 " + std::to_string(level_.kills) + "/12   레벨 "
               + std::to_string(level_.Player().level) + "/3   강화 "
               + std::to_string(level_.Player().weaponRank) + "/1",
           gold,
           .67f);
    std::ostringstream stats;
    stats << std::fixed << std::setprecision(2) << "공격 " << int(level_.Damage()) << "   간격 "
          << level_.Cooldown() << "초   사거리 " << int(level_.Range());
    r.Text(42, 199, stats.str(), muted, .66f);
    r.Text(42,
           223,
           level_.Player().magnetTime > 0
               ? "자석 " + std::to_string(int(level_.Player().magnetTime) + 1)
                     + "초 / 획득 범위 440"
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
        float x = std::max(-65.f, std::min(65.f, e->Position().x * .12f));
        float y = std::max(-54.f, std::min(54.f, e->Position().y * .12f));
        r.Ellipse({mx + x, my + y}, e->boss ? 5.f : 3.f, e->boss ? 5.f : 3.f, red);
    }
    for (const auto& item : level_.Loot())
    {
        if (Distance(item->Position().x, item->Position().y) < 500)
        {
            r.Rect(mx + item->Position().x * .12f, my + item->Position().y * .12f, 2, 2, gold);
        }
    }
    if (level_.state == RunState::BossReady)
    {
        float d = Distance(level_.Portal().Position().x, level_.Portal().Position().y);
        float factor = .12f * std::min(1.f, 480.f / std::max(1.f, d));
        r.Ellipse({mx + level_.Portal().Position().x * factor,
                   my + level_.Portal().Position().y * factor},
                  5,
                  5,
                  red);
        r.Text(w - 177, 260, "균열까지 " + std::to_string(int(d)), red, .65f);
    }
    r.Ellipse({mx, my}, 4, 4, cyan);
    for (const auto& enemy : level_.Enemies())
    {
        if (enemy->boss)
        {
            float bx = w * .5f - 200;
            r.Rect(bx - 12, 104, 424, 57, Color(15, 10, 21, .95f));
            r.Text(bx, 111, "보스 / 균열의 수문장", red, .85f);
            r.Rect(bx, 140, 400, 8, Color(52, 26, 40));
            r.Rect(bx, 140, 400 * enemy->health / enemy->maximumHealth, 8, red);
        }
    }
    float foot = h - 146;
    r.Rect(26, foot, 300, 77, Color(8, 17, 24, .96f));
    r.Text(43, foot + 9, "요괴 / 원거리 자동 조준", muted, .7f);
    r.Text(43,
           foot + 34,
           level_.Player().weapon ? "영혼포 / 자동 사격 중" : "동행 중 / Q로 무기 변신",
           cyan,
           .84f);
    r.Rect(345, foot, w - 371, 77, Color(8, 17, 24, .96f));
    r.Text(363, foot + 9, "동료의 조언", muted, .67f);
    r.Text(363, foot + 32, level_.Dialogue(level_.Player().weapon), white, .78f);
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
    activeRenderer_ = &r;
    level_.Graph().Render(*this, RenderLayer::Ground, RenderLayer::Weather);
    activeRenderer_ = nullptr;
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

void PrototypeWorld::Enemy(PrototypeRenderer& r, const EnemyActor& enemy)
{
    Point p = Project(enemy.Position().x, enemy.Position().y);
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
    for (int side = -1; side <= 1; side += 2)
    {
        for (int digit = 0; digit < 3; ++digit)
        {
            r.Line({p.x + side * (20.f + digit * 2) * z, p.y - 17 * z},
                   {p.x + side * (23.f + digit * 3) * z, p.y - (8 - digit * 2) * z},
                   1.5f * z,
                   Color(222, 180, 154));
        }
        for (int rib = 0; rib < 3; ++rib)
        {
            r.Line({p.x + side * 2 * z, p.y - (36 - rib * 5) * z},
                   {p.x + side * 8 * z, p.y - (33 - rib * 5) * z},
                   1.5f * z,
                   Color(175, 112, 119));
        }
    }
    r.Rect(p.x - 5 * z, p.y - 44 * z, 10 * z, 4 * z, Color(34, 17, 28));
    for (int tooth = 0; tooth < 3; ++tooth)
    {
        r.Triangle({p.x + (-4 + tooth * 3) * z, p.y - 44 * z},
                   {p.x + (-2 + tooth * 3) * z, p.y - 44 * z},
                   {p.x + (-3 + tooth * 3) * z, p.y - 41 * z},
                   Color(222, 180, 154));
    }
    r.Rect(p.x - 17 * z, p.y - 80 * z, 34 * z, 3 * z, Color(40, 19, 32));
    r.Rect(p.x - 17 * z,
           p.y - 80 * z,
           34 * z * enemy.health / enemy.maximumHealth,
           3 * z,
           Color(247, 94, 117));
}

void PrototypeWorld::Loot(PrototypeRenderer& r, const LootActor& item)
{
    Point p = Project(item.Position().x, item.Position().y, 10 + std::sin(item.age * 3) * 3);
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
    activeRenderer_ = &r;
    level_.Graph().Render(*this, RenderLayer::UI, RenderLayer::UI);
    activeRenderer_ = nullptr;
}

bool PrototypeWorld::SelfTest()
{
    auto require = [](bool pass, const char* name)
    {
        std::cout << (pass ? "PASS " : "FAIL ") << name << '\n';
        return pass;
    };
    bool ok = LevelOne::SelfTest();
    ok = LivingWorld::SelfTest() && ok;
    {
        PrototypeWorld world(2026), same(2026);
        bool safe = true, deterministic = true;
        int wilderness = 0;
        for (auto* npc : world.level_.Graph().Query<NpcActor>())
        {
            if (!npc->wilderness)
            {
                continue;
            }
            ++wilderness;
            auto* chunk = world.level_.Graph().Get<ChunkActor>(npc->Parent());
            auto point = npc->Position();
            safe &= chunk && world.Walkable(point.x, point.y, 16);
            for (const auto& entry : world.chunkActors_)
            {
                if (entry.second == npc->Parent())
                {
                    safe &= !IsVillageChunk(entry.first.first, entry.first.second);
                }
            }
            auto* other = same.level_.Graph().Get<NpcActor>(npc->Id());
            deterministic &=
                other && other->local.x == npc->local.x && other->local.y == npc->local.y;
        }
        ok &= require(world.level_.Graph().Query<TownActor>().size() == 7,
                      "world retains exactly seven persistent villages");
        ok &= require(wilderness >= 48 && safe,
                      "random wilderness monsters spawn outside village chunks and buildings");
        ok &= require(deterministic, "same seed reproduces wilderness spawn layout");
        auto actorCount = world.level_.Graph().Size();
        world.Stream();
        ok &= require(world.level_.Graph().Size() == actorCount,
                      "repeated streaming does not duplicate towns or monsters");
        bool districtStyles = true;
        for (const auto& entry : world.chunks_)
        {
            const auto& chunk = entry.second;
            int walls = int(std::count(chunk.styles.begin(), chunk.styles.end(), 2));
            districtStyles &=
                chunk.village
                    ? walls <= 4 && chunk.plots.size() >= 4
                    : walls == 0 && std::count(chunk.styles.begin(), chunk.styles.end(), 0) == 0;
        }
        ok &= require(districtStyles,
                      "villages have defensive walls and wilderness has only ruins or plains");
        std::map<ActorId, FarmPoint> starts;
        for (auto* npc : world.level_.Graph().Query<NpcActor>())
        {
            if (npc->wilderness)
            {
                starts[npc->Id()] = npc->local;
            }
        }
        world.level_.state = RunState::Lost;
        for (int frame = 0; frame < 120; ++frame)
        {
            world.Update(.05f);
        }
        int moved = 0;
        bool bounded = true;
        for (auto* npc : world.level_.Graph().Query<NpcActor>())
        {
            if (!npc->wilderness)
            {
                continue;
            }
            moved += Length(Minus(npc->local, starts[npc->Id()])) > 5 ? 1 : 0;
            bounded &= npc->local.x >= 18 && npc->local.y >= 18 && npc->local.x <= 622
                       && npc->local.y <= 622;
        }
        ok &= require(moved > 10 && bounded,
                      "wild monsters wander while remaining inside their wilderness chunk");
        auto services = world.level_.Context(
            [&](float x, float y, float radius)
            {
                return world.Walkable(x, y, radius);
            });
        auto* source = world.level_.society.Village(world.level_.Graph(), 0);
        auto* target = world.level_.society.Village(world.level_.Graph(), 1);
        auto* courier = world.level_.Graph().Get<NpcActor>(source->homes[11]);
        courier->letter.known = true;
        courier->letter.source = 0;
        courier->letter.text = "실제 지형 경유 편지";
        world.level_.society.BeginTravel(*courier, *target, false, services);
        bool routeSafe = true;
        for (int frame = 0; frame < 2200 && world.level_.society.lettersDelivered == 0; ++frame)
        {
            world.level_.society.UpdateNpc(*courier, .1f, services);
            auto p = courier->Position();
            routeSafe &= world.Walkable(p.x, p.y, 11);
        }
        ok &= require(routeSafe && world.level_.society.lettersDelivered == 1,
                      "courier physically crosses gates and unloaded wilderness to deliver mail");
        bool spacing = true;
        for (int x = 1160; x <= 5050; x += 5)
        {
            spacing &= world.Walkable(float(x), 0, 12);
        }
        float gateSeconds = (5050.f - 1160) / world.level_.Player().MoveSpeed();
        ok &=
            require(spacing && gateSeconds >= 19 && gateSeconds <= 22,
                    "neighboring town gates have a clear approximately twenty-second player route");
    }
    Location negative;
    negative.x = -1;
    negative.y = -641;
    negative.Normalize();
    ok &= require(negative.cx == -1 && negative.cy == -2 && negative.x == 639 && negative.y == 639,
                  "negative chunk normalization");
    auto a = Generate({-41, 9000000000000LL}), b = Generate({-41, 9000000000000LL});
    ok &= require(a.seed == b.seed && a.heights == b.heights, "deterministic distant generation");
    auto different = Generate({-41, 9000000000000LL}, 2027);
    ok &= require(a.seed != different.seed && a.heights != different.heights,
                  "different seeds change building footprints");
    bool seams = true, roadClear = true, differentCounts = false;
    size_t firstCount = Generate({2, 3}, 1).plots.size();
    for (uint32_t sampleSeed = 1; sampleSeed <= 128; ++sampleSeed)
    {
        auto sample = Generate({2, 3}, sampleSeed);
        auto east = Generate({3, 3}, sampleSeed);
        auto south = Generate({2, 4}, sampleSeed);
        seams &= sample.roads[1].y == east.roads[1].y
                 && sample.roads[1].depth == east.roads[1].depth
                 && sample.roads[0].x == south.roads[0].x
                 && sample.roads[0].width == south.roads[0].width;
        differentCounts |= sample.plots.size() != firstCount;
        for (const auto& plot : sample.plots)
        {
            for (const auto& road : sample.roads)
            {
                roadClear &= !(plot.x < road.x + road.width && plot.x + plot.width > road.x
                               && plot.y < road.y + road.depth && plot.y + plot.depth > road.y);
            }
        }
    }
    ok &= require(seams, "neighboring chunks share arterial positions and widths");
    ok &= require(roadClear && differentCounts, "variable parcel counts preserve road clearance");
    PrototypeWorld world(2026, false);
    auto expectedBuildings = [&]()
    {
        size_t count = 0;
        for (const auto& entry : world.chunks_)
        {
            count += entry.second.plots.size();
        }
        return count;
    };
    uint32_t first = world.chunks_.at({0, 0}).seed;
    ActorId oldChunk = world.chunkActors_.at({0, 0});
    ActorId oldBuilding = world.level_.Graph().Get<ChunkActor>(oldChunk)->buildings[0];
    world.player_.cx = 8000000000000LL;
    world.Stream();
    ok &= require(world.chunks_.size() == 25 && world.chunks_.count({0, 0}) == 0,
                  "bounded resident chunks after travel");
    ok &= require(!world.level_.Graph().Get(oldChunk) && !world.level_.Graph().Get(oldBuilding)
                      && world.level_.Graph().Query<ChunkActor>().size() == 25
                      && world.level_.Graph().Query<BuildingActor>().size() == expectedBuildings()
                      && world.level_.Graph().Query<LampActor>().size()
                             == size_t(std::count_if(world.chunks_.begin(),
                                                     world.chunks_.end(),
                                                     [](const auto& entry)
                                                     {
                                                         return entry.second.village;
                                                     })),
                  "streaming removes old actor subtrees and keeps a bounded scene");
    world.player_ = Location{};
    world.Stream();
    ok &= require(world.chunks_.at({0, 0}).seed == first, "return regenerates same chunk");
    auto plot = world.chunks_.at({0, 0}).plots[0];
    Location wall;
    wall.x = plot.x + 20;
    wall.y = plot.y + 20;
    ok &= require(world.Blocked(wall) && !world.Blocked(world.player_),
                  "building collision and clear spawn");
    auto* chunk = world.level_.Graph().Get<ChunkActor>(world.chunkActors_.at({0, 0}));
    auto* building = world.level_.Graph().Get<BuildingActor>(chunk->buildings[0]);
    building->enabled = false;
    ok &= require(!world.Blocked(wall), "building actor activation controls collision");
    building->enabled = true;
    double before = world.player_.x;
    world.Key('d', true);
    world.Update(.04f);
    world.Key('d', false);
    ok &= require(world.player_.x != before, "input movement");
    world.Key('q', true);
    world.Key('q', true);
    ok &= require(world.level_.Player().weapon, "transform is edge triggered");
    world.Key('q', false);
    world.level_.Player().level = 5;
    world.level_.Player().health = 12;
    world.Key('r', true);
    ok &= require(world.level_.Player().level == 1 && world.level_.Player().health == 100
                      && !world.level_.Player().weapon,
                  "restart resets progression and player");

    world.level_.Player().enabled = false;
    before = world.player_.x;
    world.Key('d', true);
    world.Update(.04f);
    world.Key('d', false);
    ok &= require(world.player_.x == before, "disabled player actor stops movement");
    world.level_.Player().enabled = true;
    // Cross a streamed boundary while PlayerActor::Update is traversing the graph.
    world.player_.x = 639;
    world.player_.y = 90;
    world.Stream();
    world.Key('d', true);
    world.Update(.04f);
    world.Key('d', false);
    ok &= require(world.player_.cx == 1 && world.level_.Graph().Query<ChunkActor>().size() == 25
                      && world.level_.Graph().Query<BuildingActor>().size() == expectedBuildings(),
                  "player movement streams actor subtrees safely during scene traversal");
    ok &= require(world.level_.Graph().Query<HudActor>().size() == 1
                      && world.level_.Graph().Query<RainActor>().size() == 1
                      && world.level_.Graph().Query<WeaponActor>().size() == 1,
                  "HUD weather and attached weapon are scene actors");

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
                for (int first = 0; first < 1024; ++first)
                {
                    if (free[first])
                    {
                        queue.push(first);
                        seen[first] = true;
                        break;
                    }
                }
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
                auto layout = Generate({cx, cy}, seed);
                int gateX = int(layout.roadX / 20), gateY = int(layout.roadY / 20);
                connected = connected && free[gateX] && free[31 * 32 + gateX] && free[gateY * 32]
                            && free[gateY * 32 + 31];
                if (!connected)
                {
                    std::cout << "Navigation sample seed=" << seed << " chunk=" << cx << ',' << cy
                              << " reachable=" << count << " free=" << total
                              << " gates=" << free[gateX] << free[31 * 32 + gateX]
                              << free[gateY * 32] << free[gateY * 32 + 31] << '\n';
                }
            }
        }
    }
    ok &= require(connected, "128 seeds / 1152 chunks: all walkable cells and exits connected");
    PrototypeWorld settlement(2026);
    settlement.level_.state = RunState::Lost;
    bool pathsClear = true;
    for (int frame = 0; frame < 3600; ++frame)
    {
        settlement.Update(.05f);
        if (frame % 20 == 0)
        {
            for (auto* npc : settlement.level_.Graph().Query<NpcActor>())
            {
                auto p = npc->Position();
                pathsClear &= settlement.Walkable(p.x, p.y, 10);
            }
        }
    }
    const auto& life = settlement.level_.society;
    std::cout << "Living world 180s: hunts=" << life.hunts << " trades=" << life.trades
              << " raids=" << life.raids << '\n';
    ok &= require(pathsClear, "NPC paths never enter generated building collision");
    ok &= require(life.hunts > 0 && life.trades > 0 && life.raids > 0,
                  "real procedural town supports autonomous hunting trade and raids");
    int insightBefore = 0;
    for (auto* npc : settlement.level_.Graph().Query<NpcActor>())
    {
        if (npc->Parent() == settlement.level_.society.town)
        {
            insightBefore += npc->insight;
        }
    }
    for (int step = 0; step < 100; ++step)
    {
        settlement.Move(24, 0);
    }
    for (int frame = 0; frame < 320; ++frame)
    {
        settlement.Update(.05f);
    }
    int insightAfter = 0;
    for (auto* npc : settlement.level_.Graph().Query<NpcActor>())
    {
        if (npc->Parent() == settlement.level_.society.town)
        {
            insightAfter += npc->insight;
        }
    }
    ok &= require(insightAfter > insightBefore
                      && Length(settlement.level_.society.Center(settlement.level_.Graph())) > 2300
                      && settlement.chunks_.count({0, 0}) == 0,
                  "offscreen town continues simulation after streaming out of view");
    PrototypeWorld journey(2026);
    auto travel = [&](FarmPoint destination)
    {
        for (int step = 0; step < 1400; ++step)
        {
            auto center = journey.level_.society.Center(journey.level_.Graph());
            FarmPoint delta{center.x + destination.x, center.y + destination.y};
            if (Length(delta) < 1)
            {
                break;
            }
            auto direction = Unit(delta);
            float movement = std::min(5.f, Length(delta));
            journey.Move(direction.x * movement, direction.y * movement);
        }
    };
    auto interact = [&]()
    {
        journey.Key('e', true);
        journey.Key('e', false);
    };
    travel({150, 0});
    interact();
    travel({0, 0});
    travel({0, 250});
    interact();
    travel({0, 70});
    interact();
    travel({0, 450});
    interact();
    ok &= require(journey.level_.society.story.mukContracted,
                  "walkable world route and E key complete the MAP01 evidence contract sequence");
    auto& expedition = journey.level_.society;
    expedition.story.hunted = 3;
    expedition.goods = 3;
    travel({0, 70});
    interact();
    travel({0, 0});
    journey.Key('t', true);
    journey.Key('t', false);
    journey.Key('t', true);
    journey.Key('t', false);
    for (float x : {1190.f, 1830.f, 2470.f})
    {
        travel({x, 0});
        interact();
    }
    ok &= require(expedition.QuestStep() == 11 && journey.chunks_.count({0, 0}) == 0,
                  "expedition road route and interactions work beyond the unloaded starting town");
    auto services = journey.level_.Context(
        [&](float x, float y, float radius)
        {
            return journey.Walkable(x, y, radius);
        });
    if (auto* guard = journey.level_.Graph().Get<NpcActor>(expedition.guardian))
    {
        journey.level_.Player().weapon = true;
        journey.level_.Player().SelectYokai(YokaiForm::Sword);
        journey.level_.Player().weapon = true;
        for (int strike = 0; strike < 20 && guard->health > 0; ++strike)
        {
            journey.level_.Weapon().shotTimer_ = 0;
            expedition.Tick(1, services);
            expedition.PlayerStrike(false, services, guard->Id(), true);
        }
    }
    interact();
    travel({0, 0});
    travel({0, 450});
    interact();
    travel({0, 70});
    interact();
    ok &= require(expedition.story.caseClosed && journey.level_.Player().weaponRank == 1,
                  "real world combat return route and E interactions finish the extended quest");
    PrototypeWorld skillInput(2026);
    skillInput.level_.Player().weapon = true;
    skillInput.level_.Player().yokaiGrowth[0].level = 3;
    skillInput.Key('k', true);
    skillInput.Key('k', false);
    ok &= require(skillInput.level_.Weapon().skillTime < 0, "K no longer casts a skill");
    skillInput.MouseSkill();
    ok &= require(skillInput.level_.Weapon().skillTime == 0
                      && skillInput.level_.Player().Companion().skillCooldown > 0,
                  "right mouse skill entry starts casting and cooldown");

    PrototypeWorld mouse(2026);
    mouse.Key('m', true);
    mouse.Key('m', true);
    ok &= require(mouse.mapExpanded_, "M expands map once while held");
    mouse.Key('m', false);
    mouse.Key('m', true);
    mouse.Key('m', false);
    ok &= require(!mouse.mapExpanded_, "second M press restores minimap");
    mouse.Key('3', true);
    mouse.Key('3', false);
    mouse.Key('q', true);
    mouse.Key('q', false);
    ok &= require(mouse.level_.Player().weapon && mouse.level_.Player().yokai == YokaiForm::Claw,
                  "number three then Q equips claw");
    mouse.Key('2', true);
    mouse.Key('2', false);
    ok &= require(!mouse.level_.Player().weapon && mouse.level_.Player().yokai == YokaiForm::Ranged,
                  "number two unequips claw and selects ranged companion");
    mouse.Key('1', true);
    mouse.Key('1', false);
    ok &= require(!mouse.level_.Player().weapon && mouse.level_.Player().yokai == YokaiForm::Sword,
                  "number one selects sword companion");
    auto residents = mouse.level_.Graph().Query<NpcActor>();
    for (auto* npc : residents)
    {
        npc->local = {1000, 1000};
    }
    auto* human = residents.front();
    auto* monster = residents[11];
    human->local = {55, 0};
    monster->local = {0, 55};
    auto click = [&](NpcActor& target, int width, int height)
    {
        mouse.MouseAttack(0, 0, width, height);
        auto point = mouse.Project(target.Position().x, target.Position().y);
        mouse.MouseAttack(int(point.x), int(point.y - 27 * mouse.zoom_), width, height);
    };
    float humanHealth = human->health, monsterHealth = monster->health;
    click(*human, 1280, 800);
    ok &= require(human->health == humanHealth, "mouse human attack respects PK opt-in");
    click(*monster, 1280, 800);
    ok &= require(monster->health == monsterHealth - 14 && human->health == humanHealth,
                  "mouse attacks selected monster before contract without hitting closer humans");
    click(*monster, 1280, 800);
    ok &= require(monster->health == monsterHealth - 14,
                  "rapid clicks cannot bypass attack cooldown");
    auto mouseContext = mouse.level_.Context(
        [](float, float, float)
        {
            return true;
        });
    mouse.level_.society.Tick(.6f, mouseContext);
    mouse.level_.Weapon().Update(.6f, mouseContext);
    mouse.level_.society.TogglePK();
    click(*human, 961, 601);
    ok &= require(human->health == humanHealth - 24 && mouse.level_.society.notoriety > 0,
                  "mouse picking scales with odd window sizes and preserves PK consequences");
    mouse.level_.society.Tick(.6f, mouseContext);
    mouse.level_.Weapon().Update(.6f, mouseContext);
    mouse.MouseAttack(500, 150, 961, 601);
    ok &= require(human->health == humanHealth - 24 && monster->health == monsterHealth - 14,
                  "empty-space click never falls back to nearest target");
    auto blockedContext = mouse.level_.Context(
        [](float, float, float)
        {
            return false;
        });
    mouse.level_.society.PlayerStrike(false, blockedContext, monster->Id(), true);
    ok &= require(monster->health == monsterHealth - 14,
                  "selected mouse target cannot be hit through walls");
    mouse.cameraLag_ = {-300, -170};
    click(*monster, 1280, 800);
    ok &=
        require(monster->health == monsterHealth - 14, "HUD consumes clicks over a nearby target");
    mouse.cameraLag_ = {};
    monster->local = {0, 110};
    click(*monster, 1280, 800);
    ok &= require(monster->health == monsterHealth - 14, "mouse attack respects melee range");
    mouse.level_.state = RunState::Lost;
    click(*human, 1280, 800);
    ok &= require(human->health == humanHealth - 24, "dead player cannot mouse attack");
    return ok;
}

void PrototypeWorld::PreviewRoadJunction()
{
    for (int step = 0; step < 128; ++step)
    {
        Move(10, 0);
    }
    cameraLag_ = {0, 0};
}

void PrototypeWorld::PreviewExpedition()
{
    auto& story = level_.society.story;
    story.homeInvestigated = story.hasHarinStudentCard = story.metRecorder = story.mukContracted =
        true;
    story.hunted = 3;
    story.reported = true;
    story.suppliesDelivered = 2;
    for (int i = 0; i < 110; ++i)
    {
        Move(10, 0);
    }
    cameraLag_ = {0, 0};
}

void PrototypeWorld::PreviewAttack(YokaiForm form, float phase)
{
    level_.Player().SelectYokai(form);
    level_.Player().weapon = true;
    level_.Player().transformPhase = 0;
    auto& weapon = level_.Weapon();
    weapon.StartSwing({80, 30});
    weapon.swingSide = 1;
    weapon.swingTime = weapon.swingDuration * (1 - std::max(0.f, std::min(.99f, phase)));
}

void PrototypeWorld::PreviewYokai(YokaiForm form, int level, bool weaponForm, bool skill)
{
    auto& player = level_.Player();
    player.SelectYokai(form);
    player.yokaiGrowth[int(form)].level = level;
    player.transformPhase = 0;
    player.weapon = weaponForm;
    auto& weapon = level_.Weapon();
    weapon.skillForm = form;
    weapon.skillTime = weaponForm && skill ? .43f : -1.f;
    weapon.aim = {1, 0};
}

void PrototypeWorld::PreviewBoss()
{
    // Only invoked by the screenshot harness, never by a gameplay key.
    *this = PrototypeWorld(seed_, false);
    level_.Player().level = 4;
    level_.Player().weaponRank = 3;
    level_.kills = 12;
    level_.pickups = 24;
    level_.Player().health = level_.MaximumHealth();
    level_.state = RunState::BossReady;
    level_.Portal().SetPosition({80, 0});
    level_.Interact(
        [&](float x, float y, float radius)
        {
            return Walkable(x, y, radius);
        });
    level_.Player().weapon = true;
}

void PrototypeWorld::Draw(const PlayerActor& actor)
{
    Player(*activeRenderer_, actor);
}

void PrototypeWorld::Draw(const WeaponActor& actor)
{
    Weapon(*activeRenderer_, actor);
}

void PrototypeWorld::Draw(const RangeActor& actor)
{
    auto* player = level_.Graph().Get<PlayerActor>(actor.Parent());
    if (player && player->weapon && player->showRange)
    {
        auto p = actor.Position();
        Ring(*activeRenderer_, p.x, p.y, player->StrikeRange(), Color(90, 220, 199, .4f));
    }
}

void PrototypeWorld::Draw(const EnemyActor& actor)
{
    Enemy(*activeRenderer_, actor);
}

void PrototypeWorld::Draw(const LootActor& actor)
{
    Loot(*activeRenderer_, actor);
}

void PrototypeWorld::Draw(const PortalActor& actor)
{
    auto p = actor.Position();
    Rift(*activeRenderer_, p.x, p.y);
}

void PrototypeWorld::Draw(const TelegraphActor& actor)
{
    auto p = actor.Position();
    Ring(*activeRenderer_, p.x, p.y, 105, Emissive(Color(255, 60, 90), 2), 3);
    Ring(*activeRenderer_, p.x, p.y, 105 * (1 - actor.remaining / 1.1f), Color(255, 130, 120), 2);
}

void PrototypeWorld::Draw(const ProjectileActor& actor)
{
    auto& r = *activeRenderer_;
    auto position = actor.Position();
    Point p = Project(position.x, position.y, 29);
    Point tail =
        Project(position.x - actor.velocity.x * .025f, position.y - actor.velocity.y * .025f, 29);
    r.Line(tail, p, 4 * zoom_, Emissive(Color(136, 255, 225), 5));
    r.Glow(p, 15 * zoom_, Color(94, 244, 215));
}

void PrototypeWorld::Draw(const EffectActor& actor)
{
    auto position = actor.Position();
    Point p = Project(position.x, position.y, 65 + (1 - actor.life) * 25);
    activeRenderer_->Text(p.x, p.y, actor.text, Color(255, 225, 173, actor.life), .7f * zoom_);
}

void PrototypeWorld::Draw(const FloorActor& actor)
{
    auto p = actor.Position();
    Floor(*activeRenderer_, p.x, p.y, actor.data);
}

void PrototypeWorld::Draw(const BuildingActor& actor)
{
    auto p = actor.Position();
    Point screen = Project(p.x + actor.width * .5f, p.y + actor.depth * .5f);
    if (screen.x < -250 || screen.x > activeRenderer_->Width() + 250 || screen.y < 0
        || screen.y > activeRenderer_->Height() + 350)
    {
        return;
    }
    if (actor.style == 0)
    {
        Building(*activeRenderer_,
                 p.x,
                 p.y,
                 actor.width,
                 actor.depth,
                 actor.height,
                 actor.seed,
                 actor.homeSlot >= 0);
        if (actor.homeSlot >= 0)
        {
            auto* settlement = level_.society.Village(level_.Graph(), actor.villageIndex);
            auto* owner = settlement
                              ? level_.Graph().Get<NpcActor>(settlement->homes[actor.homeSlot])
                              : nullptr;
            auto label = Project(p.x + actor.width * .5f, p.y + actor.depth + 6, 8);
            activeRenderer_->Text(
                label.x - 25 * zoom_,
                label.y,
                owner ? owner->Name() + (owner->Parent() == settlement->Id() ? "의 집" : " 예약")
                      : "빈집 " + std::to_string(actor.homeSlot + 1),
                owner ? Color(195, 213, 185) : Color(133, 143, 137),
                .4f * zoom_);
        }
        return;
    }
    auto& r = *activeRenderer_;
    LocalMesh(r,
              "fort-ruin-v3:" + std::to_string(actor.style) + ":" + std::to_string(actor.seed) + ":"
                  + std::to_string(actor.width) + ":" + std::to_string(actor.depth),
              p.x,
              p.y,
              1,
              [&]()
              {
                  auto at = [&](float x, float y, float z = 0.f)
                  {
                      return Project(x, y, z);
                  };
                  auto box = [&](float x, float y, float w, float d, float h, float bottom = 0.f)
                  {
                      Ink front = actor.style == 2 ? Color(76, 82, 77) : Color(70, 62, 58);
                      Ink side = actor.style == 2 ? Color(49, 60, 57) : Color(45, 42, 40);
                      r.Quad(at(x, y + d, bottom),
                             at(x + w, y + d, bottom),
                             at(x + w, y + d, bottom + h),
                             at(x, y + d, bottom + h),
                             front);
                      r.Quad(at(x + w, y, bottom),
                             at(x + w, y + d, bottom),
                             at(x + w, y + d, bottom + h),
                             at(x + w, y, bottom + h),
                             side);
                      r.Quad(at(x, y, bottom + h),
                             at(x + w, y, bottom + h),
                             at(x + w, y + d, bottom + h),
                             at(x, y + d, bottom + h),
                             Color(95, 100, 87));
                  };
                  if (actor.style == 2)
                  {
                      box(0, 0, actor.width, actor.depth, 30);
                      bool horizontal = actor.width > actor.depth;
                      float length = horizontal ? actor.width : actor.depth;
                      // A continuous masonry face, shallow battlements and heavy terminal piers.
                      for (int row = 0; row < 3; ++row)
                      {
                          float z = 8.f + row * 8;
                          r.Line(at(0, actor.depth + .1f, z),
                                 at(actor.width, actor.depth + .1f, z),
                                 .7f,
                                 Color(45, 55, 51));
                          r.Line(at(actor.width + .1f, 0, z),
                                 at(actor.width + .1f, actor.depth, z),
                                 .7f,
                                 Color(37, 47, 44));
                          for (float t = row % 2 ? 12.f : 24.f; t < length; t += 24)
                          {
                              float x = horizontal ? t : actor.width + .1f;
                              float y = horizontal ? actor.depth + .1f : t;
                              r.Line(at(x, y, z), at(x, y, z + 8), .7f, Color(47, 55, 50));
                          }
                      }
                      for (float t = 0; t < length; t += 38)
                      {
                          box(horizontal ? t : 0,
                              horizontal ? 0 : t,
                              horizontal ? std::min(24.f, length - t) : actor.width,
                              horizontal ? actor.depth : std::min(24.f, length - t),
                              7,
                              30);
                      }
                      for (float t : {0.f, std::max(0.f, length - 17)})
                      {
                          box(horizontal ? t : 0,
                              horizontal ? 0 : t,
                              horizontal ? std::min(17.f, length) : actor.width,
                              horizontal ? actor.depth : std::min(17.f, length),
                              41);
                      }
                  }
                  else
                  {
                      box(0, 0, actor.width, actor.depth, 4);
                      for (int i = 0; i < 5; ++i)
                      {
                          float x = actor.width * i / 5.f;
                          float h = actor.height * (.4f + float(Mix(actor.seed + i) % 60) / 100.f);
                          box(x, 0, actor.width / 5.f - 2, 8, h);
                          box(x, actor.depth - 9, actor.width / 5.f - 3, 9, h * .55f);
                      }
                      box(actor.width - 8, 10, 8, actor.depth * .5f, 18);
                      for (int piece = 0; piece < 9; ++piece)
                      {
                          auto detail = Mix(actor.seed + piece * 891);
                          float x =
                              10 + float(detail % 1000) / 1000.f * std::max(1.f, actor.width - 28);
                          float y = 12
                                    + float((detail >> 16) % 1000) / 1000.f
                                          * std::max(1.f, actor.depth - 32);
                          box(x,
                              y,
                              7 + float(detail % 9),
                              5 + float((detail >> 8) % 8),
                              4 + float((detail >> 24) % 10));
                      }
                  }
              });
}

void PrototypeWorld::Draw(const LampActor& actor)
{
    auto& r = *activeRenderer_;
    auto p = actor.Position();
    Point a = Project(p.x, p.y), b = Project(p.x, p.y, 95), c{b.x + 24 * zoom_, b.y};
    r.Ellipse(a, 43 * zoom_, 15 * zoom_, Color(79, 199, 177, .09f));
    r.Line(a, b, 3 * zoom_, Color(69, 90, 100));
    r.Line(b, c, 3 * zoom_, Color(114, 148, 146));
    r.Glow(c, 65 * zoom_, Color(87, 218, 190, .9f));
    r.Line(c, {c.x - 13 * zoom_, c.y}, 3 * zoom_, Emissive(Color(183, 251, 215), 5));
}

void PrototypeWorld::Draw(const RainActor&)
{
    auto& r = *activeRenderer_;
    for (int i = 0; i < 145; ++i)
    {
        float x = std::fmod(float(Mix(i) % 10000) + time_ * 42, float(r.Width()));
        float y = std::fmod(float(Mix(i + 150) % 10000) + time_ * 380, float(r.Height()));
        r.Line({x, y}, {x - 5, y + 16}, 1, Color(117, 155, 171, .16f));
    }
}

void PrototypeWorld::Draw(const HudActor&)
{
    Hud(*activeRenderer_);
}
