#include "stdafx.h"
#include "WeaponVfxActor.h"
#include "ActorRenderContext.h"
#include "GameplayActors.h"
#include "SceneGraph.h"
#include "RenderDiagnostics.h"
#include <algorithm>
#include <iostream>

WeaponVfxActor::WeaponVfxActor(int effect, int family, float duration, float rate)
    : Actor("WeaponVFX"), effectIndex(effect), family(family), duration(duration), rate(rate)
{
    layer = RenderLayer::Effect;
}

WeaponVfxActor* WeaponVfxActor::Spawn(SceneGraph& scene,
                                      int family,
                                      Art::WeaponEffectKind kind,
                                      FarmPoint position,
                                      FarmPoint direction,
                                      ActorId owner,
                                      int evolution,
                                      float delay,
                                      float rate)
{
    try
    {
        if (owner != NoActor)
        {
            auto* player = scene.Get<PlayerActor>(owner);
            if (!player || !player->weapon || player->health <= 0 || int(player->yokai) != family)
            {
                return nullptr;
            }
        }
        const auto& effect = Art::YokaiAssets::Shared().Effect(family, kind);
        if (kind == Art::WeaponEffectKind::Skill && delay < 0)
        {
            rate = family == 2 ? (4.f / 30) / .14f : 1;
            delay = .25f - effect.onset / rate;
        }
        auto active = scene.Query<WeaponVfxActor>();
        if (active.size() >= 64)
        {
            active.front()->Destroy();
        }
        auto& actor =
            scene.Spawn<WeaponVfxActor>(NoActor, effect.index, family, effect.duration, rate);
        actor.SetPosition(position);
        actor.direction = Unit(direction);
        actor.owner = owner;
        actor.evolution = std::clamp(evolution, 0, 3);
        actor.elapsed =
            kind == Art::WeaponEffectKind::Skill ? -delay * rate : effect.onset - delay * rate;
        RenderDiagnostics::Get().Event("weapon_vfx_spawn",
                                       effect.id + "; owner=" + std::to_string(owner)
                                           + "; delay=" + std::to_string(delay));
        return &actor;
    }
    catch (const std::exception& error)
    {
        static bool reported = false;
        if (!reported)
        {
            std::cerr << "[VFX] " << error.what() << '\n';
            RenderDiagnostics::Get().Event("weapon_vfx_error", error.what());
            reported = true;
        }
        return nullptr;
    }
}

void WeaponVfxActor::Update(float dt, ActorUpdateContext&)
{
    elapsed += std::max(0.f, dt) * rate;
    auto* player = owner ? Scene().Get<PlayerActor>(owner) : nullptr;
    if (elapsed >= duration
        || (owner
            && (!player || !player->weapon || player->health <= 0 || int(player->yokai) != family)))
    {
        Destroy();
    }
}

void WeaponVfxActor::Render(ActorRenderContext& renderer) const
{
    if (owner)
    {
        auto* player = Scene().Get<PlayerActor>(owner);
        if (!player || !player->weapon || player->health <= 0 || int(player->yokai) != family)
        {
            return;
        }
    }
    if (elapsed >= 0)
    {
        renderer.Draw(*this);
    }
}
