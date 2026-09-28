#include "stdafx.h"
#include "LevelOne.h"
#include <algorithm>

LevelOne::LevelOne(uint32_t seed) : scene_(std::make_unique<SceneGraph>()), random_(seed ? seed : 1)
{
    player_ = scene_->Spawn<PlayerActor>(NoActor).Id();
    weapon_ = scene_->Spawn<WeaponActor>(player_).Id();
    scene_->Spawn<RangeActor>(player_);
    portal_ = scene_->Spawn<PortalActor>(NoActor).Id();
}

uint32_t LevelOne::Random()
{
    random_ ^= random_ << 13;
    random_ ^= random_ >> 17;
    random_ ^= random_ << 5;
    return random_;
}

void LevelOne::Shift(float dx, float dy)
{
    scene_->Rebase({dx, dy});
    Player().SetPosition({});
}

GameplayContext LevelOne::Context(const Walkable& walkable)
{
    GameplayContext context(*scene_, Player(), navigation_, walkable, state, pickups);
    context.society = society.enabled ? &society : nullptr;
    context.defeated = [this, walkable](const EnemyActor& enemy)
    {
        Defeat(enemy, walkable);
    };
    context.findSpawn = [this, walkable](FarmPoint& out, float near, float far)
    {
        return FindSpawn(out, walkable, near, far);
    };
    return context;
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
        if (!navigation_.Reachable(p) || !walkable(p.x, p.y, 24))
        {
            continue;
        }
        bool occupied = false;
        for (auto* enemy : Enemies())
        {
            if (Length(Minus(p, enemy->Position())) < 50)
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

EnemyActor& LevelOne::SpawnEnemy(FarmPoint position, bool boss)
{
    auto& enemy = scene_->Spawn<EnemyActor>(NoActor, boss);
    enemy.SetPosition(position);
    enemy.health = enemy.maximumHealth = boss ? 850.f : 32 + std::min(kills, 20) * 1.4f;
    enemy.speed = boss ? 42 : 48 + float(Random() % 18);
    enemy.radius = boss ? 20.f : 12.f;
    enemy.attackTimer = boss ? 1.8f : 1.f;
    return enemy;
}

void LevelOne::Defeat(const EnemyActor& enemy, const Walkable& walkable)
{
    auto context = Context(walkable);
    FarmPoint position = enemy.Position();
    if (enemy.boss)
    {
        state = RunState::Won;
        Player().AddExperience(60, context);
        for (int i = 0; i < 5; ++i)
        {
            context.Drop({position.x + i * 8, position.y}, LootKind::SoulStone, 20);
        }
        context.Drop(position, LootKind::Upgrade);
        context.Drop({position.x + 20, position.y}, LootKind::Heal);
        Player().magnetTime = 20;
        Player().health = MaximumHealth();
        context.Effect(position, "균열의 수문장 처치", 2.5f);
        for (auto* other : Enemies())
        {
            other->Destroy();
        }
        for (auto* bullet : Bullets())
        {
            bullet->Destroy();
        }
        return;
    }
    ++kills;
    context.Drop(position, LootKind::SoulStone, 12);
    LootKind bonus = kills % 3 == 1   ? LootKind::Upgrade
                     : kills % 3 == 2 ? LootKind::Heal
                                      : LootKind::Magnet;
    context.Drop({position.x + 12, position.y}, bonus);
}

void LevelOne::Interact(const Walkable& walkable)
{
    auto position = Portal().Position();
    if (state != RunState::BossReady || Length(position) > 95
        || !walkable(position.x, position.y, 20))
    {
        return;
    }
    for (auto* enemy : Enemies())
    {
        enemy->Destroy();
    }
    for (auto* bullet : Bullets())
    {
        bullet->Destroy();
    }
    scene_->CollectDestroyed();
    SpawnEnemy(position, true);
    state = RunState::BossFight;
    Portal().visible = false;
    Player().invulnerable = 1.5f;
}

void LevelOne::Update(float dt,
                      const Walkable& walkable,
                      const std::function<void(float, float)>& movePlayer)
{
    dt = std::max(0.f, std::min(dt, .05f));
    if (state != RunState::Lost && state != RunState::Won)
    {
        navigationTimer_ -= dt;
        if (navigationTimer_ <= 0 && (!society.enabled || !Enemies().empty()))
        {
            navigation_.Rebuild(walkable);
            navigationTimer_ = .3f;
        }
        if (state == RunState::Farming && !society.enabled)
        {
            spawnTimer_ -= dt;
            if (spawnTimer_ <= 0 && Enemies().size() < 8)
            {
                FarmPoint spawn;
                if (FindSpawn(spawn, walkable, kills < 3 ? 225.f : 330.f, 480))
                {
                    SpawnEnemy(spawn);
                }
                spawnTimer_ = kills < 3 ? 2.7f : 2.f;
            }
            if (kills >= 12 && Player().level >= 3 && Player().weaponRank >= 1 && pickups >= 4)
            {
                FarmPoint position;
                if (FindSpawn(position, walkable, 160, 280))
                {
                    Portal().SetPosition(position);
                    Portal().visible = true;
                    state = RunState::BossReady;
                }
            }
        }
    }
    auto context = Context(walkable);
    context.movePlayer = movePlayer;
    if (society.enabled)
    {
        society.Tick(dt, context);
    }
    scene_->Update(dt, context);
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
    if (Player().weaponRank == 0)
    {
        return "노란 조각을 주우면 무기가 강해져. 가까이 가면 자동으로 끌려와.";
    }
    if (Player().magnetTime > 0)
    {
        return "자석이 켜졌어! 멀리 있는 영혼석도 끌어당길 수 있어.";
    }
    return "영혼석은 경험치, 초록 구슬은 회복이야. 움직이며 아이템을 모아 봐.";
}
