#pragma once
#include "GameplayActors.h"
#include "GameplayContext.h"
#include "LivingWorld.h"
#include <memory>

// Encounter orchestration only. SceneGraph owns every placed Actor.
class LevelOne
{
  public:
    using Walkable = ::Walkable;
    explicit LevelOne(uint32_t seed = 2026);
    LevelOne(LevelOne&&) = default;
    LevelOne& operator=(LevelOne&&) = default;
    void Update(float dt,
                const Walkable& walkable,
                const std::function<void(float, float)>& movePlayer = {});

    WeaponActor& Weapon() const
    {
        return *scene_->Get<WeaponActor>(weapon_);
    }

    void Shift(float dx, float dy);
    void Interact(const Walkable& walkable);

    void ToggleRange()
    {
        Player().showRange = !Player().showRange;
    }

    SceneGraph& Graph() const
    {
        return *scene_;
    }

    PlayerActor& Player() const
    {
        return *scene_->Get<PlayerActor>(player_);
    }

    PortalActor& Portal() const
    {
        return *scene_->Get<PortalActor>(portal_);
    }

    auto Enemies() const
    {
        return scene_->Query<EnemyActor>();
    }

    auto Bullets() const
    {
        return scene_->Query<ProjectileActor>();
    }

    auto Loot() const
    {
        return scene_->Query<LootActor>();
    }

    auto Effects() const
    {
        return scene_->Query<EffectActor>();
    }

    float Damage() const
    {
        return Player().Damage();
    }

    float Cooldown() const
    {
        return Player().Cooldown();
    }

    float MaximumHealth() const
    {
        return Player().MaximumHealth();
    }

    float Range() const
    {
        return Player().Range();
    }

    float MoveSpeed() const
    {
        return Player().MoveSpeed();
    }

    int ExperienceRequired() const
    {
        return Player().ExperienceRequired();
    }

    std::string Objective() const;
    std::string Dialogue(bool weapon) const;
    EnemyActor& SpawnEnemy(FarmPoint position, bool boss = false);
    GameplayContext Context(const Walkable& walkable);
    static bool SelfTest();
    RunState state = RunState::Farming;
    LivingWorld society;
    int kills = 0, pickups = 0;

  private:
    uint32_t Random();
    bool FindSpawn(FarmPoint& out, const Walkable& walkable, float minDistance, float maxDistance);
    void Defeat(const EnemyActor& enemy, const Walkable& walkable);
    std::unique_ptr<SceneGraph> scene_;
    ActorId player_ = NoActor, portal_ = NoActor, weapon_ = NoActor;
    Navigation navigation_;
    uint32_t random_;
    float spawnTimer_ = 1.5f, navigationTimer_ = 0;
};
