#include "stdafx.h"
#include "LevelOne.h"
#include <algorithm>
#include <cmath>
#include <queue>
#include <sstream>
#include <iostream>

namespace
{
    float Length(FarmPoint p)
    {
        return std::sqrt(p.x * p.x + p.y * p.y);
    }

    FarmPoint Minus(FarmPoint a, FarmPoint b)
    {
        return {a.x - b.x, a.y - b.y};
    }

    FarmPoint Unit(FarmPoint p)
    {
        float length = Length(p);
        return length > .001f ? FarmPoint{p.x / length, p.y / length} : FarmPoint{};
    }
} // namespace

LevelOne::LevelOne(uint32_t seed) : random_(seed ? seed : 1), navigation_(NavWidth * NavWidth, -1)
{
}

uint32_t LevelOne::Random()
{
    random_ ^= random_ << 13;
    random_ ^= random_ >> 17;
    random_ ^= random_ << 5;
    return random_;
}

float LevelOne::Damage() const
{
    return 14.f + (level - 1) * 2.5f + weaponRank * 3.5f;
}

float LevelOne::Cooldown() const
{
    return std::max(.24f, .85f - (level - 1) * .055f - weaponRank * .02f);
}

float LevelOne::MaximumHealth() const
{
    return 100.f + (level - 1) * 14.f;
}

float LevelOne::Range() const
{
    return 320.f + std::min(level - 1, 9) * 6.f;
}

float LevelOne::PickupRange() const
{
    return magnetTime > 0 ? 440.f : 105.f + (level - 1) * 5.f;
}

float LevelOne::MoveSpeed() const
{
    return 180.f + std::min(level - 1, 9) * 2.f;
}

int LevelOne::ExperienceRequired() const
{
    return 30 + level * 15;
}

void LevelOne::Shift(float dx, float dy)
{
    auto shift = [&](FarmPoint& p)
    {
        p.x -= dx;
        p.y -= dy;
    };
    for (auto& e : enemies_)
    {
        shift(e.position);
        shift(e.attackPosition);
    }
    for (auto& b : bullets_)
    {
        shift(b.position);
    }
    for (auto& l : loot_)
    {
        shift(l.position);
    }
    for (auto& e : effects_)
    {
        shift(e.position);
    }
    shift(portal);
}

bool LevelOne::Sight(FarmPoint a, FarmPoint b, const Walkable& walkable, float radius) const
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

void LevelOne::Navigate(const Walkable& walkable)
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

FarmPoint LevelOne::ChaseDirection(FarmPoint position, const Walkable& walkable) const
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

bool LevelOne::FindSpawn(FarmPoint& out,
                         const Walkable& walkable,
                         float minDistance,
                         float maxDistance)
{
    for (int i = 0; i < 100; ++i)
    {
        float angle = float(Random() % 10000) * 6.2831853f / 10000;
        float radius = minDistance + float(Random() % 10000) / 10000 * (maxDistance - minDistance);
        FarmPoint p{std::cos(angle) * radius, std::sin(angle) * radius};
        int x = int(std::round(p.x / NavCell)) + NavWidth / 2,
            y = int(std::round(p.y / NavCell)) + NavWidth / 2;
        if (x < 0 || y < 0 || x >= NavWidth || y >= NavWidth)
        {
            continue;
        }
        if (navigation_[y * NavWidth + x] < 0 || !walkable(p.x, p.y, 24))
        {
            continue;
        }
        bool occupied = false;
        for (const auto& e : enemies_)
        {
            if (Length(Minus(p, e.position)) < 50)
            {
                occupied = true;
            }
        }
        if (!occupied)
        {
            out = p;
            return true;
        }
    }
    return false;
}

bool LevelOne::Shoot(const Walkable& walkable)
{
    if (shotTimer_ > 0)
    {
        return false;
    }
    const FarmEnemy* target = nullptr;
    float nearest = Range();
    for (const auto& enemy : enemies_)
    {
        float distance = Length(enemy.position);
        if (enemy.health > 0 && distance <= nearest && Sight({}, enemy.position, walkable))
        {
            target = &enemy;
            nearest = distance;
        }
    }
    if (!target)
    {
        return false;
    }
    aim = Unit(target->position);
    bullets_.push_back({{}, {aim.x * 560, aim.y * 560}, 0, Range(), Damage()});
    shotTimer_ = Cooldown();
    muzzleFlash = .12f;
    return true;
}

void LevelOne::DamagePlayer(float damage)
{
    if (invulnerable > 0 || state == RunState::Won || state == RunState::Lost)
    {
        return;
    }
    health = std::max(0.f, health - damage);
    invulnerable = .8f;
    quietTime_ = 0;
    effects_.push_back({{}, "-" + std::to_string(int(damage)), .8f});
    if (health <= 0)
    {
        state = RunState::Lost;
        bullets_.clear();
    }
}

void LevelOne::AddExperience(int amount)
{
    if (level >= 10)
    {
        return;
    }
    experience += amount;
    while (level < 10 && experience >= ExperienceRequired())
    {
        experience -= ExperienceRequired();
        ++level;
        health = std::min(MaximumHealth(), health + 28);
        effects_.push_back({{}, "레벨 상승!", 1.7f});
    }
    if (level >= 10)
    {
        experience = 0;
    }
}

void LevelOne::Collect(const FarmLoot& item)
{
    ++pickups;
    switch (item.kind)
    {
    case LootKind::SoulStone:
        AddExperience(item.amount);
        effects_.push_back({{}, "경험치 +" + std::to_string(item.amount), .8f});
        break;
    case LootKind::Upgrade:
        if (weaponRank < 8)
        {
            ++weaponRank;
            effects_.push_back({{}, "무기 강화 +1", 1.4f});
        }
        else
        {
            AddExperience(20);
        }
        break;
    case LootKind::Heal:
        health = std::min(MaximumHealth(), health + 35);
        effects_.push_back({{}, "체력 회복", 1.2f});
        break;
    case LootKind::Magnet:
        magnetTime = 12;
        effects_.push_back({{}, "자석 효과 12초", 1.2f});
        break;
    }
}

void LevelOne::Defeat(const FarmEnemy& enemy)
{
    if (enemy.boss)
    {
        state = RunState::Won;
        AddExperience(60);
        for (int i = 0; i < 5; ++i)
        {
            loot_.push_back(
                {{enemy.position.x + i * 8.f, enemy.position.y}, LootKind::SoulStone, 20});
        }
        loot_.push_back({enemy.position, LootKind::Upgrade});
        loot_.push_back({{enemy.position.x + 20, enemy.position.y}, LootKind::Heal});
        magnetTime = 20;
        health = MaximumHealth();
        effects_.push_back({enemy.position, "균열의 수문장 처치", 2.5f});
        return;
    }
    ++kills;
    loot_.push_back({enemy.position, LootKind::SoulStone, 12});
    LootKind bonus = kills % 3 == 1   ? LootKind::Upgrade
                     : kills % 3 == 2 ? LootKind::Heal
                                      : LootKind::Magnet;
    loot_.push_back({{enemy.position.x + 12, enemy.position.y}, bonus});
}

void LevelOne::UpdateLoot(float dt, const Walkable& walkable)
{
    for (auto& item : loot_)
    {
        item.age += dt;
        float distance = Length(item.position);
        if (item.kind == LootKind::Heal && health >= MaximumHealth())
        {
            continue;
        }
        if (distance < PickupRange() && Sight(item.position, {}, walkable, 2))
        {
            float step = std::min(distance, (magnetTime > 0 ? 560.f : 280.f) * dt);
            FarmPoint direction = Unit({-item.position.x, -item.position.y});
            item.position.x += direction.x * step;
            item.position.y += direction.y * step;
            if (Length(item.position) < 16)
            {
                Collect(item);
                item.age = 1000;
            }
        }
    }
    loot_.erase(std::remove_if(loot_.begin(),
                               loot_.end(),
                               [](const FarmLoot& i)
                               {
                                   return i.age > 120 || Length(i.position) > 1800;
                               }),
                loot_.end());
    if (loot_.size() > 128)
    {
        loot_.erase(loot_.begin(), loot_.begin() + (loot_.size() - 128));
    }
}

void LevelOne::Interact(const Walkable& walkable)
{
    if (state != RunState::BossReady || Length(portal) > 95)
    {
        return;
    }
    if (!walkable(portal.x, portal.y, 20))
    {
        return;
    }
    FarmEnemy boss;
    boss.id = nextId_++;
    boss.position = portal;
    boss.health = boss.maximumHealth = 850;
    boss.speed = 42;
    boss.radius = 20;
    boss.boss = true;
    boss.attackTimer = 1.8f;
    enemies_.clear();
    enemies_.push_back(boss);
    bullets_.clear();
    state = RunState::BossFight;
    invulnerable = 1.5f;
}

void LevelOne::Update(float dt, bool weapon, const Walkable& walkable)
{
    dt = std::max(0.f, std::min(dt, .05f));
    invulnerable = std::max(0.f, invulnerable - dt);
    magnetTime = std::max(0.f, magnetTime - dt);
    muzzleFlash = std::max(0.f, muzzleFlash - dt);
    shotTimer_ = std::max(0.f, shotTimer_ - dt);
    for (auto& e : effects_)
    {
        e.life -= dt;
        e.position.y -= dt * 12;
    }
    effects_.erase(std::remove_if(effects_.begin(),
                                  effects_.end(),
                                  [](const FarmEffect& e)
                                  {
                                      return e.life <= 0;
                                  }),
                   effects_.end());
    if (effects_.size() > 12)
    {
        effects_.erase(effects_.begin(), effects_.end() - 12);
    }
    if (state == RunState::Lost)
    {
        return;
    }
    UpdateLoot(dt, walkable);
    if (state == RunState::Won)
    {
        return;
    }
    quietTime_ += dt;
    if (quietTime_ > 6)
    {
        health = std::min(MaximumHealth(), health + dt * 1.5f);
    }
    navigationTimer_ -= dt;
    if (navigationTimer_ <= 0)
    {
        Navigate(walkable);
        navigationTimer_ = .3f;
    }
    if (state == RunState::Farming)
    {
        spawnTimer_ -= dt;
        if (spawnTimer_ <= 0 && enemies_.size() < 8)
        {
            FarmPoint spawn;
            if (FindSpawn(spawn, walkable, kills < 3 ? 225.f : 330.f, 480))
            {
                FarmEnemy e;
                e.id = nextId_++;
                e.position = spawn;
                e.health = e.maximumHealth = 32 + std::min(kills, 20) * 1.4f;
                e.speed = 48 + float(Random() % 18);
                enemies_.push_back(e);
            }
            spawnTimer_ = kills < 3 ? 2.7f : 2.f;
        }
        if (kills >= 12 && level >= 3 && weaponRank >= 1 && pickups >= 4)
        {
            if (FindSpawn(portal, walkable, 160, 280))
            {
                state = RunState::BossReady;
            }
        }
    }
    if (weapon)
    {
        Shoot(walkable);
    }
    for (auto& enemy : enemies_)
    {
        enemy.hitFlash = std::max(0.f, enemy.hitFlash - dt);
        if (enemy.health <= 0)
        {
            continue;
        }
        if (enemy.boss && Length(enemy.position) > 650)
        {
            FarmPoint spawn;
            if (FindSpawn(spawn, walkable, 360, 480))
            {
                enemy.position = spawn;
                enemy.windup = 0;
                enemy.attackTimer = 2;
            }
        }
        if (enemy.windup > 0)
        {
            enemy.windup -= dt;
            if (enemy.windup <= 0)
            {
                if (Length(enemy.attackPosition) < 105 && Sight(enemy.position, {}, walkable, 2))
                {
                    DamagePlayer(28);
                }
                effects_.push_back({enemy.attackPosition, "충격파", .7f});
                enemy.attackTimer = 2.8f;
            }
            continue;
        }
        FarmPoint direction = ChaseDirection(enemy.position, walkable);
        float dx = direction.x * enemy.speed * dt, dy = direction.y * enemy.speed * dt;
        if (walkable(enemy.position.x + dx, enemy.position.y, enemy.radius))
        {
            enemy.position.x += dx;
        }
        if (walkable(enemy.position.x, enemy.position.y + dy, enemy.radius))
        {
            enemy.position.y += dy;
        }
        enemy.attackTimer -= dt;
        if (enemy.boss && enemy.attackTimer <= 0 && Length(enemy.position) < 380)
        {
            enemy.windup = 1.1f;
            enemy.attackPosition = {};
        }
        if (Length(enemy.position) < enemy.radius + 12)
        {
            DamagePlayer(enemy.boss ? 18.f : 9.f);
        }
    }
    if (state == RunState::Lost)
    {
        return;
    }
    for (auto& bullet : bullets_)
    {
        float distance = std::min(560 * dt, bullet.range - bullet.traveled);
        int steps = std::max(1, int(std::ceil(distance / 5)));
        for (int step = 0; step < steps && bullet.traveled < bullet.range; ++step)
        {
            float movement = distance / steps;
            bullet.position.x += bullet.velocity.x / 560 * movement;
            bullet.position.y += bullet.velocity.y / 560 * movement;
            bullet.traveled += movement;
            if (!walkable(bullet.position.x, bullet.position.y, 3))
            {
                bullet.traveled = bullet.range;
                break;
            }
            for (auto& enemy : enemies_)
            {
                if (enemy.health > 0
                    && Length(Minus(bullet.position, enemy.position)) < enemy.radius + 4)
                {
                    enemy.health = std::max(0.f, enemy.health - bullet.damage);
                    enemy.hitFlash = .15f;
                    effects_.push_back({enemy.position, std::to_string(int(bullet.damage)), .6f});
                    bullet.traveled = bullet.range;
                    if (enemy.health <= 0)
                    {
                        Defeat(enemy);
                    }
                    break;
                }
            }
        }
    }
    bullets_.erase(std::remove_if(bullets_.begin(),
                                  bullets_.end(),
                                  [](const FarmBullet& b)
                                  {
                                      return b.traveled >= b.range;
                                  }),
                   bullets_.end());
    enemies_.erase(std::remove_if(enemies_.begin(),
                                  enemies_.end(),
                                  [](const FarmEnemy& e)
                                  {
                                      return e.health <= 0 || (!e.boss && Length(e.position) > 900);
                                  }),
                   enemies_.end());
    if (state == RunState::Won)
    {
        enemies_.clear();
        bullets_.clear();
    }
}

std::string LevelOne::Objective() const
{
    if (state == RunState::Lost)
    {
        return "쓰러졌습니다. R을 눌러 다시 도전하세요.";
    }
    if (state == RunState::Won)
    {
        return "레벨 1 완료! 파밍과 첫 결투를 배웠습니다.";
    }
    if (state == RunState::BossReady)
    {
        return "붉은 균열로 이동한 뒤 E로 보스를 소환하세요.";
    }
    if (state == RunState::BossFight)
    {
        return "붉은 원을 피하며 균열의 수문장을 처치하세요.";
    }
    return "적 12체 처치 / 레벨 3 / 무기 강화 1회";
}

std::string LevelOne::Dialogue(bool weapon) const
{
    if (state == RunState::Lost)
    {
        return "괜찮아. 다시 시작하자. 적에게 둘러싸이지 않게 움직여.";
    }
    if (state == RunState::Won)
    {
        return "해냈어! 모은 영혼으로 우린 더 강해졌어. 다음 구역으로 가자.";
    }
    if (state == RunState::BossReady)
    {
        return "준비가 됐어. 균열 가까이에서 E를 눌러 수문장을 불러내자.";
    }
    if (state == RunState::BossFight)
    {
        return "바닥의 붉은 원이 터지기 전에 벗어나! 내 사거리를 유지해.";
    }
    if (!weapon)
    {
        return "Q를 누르면 내가 원거리 무기로 변할게. 사거리 안의 적은 내가 조준해.";
    }
    if (kills == 0)
    {
        return "푸른 원이 사거리야. 벽 너머는 쏠 수 없어. 가까운 적부터 처리하자.";
    }
    if (weaponRank == 0)
    {
        return "노란 조각을 주우면 무기가 강해져. 가까이 가면 자동으로 끌려와.";
    }
    if (magnetTime > 0)
    {
        return "자석이 켜졌어! 멀리 있는 영혼석도 끌어당길 수 있어.";
    }
    return "영혼석은 경험치, 초록 구슬은 회복이야. 움직이며 아이템을 모아 봐.";
}

bool LevelOne::SelfTest()
{
    bool ok = true;
    auto check = [&](bool pass, const char* name)
    {
        std::cout << (pass ? "PASS " : "FAIL ") << name << '\n';
        ok = ok && pass;
    };
    Walkable open = [](float, float, float)
    {
        return true;
    };
    LevelOne a;
    a.spawnTimer_ = 999;
    FarmEnemy e;
    e.id = 1;
    e.position = {150, 0};
    e.speed = 0;
    e.health = e.maximumHealth = 200;
    a.enemies_.push_back(e);
    check(a.Shoot(open), "auto aim acquires a visible enemy");
    check(!a.Shoot(open), "projectile cooldown cannot be bypassed");
    for (int i = 0; i < 30; ++i)
    {
        a.Update(.02f, false, open);
    }
    check(a.enemies_[0].health < 200, "ranged projectile deals damage");
    a.enemies_[0].position = {a.Range() + 1, 0};
    a.shotTimer_ = 0;
    check(!a.Shoot(open), "auto aim respects range");
    a.enemies_[0].position = {150, 0};
    Walkable wall = [](float x, float, float radius)
    {
        return x < 60 - radius || x > 90 + radius;
    };
    check(!a.Shoot(wall), "auto aim cannot see through a wall");
    float oldDamage = a.Damage(), oldCooldown = a.Cooldown(), oldHealth = a.MaximumHealth();
    a.AddExperience(200);
    check(a.level > 1 && a.Damage() > oldDamage && a.Cooldown() < oldCooldown
              && a.MaximumHealth() > oldHealth,
          "XP improves damage cooldown and max health");
    a.health = 10;
    a.Collect({{}, LootKind::Heal});
    check(a.health == 45, "healing drop restores health");
    a.Collect({{}, LootKind::Upgrade});
    check(a.weaponRank == 1, "upgrade drop improves weapon rank");
    a.Collect({{}, LootKind::Magnet});
    check(a.PickupRange() == 440, "magnet pickup extends collection range");
    a.loot_.push_back({{220, 0}, LootKind::SoulStone, 12});
    int picked = a.pickups;
    for (int i = 0; i < 40; ++i)
    {
        a.UpdateLoot(.02f, open);
    }
    check(a.pickups > picked && a.loot_.empty(), "magnetic attraction collects soul stones");
    a.loot_.push_back({{150, 0}, LootKind::Upgrade});
    for (int i = 0; i < 50; ++i)
    {
        a.UpdateLoot(.02f, wall);
    }
    check(a.loot_.size() == 1 && a.loot_[0].position.x == 150, "loot cannot pass through walls");
    LevelOne drops;
    for (int i = 0; i < 3; ++i)
    {
        drops.Defeat(e);
    }
    bool kinds[4] = {};
    for (const auto& item : drops.loot_)
    {
        kinds[int(item.kind)] = true;
    }
    check(kinds[0] && kinds[1] && kinds[2] && kinds[3], "all four drop types are obtainable");
    LevelOne boss;
    boss.state = RunState::BossReady;
    boss.portal = {200, 0};
    boss.Interact(open);
    check(boss.state == RunState::BossReady, "boss summon requires proximity");
    boss.portal = {70, 0};
    boss.Interact(open);
    check(boss.state == RunState::BossFight && boss.enemies_.size() == 1 && boss.enemies_[0].boss,
          "boss encounter starts once");
    boss.enemies_[0].speed = 0;
    boss.enemies_[0].attackTimer = 999;
    boss.level = 10;
    boss.weaponRank = 8;
    for (int i = 0; i < 1500 && boss.state == RunState::BossFight; ++i)
    {
        boss.Update(.02f, true, open);
    }
    check(boss.state == RunState::Won, "boss can be defeated through normal projectiles");
    LevelOne death;
    death.DamagePlayer(200);
    death.Update(.05f, true, open);
    check(death.state == RunState::Lost && death.health == 0 && death.enemies_.empty(),
          "death stops encounter simulation");
    LevelOne cap;
    cap.AddExperience(999999);
    for (int i = 0; i < 20; ++i)
    {
        cap.Collect({{}, LootKind::Upgrade});
    }
    check(cap.level == 10 && cap.weaponRank == 8 && cap.Cooldown() >= .24f,
          "progression has safe caps");
    LevelOne unlock;
    unlock.kills = 12;
    unlock.level = 3;
    unlock.weaponRank = 1;
    unlock.pickups = 4;
    unlock.Update(.02f, true, open);
    check(unlock.state == RunState::BossReady, "farming objectives unlock boss portal");
    LevelOne projectile;
    projectile.spawnTimer_ = 999;
    projectile.bullets_.push_back({{}, {560, 0}, 0, 100, 14});
    for (int i = 0; i < 30; ++i)
    {
        projectile.Update(.02f, false, open);
    }
    check(projectile.bullets_.empty(), "projectile expires at maximum travel distance");
    projectile.bullets_.push_back({{}, {560, 0}, 0, 320, 14});
    for (int i = 0; i < 10; ++i)
    {
        projectile.Update(.02f, false, wall);
    }
    check(projectile.bullets_.empty(), "in-flight projectiles collide with walls");
    LevelOne immunity;
    immunity.DamagePlayer(9);
    immunity.DamagePlayer(9);
    check(immunity.health == 91, "hit immunity prevents stacked contact damage");
    return ok;
}
