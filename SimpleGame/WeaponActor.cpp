#include "stdafx.h"
#include "GameplayActors.h"
#include "GameplayContext.h"
#include <algorithm>

void WeaponActor::Update(float dt, ActorUpdateContext& services)
{
    auto& context = dynamic_cast<GameplayContext&>(services);
    shotTimer_ = std::max(0.f, shotTimer_ - dt);
    muzzleFlash = std::max(0.f, muzzleFlash - dt);
    auto* owner = Scene().Get<PlayerActor>(Parent());
    if (owner && owner->weapon && context.state != RunState::Won && context.state != RunState::Lost)
    {
        Shoot(context);
    }
}

bool WeaponActor::Shoot(GameplayContext& context)
{
    auto* owner = Scene().Get<PlayerActor>(Parent());
    if (!owner || shotTimer_ > 0)
    {
        return false;
    }
    EnemyActor* target = nullptr;
    float nearest = owner->Range();
    for (auto* enemy : context.scene.Query<EnemyActor>())
    {
        FarmPoint delta = Minus(enemy->Position(), Position());
        float distance = Length(delta);
        if (enemy->health > 0 && distance <= nearest
            && Navigation::Sight(Position(), enemy->Position(), context.walkable))
        {
            target = enemy;
            nearest = distance;
        }
    }
    if (!target)
    {
        return false;
    }
    aim = Unit(Minus(target->Position(), Position()));
    auto& bullet = context.scene.Spawn<ProjectileActor>(
        NoActor, FarmPoint{aim.x * 560, aim.y * 560}, owner->Range(), owner->Damage());
    bullet.SetPosition(Position());
    shotTimer_ = owner->Cooldown();
    muzzleFlash = .12f;
    return true;
}
