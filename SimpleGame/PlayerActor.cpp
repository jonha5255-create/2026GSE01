#include "stdafx.h"
#include "GameplayActors.h"
#include "GameplayContext.h"
#include <algorithm>

void PlayerActor::SelectYokai(YokaiForm form)
{
    yokai = form;
    weapon = false;
    transformPhase = 1;
}

const char* PlayerActor::YokaiName() const
{
    return yokai == YokaiForm::Sword    ? "검 요괴"
           : yokai == YokaiForm::Ranged ? "원거리 요괴"
                                        : "클로 요괴";
}

float PlayerActor::StrikeRange() const
{
    return weapon && yokai == YokaiForm::Ranged ? Range()
           : weapon && yokai == YokaiForm::Claw ? 65.f
                                                : 85.f;
}

float PlayerActor::StrikeCooldown() const
{
    return weapon && yokai == YokaiForm::Claw ? .2f : .5f;
}

float PlayerActor::StrikeDamage() const
{
    return (yokai == YokaiForm::Claw ? 11.f : 24.f) + (level - 1) * 2.f + weaponRank * 3.f
           + (Companion().level - 1) * 2.f;
}

float PlayerActor::Damage() const
{
    return 14.f + (level - 1) * 2.5f + weaponRank * 3.5f + (Companion().level - 1) * 2.f;
}

float PlayerActor::Cooldown() const
{
    return std::max(.24f, .85f - (level - 1) * .055f - weaponRank * .02f);
}

float PlayerActor::MaximumHealth() const
{
    return 100.f + (level - 1) * 14.f;
}

float PlayerActor::Range() const
{
    return 320.f + std::min(level - 1, 9) * 6.f;
}

float PlayerActor::PickupRange() const
{
    return magnetTime > 0 ? 440.f : 105.f + (level - 1) * 5.f;
}

float PlayerActor::MoveSpeed() const
{
    return 180.f + std::min(level - 1, 9) * 2.f;
}

int PlayerActor::ExperienceRequired() const
{
    return 30 + level * 15;
}

void PlayerActor::DamagePlayer(float damage, GameplayContext& context)
{
    if (invulnerable > 0 || context.state == RunState::Won || context.state == RunState::Lost)
    {
        return;
    }
    health = std::max(0.f, health - damage);
    invulnerable = .8f;
    quietTime_ = 0;
    context.Effect({}, "-" + std::to_string(int(damage)), .8f);
    if (health <= 0)
    {
        context.state = RunState::Lost;
        for (auto* bullet : context.scene.Query<ProjectileActor>())
        {
            bullet->Destroy();
        }
    }
}

void PlayerActor::AddExperience(int amount, GameplayContext& context)
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
        context.Effect({}, "레벨 상승!", 1.7f);
    }
    if (level >= 10)
    {
        experience = 0;
    }
}

void PlayerActor::Collect(LootKind kind, int amount, GameplayContext& context)
{
    ++context.pickups;
    switch (kind)
    {
    case LootKind::SoulStone:
        AddExperience(amount, context);
        context.Effect({}, "경험치 +" + std::to_string(amount), .8f);
        break;
    case LootKind::Upgrade:
        if (weaponRank < 8)
        {
            ++weaponRank;
            context.Effect({}, "무기 강화 +1", 1.4f);
        }
        else
        {
            AddExperience(20, context);
        }
        break;
    case LootKind::Heal:
        health = std::min(MaximumHealth(), health + 35);
        context.Effect({}, "체력 회복", 1.2f);
        break;
    case LootKind::Magnet:
        magnetTime = 12;
        context.Effect({}, "자석 효과 12초", 1.2f);
        break;
    }
}

void PlayerActor::ApplyShotRecoil(FarmPoint direction, bool powerful)
{
    firingHold = powerful ? .24f : .16f;
    auto forward = Unit(direction);
    float speed = powerful ? 160.f : 100.f;
    recoilVelocity = {-forward.x * speed, -forward.y * speed};
}

void PlayerActor::Update(float dt, ActorUpdateContext& services)
{
    for (auto& growth : yokaiGrowth)
    {
        growth.skillCooldown = std::max(0.f, growth.skillCooldown - dt);
    }
    auto& context = dynamic_cast<GameplayContext&>(services);
    invulnerable = std::max(0.f, invulnerable - dt);
    magnetTime = std::max(0.f, magnetTime - dt);
    transformPhase = std::max(0.f, transformPhase - dt * 1.5f);
    if (context.state == RunState::Lost || context.state == RunState::Won)
    {
        firingHold = 0;
        recoilVelocity = {};
        return;
    }
    float movementTime = std::max(0.f, dt - firingHold);
    firingHold = std::max(0.f, firingHold - dt);
    float decay = std::exp(-14.f * dt);
    if (context.movePlayer && Length(recoilVelocity) > .01f)
    {
        // Route recoil through the world's collision-aware movement, never teleport.
        context.movePlayer(recoilVelocity.x * (1 - decay) / 14.f,
                           recoilVelocity.y * (1 - decay) / 14.f);
    }
    recoilVelocity.x *= decay;
    recoilVelocity.y *= decay;
    if (Length(moveInput) > 0 && context.movePlayer && movementTime > 0)
    {
        auto input = Unit(moveInput);
        auto direction = Unit({input.x + input.y * 2, -input.x + input.y * 2});
        context.movePlayer(direction.x * MoveSpeed() * movementTime,
                           direction.y * MoveSpeed() * movementTime);
        walkPhase += movementTime * 11;
    }
    if (context.state == RunState::Won)
    {
        return;
    }
    quietTime_ += dt;
    if (quietTime_ > 6)
    {
        health = std::min(MaximumHealth(), health + dt * 1.5f);
    }
}

const char* PlayerActor::SkillName() const
{
    return yokai == YokaiForm::Sword    ? "월영 참격"
           : yokai == YokaiForm::Ranged ? "혼령 산탄"
                                        : "삼연 발톱";
}

void PlayerActor::AddYokaiExperience(int amount, GameplayContext& context)
{
    auto& growth = yokaiGrowth[int(yokai)];
    if (amount <= 0 || growth.level >= 10)
    {
        return;
    }
    growth.experience += amount;
    while (growth.level < 10 && growth.experience >= YokaiExperienceRequired())
    {
        growth.experience -= YokaiExperienceRequired();
        ++growth.level;
        transformPhase = 1;
        context.Effect(Position(),
                       std::string(YokaiName()) + " LV." + std::to_string(growth.level)
                           + (growth.level == 3                        ? " / 스킬 해금! 우클릭"
                              : growth.level == 6 || growth.level == 9 ? " / 진화!"
                                                                       : ""),
                       2.2f);
    }
    if (growth.level == 10)
    {
        growth.experience = 0;
    }
}
