#include "stdafx.h"
#include "GameplayActors.h"
#include "GameplayContext.h"
#include <algorithm>

void LootActor::Update(float dt, ActorUpdateContext& services)
{
    auto& context = dynamic_cast<GameplayContext&>(services);
    if (context.state == RunState::Lost)
    {
        return;
    }
    age += dt;
    FarmPoint position = Position();
    if (age > 120 || Length(position) > 1800)
    {
        Destroy();
        return;
    }
    auto& player = context.player;
    if (kind == LootKind::Heal && player.health >= player.MaximumHealth())
    {
        return;
    }
    FarmPoint delta = Minus(player.Position(), position);
    float distance = Length(delta);
    if (distance < player.PickupRange()
        && Navigation::Sight(position, player.Position(), context.walkable, 2))
    {
        float step = std::min(distance, (player.magnetTime > 0 ? 560.f : 280.f) * dt);
        auto direction = Unit(delta);
        position.x += direction.x * step;
        position.y += direction.y * step;
        SetPosition(position);
        if (Length(Minus(position, player.Position())) < 16)
        {
            player.Collect(kind, amount, context);
            Destroy();
        }
    }
}
