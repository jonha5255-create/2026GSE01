#pragma once
#include "Actor.h"
#include "GameTypes.h"
#include "ActorRenderContext.h"
#include <array>
struct GameplayContext;
enum class YokaiForm
{
    Sword,
    Ranged,
    Claw
};

struct YokaiGrowth
{
    int level = 1, experience = 0;
    float skillCooldown = 0;
};

class PlayerActor : public Actor
{
  public:
    PlayerActor() : Actor("Player")
    {
    }

    void Update(float dt, ActorUpdateContext& context) override;

    int UpdateOrder() const override
    {
        return 10;
    }

    void Render(ActorRenderContext& r) const override
    {
        r.Draw(*this);
    }

    float Damage() const;
    void SelectYokai(YokaiForm form);
    const char* YokaiName() const;
    std::array<YokaiGrowth, 3> yokaiGrowth{};

    const YokaiGrowth& Companion() const
    {
        return yokaiGrowth[int(yokai)];
    }

    int Evolution() const
    {
        return Companion().level >= 9   ? 3
               : Companion().level >= 6 ? 2
               : Companion().level >= 3 ? 1
                                        : 0;
    }

    int YokaiExperienceRequired() const
    {
        return 30 + Companion().level * 20;
    }

    const char* SkillName() const;
    void AddYokaiExperience(int amount, GameplayContext& context);
    float StrikeRange() const;
    float StrikeCooldown() const;
    float StrikeDamage() const;
    YokaiForm yokai = YokaiForm::Ranged;
    float Cooldown() const;
    float MaximumHealth() const;
    float Range() const;
    float PickupRange() const;
    float MoveSpeed() const;
    int ExperienceRequired() const;
    void DamagePlayer(float damage, GameplayContext& context);
    void AddExperience(int amount, GameplayContext& context);
    void Collect(LootKind kind, int amount, GameplayContext& context);
    int level = 1, experience = 0, weaponRank = 0;
    float health = 100, invulnerable = 0, magnetTime = 0;
    bool weapon = false, showRange = true;
    FarmPoint moveInput;
    void ApplyShotRecoil(FarmPoint direction, bool powerful = false);
    float firingHold = 0;
    FarmPoint recoilVelocity;
    float quietTime_ = 0, walkPhase = 0, transformPhase = 0;
};

class EnemyActor : public Actor
{
  public:
    explicit EnemyActor(bool isBoss = false) : Actor(isBoss ? "Boss" : "Enemy"), boss(isBoss)
    {
    }

    void Update(float dt, ActorUpdateContext& context) override;

    int UpdateOrder() const override
    {
        return 30;
    }

    void Render(ActorRenderContext& r) const override
    {
        r.Draw(*this);
    }

    void TakeDamage(float amount, GameplayContext& context);
    float health = 36, maximumHealth = 36, speed = 54, radius = 12;
    float attackTimer = 1, windup = 0, hitFlash = 0;
    bool boss = false;

  private:
    ActorId warning_ = NoActor;
};

class ProjectileActor : public Actor
{
  public:
    ProjectileActor(FarmPoint velocity, float range, float damage)
        : Actor("Projectile"), velocity(velocity), range(range), damage(damage)
    {
        layer = RenderLayer::Projectile;
    }

    void Update(float dt, ActorUpdateContext& context) override;

    int UpdateOrder() const override
    {
        return 40;
    }

    void Render(ActorRenderContext& r) const override
    {
        r.Draw(*this);
    }

    FarmPoint velocity;
    ActorId humanTarget = NoActor;
    float traveled = 0, range = 320, damage = 14;
};

class LootActor : public Actor
{
  public:
    LootActor(LootKind kind, int amount = 12) : Actor("Loot"), kind(kind), amount(amount)
    {
    }

    void Update(float dt, ActorUpdateContext& context) override;

    int UpdateOrder() const override
    {
        return 20;
    }

    void Render(ActorRenderContext& r) const override
    {
        r.Draw(*this);
    }

    LootKind kind;
    int amount;
    float age = 0;
};

class EffectActor : public Actor
{
  public:
    EffectActor(std::string text, float life) : Actor("Effect"), text(std::move(text)), life(life)
    {
        layer = RenderLayer::Effect;
    }

    void Update(float dt, ActorUpdateContext&) override;

    void Render(ActorRenderContext& r) const override
    {
        r.Draw(*this);
    }

    std::string text;
    float life;
};

class PortalActor : public Actor
{
  public:
    PortalActor() : Actor("BossPortal")
    {
        visible = false;
    }

    void Render(ActorRenderContext& r) const override
    {
        r.Draw(*this);
    }
};

class TelegraphActor : public Actor
{
  public:
    TelegraphActor() : Actor("BossTelegraph")
    {
        layer = RenderLayer::Telegraph;
    }

    void Render(ActorRenderContext& r) const override
    {
        r.Draw(*this);
    }

    float remaining = 1.1f;
};

class WeaponActor : public Actor
{
  public:
    void Update(float dt, ActorUpdateContext& context) override;

    int UpdateOrder() const override
    {
        return 15;
    }

    bool Shoot(GameplayContext& context);
    bool CastSkill(GameplayContext& context);
    void UpdateSkill(float dt, GameplayContext& context);
    float skillTime = -1, skillDamage = 0;
    int skillPulses = 0;
    YokaiForm skillForm = YokaiForm::Sword;
    bool ShootAt(Actor& target, GameplayContext& context);
    void StartSwing(FarmPoint target);
    float SwingProgress() const;
    float SwingLunge() const;
    float swingTime = 0, swingDuration = .42f;
    int swingSide = 1;
    FarmPoint aim{1, 0};
    float shotTimer_ = 0, muzzleFlash = 0;

    WeaponActor() : Actor("YokaiWeapon")
    {
        depthBias = .1f;
    }

    void Render(ActorRenderContext& r) const override
    {
        r.Draw(*this);
    }
};

class RangeActor : public Actor
{
  public:
    RangeActor() : Actor("AttackRange")
    {
        layer = RenderLayer::Telegraph;
    }

    void Render(ActorRenderContext& r) const override
    {
        r.Draw(*this);
    }
};
