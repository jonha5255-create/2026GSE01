#include "stdafx.h"
#include "LivingWorld.h"
#include "LevelOne.h"
#include <iostream>
#include <cmath>

bool LivingWorld::SelfTest()
{
    bool ok = true;
    auto check = [&](bool pass, const char* name)
    {
        std::cout << (pass ? "PASS " : "FAIL ") << name << '\n';
        ok = ok && pass;
    };
    const Walkable clear = [](float, float, float)
    {
        return true;
    };
    LevelOne level(2026);
    auto& sim = level.society;
    sim.Initialize(level.Graph(), 2026);
    auto context = level.Context(clear);
    auto citizens = level.Graph().Query<NpcActor>();
    check(citizens.size() == 14 && level.Graph().Query<TownSiteActor>().size() == 6,
          "living world spawns bounded citizens and town facilities as actors");

    NpcActor merchant("seller", NpcJob::Merchant), hunter("buyer", NpcJob::Hunter);
    merchant.goods = 2;
    merchant.gold = 20;
    hunter.gold = 30;
    check(Transfer(merchant, hunter, 8) && merchant.gold + hunter.gold == 50
              && merchant.goods + hunter.goods == 2,
          "trade conserves money and transfers inventory");
    hunter.gold = 0;
    check(!Transfer(merchant, hunter, 8) && !Transfer(merchant, merchant, 8),
          "trade rejects insufficient funds and self trade");
    hunter.raidWeight = 1;
    hunter.wealthWeight = 1;
    hunter.goods = 0;
    check(Choose(hunter) == NpcAction::Raid, "high raid weight selects predation");
    hunter.raidWeight = 0;
    hunter.goods = 0;
    check(Choose(hunter) == NpcAction::Hunt, "low raid weight selects legitimate income");
    hunter.health = 10;
    check(Choose(hunter) == NpcAction::Rest, "survival overrides wealth goals");
    hunter.health = 100;
    hunter.gold = 50;
    hunter.insightWeight = 1;
    check(Choose(hunter) == NpcAction::Meditate, "insight utility selects meditation");

    sim.AdvanceStory(TownSite::School);
    check(!sim.story.hasHarinStudentCard, "student card requires home investigation");
    sim.AdvanceStory(TownSite::Shrine);
    check(!sim.story.mukContracted, "contract cannot skip recorder evidence");
    sim.AdvanceStory(TownSite::Home);
    sim.AdvanceStory(TownSite::School);
    sim.AdvanceStory(TownSite::Archive);
    sim.AdvanceStory(TownSite::Shrine);
    check(sim.story.hasHarinStudentCard && sim.story.metRecorder && sim.story.mukContracted,
          "MAP01 home card recorder contract progression");
    sim.AdvanceStory(TownSite::Shrine);
    check(sim.story.mukSync == 10, "repeat interactions do not duplicate contract rewards");

    auto* victim = citizens[0];
    victim->local = {25, 0};
    float health = victim->health;
    sim.PlayerStrike(true, context);
    check(victim->health == health, "human attacks require explicit PK opt-in");
    sim.TogglePK();
    sim.PlayerStrike(true, context);
    check(victim->health < health && sim.notoriety > 0 && victim->playerHostile,
          "PK damages citizen and triggers retaliation and wanted state");
    sim.TogglePK();
    check(sim.notoriety > 0, "turning off PK does not erase crime");
    victim->gold = 100;
    victim->goods = 2;
    int before = sim.gold;
    sim.Damage(*victim, 10000, nullptr, context);
    check(victim->recovery > 0 && sim.gold == before + 50 && victim->gold == 50 && sim.goods == 2,
          "player kill transfers half victim money and cargo exactly once");
    sim.Damage(*victim, 10000, nullptr, context);
    check(sim.gold == before + 50, "dead citizen cannot be looted twice");
    sim.UpdateNpc(*victim, 29, context);
    check(victim->health == victim->maximumHealth && victim->gold == 50,
          "citizen recovery preserves post-loot wealth");
    victim->enabled = false;
    float timer = victim->cooldown = 2;
    level.Graph().Update(.05f, context);
    check(victim->cooldown == timer, "disabled citizen is excluded from scene updates");
    victim->enabled = true;

    auto* prey = citizens.back();
    before = sim.story.hunted;
    sim.Damage(*prey, 10000, nullptr, context);
    check(sim.story.hunted == before + 1 && sim.story.mukSync == 13,
          "player hunt grants story progress and yokai synchronization");
    sim.UpdateNpc(*prey, 23, context);
    check(prey->health > 0 && prey->gold == 12 && prey->goods == 1,
          "renewable hunting encounter restores bounded resource reward");

    auto local = citizens[5]->local;
    auto world = citizens[5]->Position();
    level.Shift(640, 100);
    check(citizens[5]->local.x == local.x && citizens[5]->local.y == local.y
              && std::abs(citizens[5]->Position().x - (world.x - 640)) < .01f,
          "town hierarchy rebases once while local NPC paths remain stable");
    level.Shift(-640, -100);

    sim.goods = 1;
    before = sim.gold;
    sim.PlayerTrade(false, context);
    check(sim.goods == 0 && sim.gold == before + 8, "player sells at living town market");
    level.Player().health = 50;
    sim.PlayerTrade(true, context);
    check(level.Player().health == 90 && sim.gold == before - 7,
          "player buys merchant stock to recover health");

    // Real 180-second autonomous simulation, including activity while player is dead.
    LevelOne ecosystem(2026);
    ecosystem.society.Initialize(ecosystem.Graph(), 2026);
    auto& living = ecosystem.society;
    ecosystem.state = RunState::Lost;
    auto services = ecosystem.Context(clear);
    for (int frame = 0; frame < 3600; ++frame)
    {
        living.Tick(.05f, services);
        ecosystem.Graph().Update(.05f, services);
    }
    check(living.hunts > 0 && living.trades > 0 && living.raids > 0,
          "autonomous population completes hunting trade and predation without player");
    bool insight = false, valid = true;
    for (auto* npc : ecosystem.Graph().Query<NpcActor>())
    {
        insight |= npc->insight > 0;
        valid &= std::isfinite(npc->health) && npc->health >= 0 && npc->health <= npc->maximumHealth
                 && npc->gold >= 0 && npc->goods >= 0;
    }
    check(insight && valid, "continuous NPC updates grow insight and preserve valid resources");
    check(ecosystem.Graph().Query<NpcActor>().size() == 14 && living.events.size() <= 5,
          "simulation keeps population and UI event history bounded");
    return ok;
}
