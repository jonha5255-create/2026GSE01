#include "stdafx.h"
#include "GameplayActors.h"
#include "GameplayContext.h"
#include <algorithm>
#include <cmath>

void ProjectileActor::Update(float dt, ActorUpdateContext& services)
{
    auto& context = dynamic_cast<GameplayContext&>(services);
    if (context.state == RunState::Lost || context.state == RunState::Won)
    {
        Destroy();
        return;
    }
    float speed = Length(velocity);
    float distance = std::min(speed * dt, range - traveled);
    int steps = std::max(1, int(std::ceil(distance / 5)));
    auto enemies = Scene().Query<EnemyActor>();
    FarmPoint position = Position();
    for (int step = 0; step < steps && traveled < range; ++step)
    {
        float movement = distance / steps;
        if (speed > 0)
        {
            position.x += velocity.x / speed * movement;
            position.y += velocity.y / speed * movement;
        }
        traveled += movement;
        SetPosition(position);
        if (!context.walkable(position.x, position.y, 3))
        {
            Destroy();
            return;
        }
        for (auto* enemy : enemies)
        {
            if (Scene().Active(enemy->Id()) && enemy->health > 0
                && Length(Minus(position, enemy->Position())) < enemy->radius + 4)
            {
                enemy->TakeDamage(damage, context);
                Destroy();
                return;
            }
        }
    }
    if (traveled >= range || speed <= 0)
    {
        Destroy();
    }
}
