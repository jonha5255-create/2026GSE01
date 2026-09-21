#include "stdafx.h"
#include "GameplayActors.h"
#include "GameplayContext.h"
#include <algorithm>

float PlayerActor::Damage() const
{
    return 14.f + (level - 1) * 2.5f + weaponRank * 3.5f;
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

void PlayerActor::Update(float dt, ActorUpdateContext& services)
{
    auto& context = dynamic_cast<GameplayContext&>(services);
    invulnerable = std::max(0.f, invulnerable - dt);
    magnetTime = std::max(0.f, magnetTime - dt);
    transformPhase = std::max(0.f, transformPhase - dt * 1.5f);
    if (context.state == RunState::Lost)
    {
        return;
    }
    if (Length(moveInput) > 0 && context.movePlayer)
    {
        auto input = Unit(moveInput);
        auto direction = Unit({input.x + input.y * 2, -input.x + input.y * 2});
        context.movePlayer(direction.x * MoveSpeed() * dt, direction.y * MoveSpeed() * dt);
        walkPhase += dt * 11;
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
