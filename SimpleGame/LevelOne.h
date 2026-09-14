#pragma once
#include <functional>
#include <string>
#include <vector>
#include <cstdint>

struct FarmPoint
{
    float x = 0, y = 0;
};

enum class LootKind
{
    SoulStone,
    Upgrade,
    Heal,
    Magnet
};
enum class RunState
{
    Farming,
    BossReady,
    BossFight,
    Won,
    Lost
};

struct FarmEnemy
{
    int id = 0;
    FarmPoint position;
    float health = 36, maximumHealth = 36, speed = 54, radius = 12;
    float attackTimer = 1, windup = 0, hitFlash = 0;
    FarmPoint attackPosition;
    bool boss = false;
};

struct FarmBullet
{
    FarmPoint position, velocity;
    float traveled = 0, range = 320, damage = 14;
};

struct FarmLoot
{
    FarmPoint position;
    LootKind kind = LootKind::SoulStone;
    int amount = 12;
    float age = 0;
};

struct FarmEffect
{
    FarmPoint position;
    std::string text;
    float life = 1;
};

// Simulation positions are relative to the player. Shift() rebases every entity
// after player movement, so long world travel never accumulates large floats.
class LevelOne
{
  public:
    using Walkable = std::function<bool(float, float, float)>;
    explicit LevelOne(uint32_t seed = 2026);
    void Update(float dt, bool weapon, const Walkable& walkable);
    void Shift(float dx, float dy);
    void Interact(const Walkable& walkable);

    void ToggleRange()
    {
        showRange = !showRange;
    }

    float Damage() const;
    float Cooldown() const;
    float MaximumHealth() const;
    float Range() const;
    float PickupRange() const;
    float MoveSpeed() const;
    int ExperienceRequired() const;
    std::string Objective() const;
    std::string Dialogue(bool weapon) const;
    static bool SelfTest();

    const std::vector<FarmEnemy>& Enemies() const
    {
        return enemies_;
    }

    const std::vector<FarmBullet>& Bullets() const
    {
        return bullets_;
    }

    const std::vector<FarmLoot>& Loot() const
    {
        return loot_;
    }

    const std::vector<FarmEffect>& Effects() const
    {
        return effects_;
    }

    RunState state = RunState::Farming;
    int level = 1, experience = 0, weaponRank = 0, kills = 0, pickups = 0;
    float health = 100, invulnerable = 0, magnetTime = 0, muzzleFlash = 0;
    bool showRange = true;
    FarmPoint aim{1, 0}, portal;

  private:
    uint32_t Random();
    bool Sight(FarmPoint a, FarmPoint b, const Walkable& walkable, float radius = 3) const;
    bool FindSpawn(FarmPoint& out, const Walkable& walkable, float minDistance, float maxDistance);
    void Navigate(const Walkable& walkable);
    FarmPoint ChaseDirection(FarmPoint position, const Walkable& walkable) const;
    bool Shoot(const Walkable& walkable);
    void DamagePlayer(float damage);
    void AddExperience(int amount);
    void Collect(const FarmLoot& item);
    void Defeat(const FarmEnemy& enemy);
    void UpdateLoot(float dt, const Walkable& walkable);
    std::vector<FarmEnemy> enemies_;
    std::vector<FarmBullet> bullets_;
    std::vector<FarmLoot> loot_;
    std::vector<FarmEffect> effects_;
    uint32_t random_;
    int nextId_ = 1;
    float spawnTimer_ = 1.5f, shotTimer_ = 0, navigationTimer_ = 0, quietTime_ = 0;
    static constexpr int NavWidth = 65;
    static constexpr float NavCell = 20;
    std::vector<int> navigation_;
};
