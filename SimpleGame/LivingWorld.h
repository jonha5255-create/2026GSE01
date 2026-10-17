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
    Shrine,
    Relay,
    Signal,
    Station
};

struct StoryProgress
{
    bool homeInvestigated = false, hasHarinStudentCard = false;
    bool metRecorder = false, mukContracted = false;
    int mukSync = 0, hunted = 0;
    bool reported = false, relayAligned = false, signalDecoded = false, gateOpened = false;
    bool guardianDefeated = false, echoRecovered = false, weaponAwakened = false,
         caseClosed = false;
    int suppliesDelivered = 0;
};

class TownActor : public Actor
{
  public:
    explicit TownActor(std::string name = "잔명 마을") : Actor(std::move(name))
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
    bool storyGuardian = false;
    bool wilderness = false;
    bool fieldBoss = false;
    int monsterForm = 0; // 0: horned wraith, 1: carrion hound, 2: stone brute.
    float gaitPhase = 0, moveBlend = 0;
    FarmPoint moveFacing{1, 0};
    float attackTime = -1, attackDamage = 0;
    ActorId attackVictim = NoActor;
    FarmPoint attackDirection{1, 0};
    bool attackPlayer = false, attackHit = false;
    bool settlementResident = false;
    FarmPoint wanderGoal;
    float wanderTimer = 0;
    FarmPoint home, destination;
    std::vector<FarmPoint> path;
    size_t pathIndex = 0;
};

struct WorldIncident
{
    uint64_t id = 0, sourceId = 0;
    float time = 0;
    std::string type, text, actorName, victimName, witnessName;
    ActorId actor = NoActor, victim = NoActor, witness = NoActor, village = NoActor;
    bool rumorIssued = false;
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
    void PlayerStrike(bool human,
                      GameplayContext& context,
                      ActorId selected = NoActor,
                      bool basicAttack = false);
    void Damage(NpcActor& victim, float amount, NpcActor* attacker, GameplayContext& context);
    void TogglePK();
    std::string Objective() const;
    int QuestStep() const;
    bool CombatQuest() const;
    std::string QuestInstruction() const;
    ActorId QuestTarget(const SceneGraph& graph) const;
    std::string Hint(const SceneGraph& graph) const;
    FarmPoint Center(const SceneGraph& graph) const;
    static const char* JobName(NpcJob job);
    static const char* ActionName(NpcAction action);
    static const char* SiteName(TownSite site);
    static NpcAction Choose(const NpcActor& npc);
    static bool Transfer(NpcActor& seller, NpcActor& buyer, int price);
    static bool SelfTest();

    ActorId town = NoActor;
    ActorId guardian = NoActor;
    StoryProgress story;
    bool enabled = false, pk = false, panel = false;
    int gold = 80, goods = 0, notoriety = 0, trades = 0, raids = 0, hunts = 0;
    float clock = 0;
    std::string dialogue = "백연: 이곳은 잔명 마을. 사라진 이름을 기억하는 사람들이 모였어요.";
    std::deque<std::string> events;
    std::deque<WorldIncident> incidents;
    uint64_t murders = 0, rumors = 0;

  private:
    void BeginMonsterAttack(
        NpcActor& npc, ActorId target, bool player, float damage, FarmPoint position);
    void Note(const std::string& text, const std::string& kind = "world_event");
    void RecordIncident(WorldIncident incident);
    uint64_t nextIncident_ = 1;
    void Decide(NpcActor& npc, GameplayContext& context);
    void MoveTo(NpcActor& npc, FarmPoint localGoal, float dt, GameplayContext& context);
    void Trade(NpcActor& npc, GameplayContext& context);
    void Defeat(NpcActor& victim, NpcActor* attacker, GameplayContext& context);
    void AdvanceStory(TownSite site);
    uint32_t Random();
    uint32_t random_ = 1;
    float summaryTimer_ = 0, strikeCooldown_ = 0;
};
