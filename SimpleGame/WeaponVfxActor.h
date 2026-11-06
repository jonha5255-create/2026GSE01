#pragma once
#include "Actor.h"
#include "GameTypes.h"
#include "YokaiAssets.h"

class WeaponVfxActor : public Actor
{
  public:
    WeaponVfxActor(int effect, int family, float duration, float rate);
    static WeaponVfxActor* Spawn(SceneGraph& scene,
                                 int family,
                                 Art::WeaponEffectKind kind,
                                 FarmPoint position,
                                 FarmPoint direction,
                                 ActorId owner = NoActor,
                                 int evolution = 0,
                                 float delay = 0,
                                 float rate = 1);
    void Update(float dt, ActorUpdateContext& context) override;
    void Render(ActorRenderContext& renderer) const override;
    int effectIndex, family, evolution = 0;
    float elapsed = 0, duration, rate;
    FarmPoint direction{1, 0};
    ActorId owner = NoActor;
    // Visual-only socket snapshot. Repeated renders never advance its clock.
    mutable bool anchorResolved = false;
    mutable FarmPoint anchorOffset{};
    mutable float anchorHeight = 28;
};
