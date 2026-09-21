#include "stdafx.h"
#include "GameplayActors.h"

void EffectActor::Update(float dt, ActorUpdateContext&)
{
    life -= dt;
    local.y -= dt * 12;
    if (life <= 0)
    {
        Destroy();
    }
}
