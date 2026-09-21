#include "stdafx.h"
#include "GameplayActors.h"
#include "GameplayContext.h"
#include <algorithm>

void EnemyActor::TakeDamage(float amount, GameplayContext& context)
{
    if (health <= 0 || !Scene().Get(Id()))
    {
        return;
    }
    health = std::max(0.f, health - amount);
    hitFlash = .15f;
    context.Effect(Position(), std::to_string(int(amount)), .6f);
    if (health <= 0)
    {
        if (context.defeated)
        {
            context.defeated(*this);
        }
        Destroy();
    }
}

void EnemyActor::Update(float dt, ActorUpdateContext& services)
{
    auto& context = dynamic_cast<GameplayContext&>(services);
    if (context.state == RunState::Lost || context.state == RunState::Won)
    {
        return;
    }
    hitFlash = std::max(0.f, hitFlash - dt);
    if (health <= 0)
    {
        Destroy();
        return;
    }
    FarmPoint position = Position();
    if (!boss && Length(position) > 900)
    {
        Destroy();
        return;
    }
    if (boss && Length(position) > 650 && context.findSpawn)
    {
        FarmPoint spawn;
        if (context.findSpawn(spawn, 360, 480))
        {
            SetPosition(spawn);
            position = spawn;
            Scene().Destroy(warning_);
            warning_ = NoActor;
            windup = 0;
            attackTimer = 2;
        }
    }
    if (windup > 0)
    {
        windup -= dt;
        auto* warning = Scene().Get<TelegraphActor>(warning_);
        if (warning)
        {
            warning->remaining = windup;
        }
        if (windup <= 0)
        {
            FarmPoint target = warning ? warning->Position() : context.player.Position();
            if (Length(Minus(target, context.player.Position())) < 105
                && Navigation::Sight(position, context.player.Position(), context.walkable, 2))
            {
                context.player.DamagePlayer(28, context);
            }
            context.Effect(target, "충격파", .7f);
            Scene().Destroy(warning_);
            warning_ = NoActor;
            attackTimer = 2.8f;
        }
        return;
    }
    FarmPoint direction = context.navigation.Direction(position, context.walkable);
    float dx = direction.x * speed * dt, dy = direction.y * speed * dt;
    if (context.walkable(position.x + dx, position.y, radius))
    {
        position.x += dx;
    }
    if (context.walkable(position.x, position.y + dy, radius))
    {
        position.y += dy;
    }
    SetPosition(position);
    attackTimer -= dt;
    if (boss && attackTimer <= 0 && Length(Minus(position, context.player.Position())) < 380)
    {
        windup = 1.1f;
        auto& warning = Scene().Spawn<TelegraphActor>(Id());
        warning.SetPosition(context.player.Position());
        warning_ = warning.Id();
    }
    if (Length(Minus(position, context.player.Position())) < radius + 12)
    {
        context.player.DamagePlayer(boss ? 18.f : 9.f, context);
    }
}
