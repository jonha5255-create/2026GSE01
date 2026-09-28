#pragma once
#include "Actor.h"
#include "ActorRenderContext.h"
#include "GameTypes.h"
#include <deque>
#include <vector>

struct GameplayContext;
class SceneGraph;

enum class NpcJob
{
    Hunter,
    Raider,
    Merchant,
    Seeker,
    Guard,
    Recorder,
    Wraith
};
enum class NpcAction
{
    Rest,
    Hunt,
    Raid,
    Trade,
    Meditate,
    Patrol,
    Recover
};
enum class TownSite
{
    Market,
    Clinic,
    Archive,
    Home,
    School,
    Shrine
};

struct StoryProgress
{
    bool homeInvestigated = false, hasHarinStudentCard = false;
    bool metRecorder = false, mukContracted = false;
    int mukSync = 0, hunted = 0;
};

class TownActor : public Actor
{
  public:
    TownActor() : Actor("Town")
    {
    }
};

class TownSiteActor : public Actor
{
  public:
    explicit TownSiteActor(TownSite site) : Actor("TownSite"), site(site)
    {
    }

    void Render(ActorRenderContext& context) const override
    {
        context.Draw(*this);
    }

    TownSite site;
};

class NpcActor : public Actor
{
  public:
    NpcActor(std::string name, NpcJob job);
    void Update(float dt, ActorUpdateContext& context) override;

    void Render(ActorRenderContext& context) const override
    {
        context.Draw(*this);
    }

    int UpdateOrder() const override
    {
        return 35;
    }

    NpcJob job;
    NpcAction action = NpcAction::Rest;
    float health = 100, maximumHealth = 100;
    float wealthWeight = .6f, survivalWeight = .8f, insightWeight = .2f, raidWeight = .1f;
    float decision = 0, cooldown = 0, recovery = 0, actionTime = 0, repath = 0;
    int gold = 60, goods = 0, insight = 0, growth = 0, victories = 0;
    ActorId target = NoActor, aggressor = NoActor;
    bool playerHostile = false;
    bool offender = false;
    FarmPoint home, destination;
    std::vector<FarmPoint> path;
    size_t pathIndex = 0;
};

// Simulation services are separate from rendering and story progression.
// SceneGraph owns all placed entities; this object stores IDs and session state only.
class LivingWorld
{
  public:
    void Initialize(SceneGraph& graph, uint32_t seed);
    void Tick(float dt, GameplayContext& context);
    void UpdateNpc(NpcActor& npc, float dt, GameplayContext& context);
    bool Interact(GameplayContext& context);
    void PlayerTrade(bool buy, GameplayContext& context);
    void PlayerStrike(bool human, GameplayContext& context);
    void Damage(NpcActor& victim, float amount, NpcActor* attacker, GameplayContext& context);
    void TogglePK();
    std::string Objective() const;
    std::string Hint(const SceneGraph& graph) const;
    FarmPoint Center(const SceneGraph& graph) const;
    static const char* JobName(NpcJob job);
    static const char* ActionName(NpcAction action);
    static const char* SiteName(TownSite site);
    static NpcAction Choose(const NpcActor& npc);
    static bool Transfer(NpcActor& seller, NpcActor& buyer, int price);
    static bool SelfTest();

    ActorId town = NoActor;
    StoryProgress story;
    bool enabled = false, pk = false, panel = true;
    int gold = 80, goods = 0, notoriety = 0, trades = 0, raids = 0, hunts = 0;
    float clock = 0;
    std::string dialogue = "백연: 이곳은 잔명 마을. 사라진 이름을 기억하는 사람들이 모였어요.";
    std::deque<std::string> events;

  private:
    void Note(const std::string& text, const std::string& kind = "world_event");
    void Decide(NpcActor& npc, GameplayContext& context);
    void MoveTo(NpcActor& npc, FarmPoint localGoal, float dt, GameplayContext& context);
    void Trade(NpcActor& npc, GameplayContext& context);
    void Defeat(NpcActor& victim, NpcActor* attacker, GameplayContext& context);
    void AdvanceStory(TownSite site);
    uint32_t Random();
    uint32_t random_ = 1;
    float summaryTimer_ = 0, strikeCooldown_ = 0;
};
