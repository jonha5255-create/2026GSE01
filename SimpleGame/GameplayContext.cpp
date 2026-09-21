#include "stdafx.h"
#include "GameplayContext.h"
#include "GameplayActors.h"

EffectActor& GameplayContext::Effect(FarmPoint position, const std::string& text, float life)
{
    auto effects = scene.Query<EffectActor>();
    if (effects.size() >= 12)
    {
        effects.front()->Destroy();
    }
    auto& effect = scene.Spawn<EffectActor>(NoActor, text, life);
    effect.SetPosition(position);
    return effect;
}

LootActor& GameplayContext::Drop(FarmPoint position, LootKind kind, int amount)
{
    auto items = scene.Query<LootActor>();
    if (items.size() >= 128)
    {
        items.front()->Destroy();
    }
    auto& item = scene.Spawn<LootActor>(NoActor, kind, amount);
    item.SetPosition(position);
    return item;
}
