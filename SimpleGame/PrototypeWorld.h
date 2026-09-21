#pragma once
#include "PrototypeRenderer.h"
#include "LevelOne.h"
#include "WorldActors.h"
#include <cstdint>
#include <map>
#include <utility>

class PrototypeWorld : private ActorRenderContext
{
  public:
    explicit PrototypeWorld(uint32_t seed = 2026);
    void Update(float dt);
    void Key(unsigned char key, bool down);
    void Draw(PrototypeRenderer& r);
    void DrawUI(PrototypeRenderer& r);
    static bool SelfTest();
    void PreviewBoss();

  private:
    using Coord = std::pair<int64_t, int64_t>;
    static CityChunk Generate(Coord coord, uint32_t seed = 2026);
    void Stream();
    bool Blocked(const Location& p, float radius = 12) const;
    bool Walkable(float x, float y, float radius) const;
    void Move(float dx, float dy);
    Point Project(float x, float y, float z = 0) const;
    void Floor(PrototypeRenderer& r, float x, float y, const CityChunk& chunk);
    void FloorGeometry(PrototypeRenderer& r, float x, float y, const CityChunk& chunk);
    void LocalMesh(PrototypeRenderer& r,
                   const std::string& key,
                   float x,
                   float y,
                   float opacity,
                   const std::function<void()>& build);
    void BuildingGeometry(PrototypeRenderer& r,
                          float x,
                          float y,
                          float w,
                          float d,
                          float h,
                          uint32_t seed,
                          float alpha,
                          bool haunted);
    void Building(
        PrototypeRenderer& r, float x, float y, float w, float d, float height, uint32_t seed);
    void Player(PrototypeRenderer& r, const PlayerActor& actor);
    void Weapon(PrototypeRenderer& r, const WeaponActor& actor);
    void Rift(PrototypeRenderer& r, float x, float y);
    void Hud(PrototypeRenderer& r);
    void Enemy(PrototypeRenderer& r, const EnemyActor& enemy);
    void Loot(PrototypeRenderer& r, const LootActor& item);
    void Ring(PrototypeRenderer& r, float x, float y, float radius, Ink color, float width = 1);
    void Draw(const PlayerActor& actor) override;
    void Draw(const WeaponActor& actor) override;
    void Draw(const RangeActor& actor) override;
    void Draw(const EnemyActor& actor) override;
    void Draw(const ProjectileActor& actor) override;
    void Draw(const LootActor& actor) override;
    void Draw(const EffectActor& actor) override;
    void Draw(const PortalActor& actor) override;
    void Draw(const TelegraphActor& actor) override;
    void Draw(const FloorActor& actor) override;
    void Draw(const BuildingActor& actor) override;
    void Draw(const LampActor& actor) override;
    void Draw(const RainActor& actor) override;
    void Draw(const HudActor& actor) override;
    PrototypeRenderer* activeRenderer_ = nullptr;
    std::map<Coord, ActorId> chunkActors_;
    std::map<Coord, CityChunk> chunks_;
    Location player_;
    uint32_t seed_ = 2026;
    LevelOne level_;
    bool keys_[256] = {};
    bool distortion_ = true, memory_ = false, debug_ = false;
    float time_ = 0, zoom_ = 1;
    Point center_{640, 400}, cameraLag_{0, 0};
};
