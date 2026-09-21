#pragma once
class PlayerActor;
class WeaponActor;
class RangeActor;
class EnemyActor;
class ProjectileActor;
class LootActor;
class EffectActor;
class PortalActor;
class TelegraphActor;
class FloorActor;
class BuildingActor;
class LampActor;
class RainActor;
class HudActor;

// Render visitor keeps actors/simulation independent of OpenGL and the camera.
class ActorRenderContext
{
  public:
    virtual ~ActorRenderContext() = default;
    virtual void Draw(const PlayerActor&) = 0;
    virtual void Draw(const WeaponActor&) = 0;
    virtual void Draw(const RangeActor&) = 0;
    virtual void Draw(const EnemyActor&) = 0;
    virtual void Draw(const ProjectileActor&) = 0;
    virtual void Draw(const LootActor&) = 0;
    virtual void Draw(const EffectActor&) = 0;
    virtual void Draw(const PortalActor&) = 0;
    virtual void Draw(const TelegraphActor&) = 0;
    virtual void Draw(const FloorActor&) = 0;
    virtual void Draw(const BuildingActor&) = 0;
    virtual void Draw(const LampActor&) = 0;
    virtual void Draw(const RainActor&) = 0;
    virtual void Draw(const HudActor&) = 0;
};
