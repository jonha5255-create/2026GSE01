#pragma once
#include "SceneGraph.h"
#include "GameTypes.h"
#include "Navigation.h"
class PlayerActor;
class EnemyActor;
class LootActor;
class EffectActor;
class LivingWorld;

// Per-tick services and encounter callbacks, not an owning object registry.
struct GameplayContext : ActorUpdateContext
{
    GameplayContext(SceneGraph& graph,
                    PlayerActor& hero,
                    Navigation& navigation,
                    const Walkable& collision,
                    RunState& run,
                    int& collected)
        : scene(graph), player(hero), navigation(navigation), walkable(collision), state(run),
          pickups(collected)
    {
    }

    SceneGraph& scene;
    LivingWorld* society = nullptr;
    PlayerActor& player;
    Navigation& navigation;
    Walkable walkable;
    RunState& state;
    int& pickups;
    std::function<void(const EnemyActor&)> defeated;
    std::function<bool(FarmPoint&, float, float)> findSpawn;
    std::function<void(float, float)> movePlayer;
    EffectActor& Effect(FarmPoint position, const std::string& text, float life);
    LootActor& Drop(FarmPoint position, LootKind kind, int amount = 12);
};
