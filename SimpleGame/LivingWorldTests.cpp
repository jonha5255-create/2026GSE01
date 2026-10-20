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
    {
        LevelOne test(2026);
        auto& society = test.society;
        society.Initialize(test.Graph(), 2026);
        auto services = test.Context(clear);
        auto people = test.Graph().Query<NpcActor>();
        people[0]->local = {0, 0};
        people[1]->local = {5, 0};
        society.Damage(*people[0], 10000, nullptr, services);
        check(society.murders == 1 && society.incidents.front().victim == people[0]->Id(),
              "human lethal defeat records one murder with participants");
        society.Damage(*people[0], 10000, nullptr, services);
        check(society.murders == 1, "recovering victims do not duplicate murders");
        society.Tick(3.1f, services);
        check(society.rumors == 1 && society.incidents.front().type == "rumor"
                  && society.incidents.front().sourceId == 1,
              "living nearby witness reports a linked unverified rumor");
        society.Tick(4.f, services);
        check(society.rumors == 1, "rumor is generated once per witnessed incident");
        society.Damage(*people[11], 10000, nullptr, services);
        check(society.murders == 1, "monster hunts are not classified as murder");
        for (int i = 0; i < 140; ++i)
        {
            WorldIncident record;
            record.type = "rumor";
            record.text = "검증용 소문";
            society.RecordIncident(record);
        }
        check(society.incidents.size() == 128 && society.rumors == 141,
              "incident history is bounded while lifetime totals are preserved");
    }
    {
        LevelOne postal(2026);
        auto& sim = postal.society;
        sim.Initialize(postal.Graph(), 2026);
        auto context = postal.Context(clear);
        auto* source = sim.Village(postal.Graph(), 0);
        auto* target = sim.Village(postal.Graph(), 1);
        bool homes = true;
        for (auto* settlement : postal.Graph().Query<TownActor>())
        {
            int owned = 0;
            for (auto id : settlement->homes)
            {
                owned += id ? 1 : 0;
            }
            homes &= owned == 30;
        }
        check(homes, "each of seven villages starts with 30 owned homes and six vacant homes");
        auto* messenger = postal.Graph().Get<NpcActor>(source->homes[11]);
        messenger->letter = sim.MakeNews(*source, context);
        check(sim.BeginTravel(*messenger, *target, false, context) && !target->news[0].known,
              "dispatch does not instantly reveal another village's news");
        for (int frame = 0; frame < 2000 && sim.lettersDelivered == 0; ++frame)
        {
            sim.UpdateNpc(*messenger, .1f, context);
        }
        check(sim.lettersDelivered == 1 && target->news[0].known && messenger->travelTown == 0,
              "physical messenger arrival delivers dated news and starts return trip");
        for (auto* npc : postal.Graph().Query<NpcActor>())
        {
            npc->migrationCooldown = 1000;
        }
        auto* mover = postal.Graph().Get<NpcActor>(target->homes[1]);
        mover->wealthWeight = 1;
        mover->survivalWeight = mover->insightWeight = mover->raidWeight = 0;
        mover->migrationCooldown = 0;
        target->news[0].wealth = 10;
        target->migrationTimer = 0;
        sim.UpdateSettlements(.1f, context);
        check(mover->migrating && mover->travelTown == 0 && mover->Parent() == target->Id(),
              "delivered prosperity report triggers a wealth-seeking resident's physical move");
        int oldSlot = mover->homeSlot;
        for (int frame = 0; frame < 2500 && sim.migrations == 0; ++frame)
        {
            sim.UpdateNpc(*mover, .1f, context);
        }
        check(sim.migrations == 1 && mover->Parent() == source->Id()
                  && target->homes[oldSlot] == NoActor
                  && source->homes[mover->homeSlot] == mover->Id(),
              "migration arrival transfers village membership and house ownership without "
              "duplicating NPC");
        check(sim.BeginTravel(*mover, *target, true, context),
              "vacant home can be reserved for migration");
        int reserved = mover->travelSlot;
        mover->health = 0;
        sim.UpdateNpc(*mover, .1f, context);
        check(target->homes[reserved] == NoActor && mover->travelTown < 0,
              "defeated migrant releases reserved destination house");
    }
    LevelOne level(2026);
    {
        LevelOne fight(2026);
        auto& society = fight.society;
        society.Initialize(fight.Graph(), 2026);
        auto services = fight.Context(clear);
        auto& attacker =
            fight.Graph().Spawn<NpcActor>(society.town, "공격 검증 석괴", NpcJob::Wraith);
        auto& victim = fight.Graph().Spawn<NpcActor>(society.town, "검증 대상", NpcJob::Hunter);
        attacker.monsterForm = 2;
        attacker.local = {0, 0};
        victim.local = {20, 0};
        society.BeginMonsterAttack(attacker, victim.Id(), false, 11, victim.Position());
        society.UpdateNpc(attacker, .2f, services);
        check(victim.health == 100, "monster windup does not cause immediate damage");
        society.UpdateNpc(attacker, .36f, services);
        society.UpdateNpc(attacker, .1f, services);
        check(victim.health == 89, "monster attack damages once at animation impact");
        society.BeginMonsterAttack(attacker, victim.Id(), false, 11, victim.Position());
        victim.local = {180, 0};
        society.UpdateNpc(attacker, .6f, services);
        check(victim.health == 89, "moving out of reach avoids a telegraphed monster attack");
        attacker.health = 0;
        society.UpdateNpc(attacker, .1f, services);
        check(attacker.attackTime < 0, "defeated monster cancels its attack animation");
    }
    {
        LevelOne encounter(2026);
        auto& society = encounter.society;
        society.Initialize(encounter.Graph(), 2026);
        auto services = encounter.Context(clear);
        auto& boss =
            encounter.Graph().Spawn<NpcActor>(society.town, "폐허의 포식자", NpcJob::Wraith);
        boss.fieldBoss = true;
        boss.health = boss.maximumHealth = 360;
        boss.gold = 65;
        boss.goods = 5;
        int money = society.gold, cargo = society.goods;
        society.Damage(boss, 1000, nullptr, services);
        check(boss.health == 0 && boss.recovery == 90 && society.gold == money + 65
                  && society.goods == cargo + 5,
              "field boss grants enhanced loot and uses a longer respawn timer");
        society.UpdateNpc(boss, 90, services);
        check(boss.health == 360 && boss.gold == 65 && boss.goods == 5,
              "field boss respawns with full health and renewed boss rewards");
    }
    auto& sim = level.society;
    sim.Initialize(level.Graph(), 2026);
    auto context = level.Context(clear);
    auto citizens = level.Graph().Query<NpcActor>();
    check(citizens.size() == 213 && level.Graph().Query<TownSiteActor>().size() == 21
              && level.Graph().Query<TownActor>().size() == 7,
          "living world spawns bounded citizens and town facilities as actors");
    {
        LevelOne tutorial(2026);
        auto& quests = tutorial.society;
        quests.Initialize(tutorial.Graph(), 2026);
        auto tutorialContext = tutorial.Context(clear);
        auto* home = tutorial.Graph().Get<TownSiteActor>(quests.QuestTarget(tutorial.Graph()));
        check(home && home->site == TownSite::Home, "quest guidance starts at the home");
        for (auto* site : tutorial.Graph().Query<TownSiteActor>())
        {
            site->local = site == home ? FarmPoint{50, 0} : FarmPoint{10, 0};
        }
        quests.Interact(tutorialContext);
        auto* school = tutorial.Graph().Get<TownSiteActor>(quests.QuestTarget(tutorial.Graph()));
        check(quests.story.homeInvestigated && school && school->site == TownSite::School,
              "E prioritizes reachable quest facility over closer unrelated facilities");
        quests.AdvanceStory(TownSite::School);
        quests.AdvanceStory(TownSite::Archive);
        quests.AdvanceStory(TownSite::Shrine);
        auto* monster = tutorial.Graph().Get<NpcActor>(quests.QuestTarget(tutorial.Graph()));
        check(monster && monster->job == NpcJob::Wraith && monster->health > 0,
              "hunting guidance targets a living monster instead of the shrine");
        quests.story.hunted = 3;
        check(quests.QuestStep() == 6 && quests.QuestTarget(tutorial.Graph()) != NoActor,
              "initial hunt continues into the expedition quest");
        quests.goods = 3;
        quests.Interact(tutorialContext);
        check(quests.story.reported && quests.QuestStep() == 7,
              "hunt report unlocks market preparation");
        quests.PlayerTrade(false, tutorialContext);
        quests.PlayerTrade(false, tutorialContext);
        check(quests.story.suppliesDelivered == 2 && quests.QuestStep() == 8,
              "two actual player sales unlock the expedition");
        quests.AdvanceStory(TownSite::Station);
        check(!quests.story.gateOpened, "station encounter requires decoded route evidence");
        quests.Interact(tutorialContext);
        quests.Interact(tutorialContext);
        quests.Interact(tutorialContext);
        auto* guardian = tutorial.Graph().Get<NpcActor>(quests.guardian);
        check(quests.QuestStep() == 11 && guardian && guardian->storyGuardian,
              "relay signal and station interactions spawn one quest guardian");
        quests.Interact(tutorialContext);
        check(tutorial.Graph().Query<NpcActor>().size() == 214 && !quests.story.echoRecovered,
              "station cannot duplicate guardians or bypass combat");
        if (guardian)
        {
            quests.Damage(*guardian, 1000, nullptr, tutorialContext);
        }
        quests.Interact(tutorialContext);
        int rank = tutorial.Player().weaponRank;
        quests.Interact(tutorialContext);
        check(quests.story.echoRecovered && quests.story.weaponAwakened
                  && tutorial.Player().weaponRank == rank + 1,
              "recovering Harin memory permanently strengthens the companion");
        quests.Interact(tutorialContext);
        int reward = quests.gold;
        quests.Interact(tutorialContext);
        check(quests.story.caseClosed && quests.gold == reward
                  && quests.QuestTarget(tutorial.Graph()) == NoActor,
              "expedition completion grants rewards once and clears navigation");
    }

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

    auto* prey = citizens[13];
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

    LevelOne arsenal(2026);
    arsenal.society.Initialize(arsenal.Graph(), 2026);
    auto combat = arsenal.Context(clear);
    auto& hero = arsenal.Player();
    auto& weapon = arsenal.Weapon();
    auto* dummy = arsenal.Graph().Query<NpcActor>()[13];
    for (auto* npc : arsenal.Graph().Query<NpcActor>())
    {
        npc->local = {1000, 1000};
    }
    dummy->local = {0, 50};
    dummy->health = dummy->maximumHealth = 1000;
    auto elapse = [&](float dt)
    {
        arsenal.society.Tick(dt, combat);
        weapon.Update(dt, combat);
    };
    hero.SelectYokai(YokaiForm::Sword);
    check(!hero.weapon && hero.yokai == YokaiForm::Sword,
          "sword selection always returns to companion form");
    hero.weapon = true;
    arsenal.society.PlayerStrike(false, combat, dummy->Id(), true);
    check(dummy->health == 976 && arsenal.Bullets().empty(),
          "sword performs melee without ranged auto fire");
    check(weapon.swingTime > 0 && weapon.swingDuration == .42f,
          "successful sword strike starts timed animation");
    int swordSide = weapon.swingSide;
    weapon.Update(.1f, combat);
    check(weapon.SwingProgress() > 0 && weapon.SwingLunge() > 0,
          "attack pose advances with simulation time");
    hero.SelectYokai(YokaiForm::Claw);
    check(!hero.weapon, "switching an equipped weapon restores yokai companion state");
    hero.weapon = true;
    arsenal.society.PlayerStrike(false, combat, dummy->Id(), true);
    check(dummy->health == 976, "switch cannot reset attack cooldown");
    elapse(.51f);
    arsenal.society.PlayerStrike(false, combat, dummy->Id(), true);
    check(weapon.swingDuration == .18f && weapon.swingSide != swordSide,
          "claw uses a shorter alternating attack animation");
    elapse(.21f);
    arsenal.society.PlayerStrike(false, combat, dummy->Id(), true);
    check(dummy->health == 954 && hero.StrikeRange() == 65,
          "claw has faster repeat attacks and shorter reach");
    hero.SelectYokai(YokaiForm::Ranged);
    elapse(.21f);
    check(weapon.swingTime == 0, "companion switching clears melee pose");
    check(!hero.weapon && arsenal.Bullets().empty(), "ranged companion does not auto fire");
    dummy->local = {240, 0};
    hero.weapon = true;
    arsenal.society.PlayerStrike(false, combat, dummy->Id(), true);
    check(arsenal.Bullets().size() == 1 && dummy->health == 954,
          "ranged click launches projectile instead of instant melee damage");
    for (int step = 0; step < 10; ++step)
    {
        for (auto* bullet : arsenal.Bullets())
        {
            bullet->Update(.05f, combat);
        }
        arsenal.Graph().CollectDestroyed();
    }
    check(dummy->health < 954, "selected ranged projectile applies damage on impact");
    hero.SelectYokai(YokaiForm::Ranged);
    elapse(1);
    hero.weapon = true;
    dummy->local = {1000, 1000};
    auto people = arsenal.Graph().Query<NpcActor>();
    auto* selectedHuman = people[0];
    auto* bystander = people[1];
    selectedHuman->local = {180, 0};
    bystander->local = {90, 0};
    float selectedHealth = selectedHuman->health, bystanderHealth = bystander->health;
    check(!weapon.ShootAt(*selectedHuman, combat), "ranged human attack requires PK consent");
    arsenal.society.TogglePK();
    check(weapon.ShootAt(*selectedHuman, combat), "PK permits selected human projectile");
    for (int step = 0; step < 10; ++step)
    {
        for (auto* bullet : arsenal.Bullets())
        {
            bullet->Update(.05f, combat);
        }
        arsenal.Graph().CollectDestroyed();
    }
    check(selectedHuman->health < selectedHealth && bystander->health == bystanderHealth
              && arsenal.society.notoriety > 0,
          "ranged PK hits authorized target without harming human bystanders");

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
    check(ecosystem.Graph().Query<NpcActor>().size() == 213 && living.events.size() <= 5,
          "simulation keeps population and UI event history bounded");
    return ok;
}
