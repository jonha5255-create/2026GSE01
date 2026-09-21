#include "stdafx.h"
#include "LevelOne.h"
#include <iostream>

bool LevelOne::SelfTest()
{
    bool ok = SceneGraph::SelfTest();
    auto check = [&](bool pass, const char* name)
    {
        std::cout << (pass ? "PASS " : "FAIL ") << name << '\n';
        ok = ok && pass;
    };
    Walkable open = [](float, float, float)
    {
        return true;
    };
    Walkable wall = [](float x, float, float radius)
    {
        return x < 60 - radius || x > 90 + radius;
    };
    LevelOne a;
    a.spawnTimer_ = 999;
    auto& enemy = a.SpawnEnemy({150, 0});
    enemy.speed = 0;
    enemy.health = enemy.maximumHealth = 200;
    ActorId enemyId = enemy.Id();
    auto context = a.Context(open);
    check(a.Weapon().Shoot(context), "auto aim acquires a visible enemy");
    check(!a.Weapon().Shoot(context), "projectile cooldown cannot be bypassed");
    for (int i = 0; i < 30; ++i)
    {
        a.Update(.02f, open);
    }
    check(a.Graph().Get<EnemyActor>(enemyId)->health < 200, "ranged projectile deals damage");
    enemy.SetPosition({a.Range() + 1, 0});
    a.Weapon().shotTimer_ = 0;
    check(!a.Weapon().Shoot(context), "auto aim respects range");
    enemy.SetPosition({150, 0});
    auto wallContext = a.Context(wall);
    check(!a.Weapon().Shoot(wallContext), "auto aim cannot see through a wall");
    float damage = a.Damage(), cooldown = a.Cooldown(), health = a.MaximumHealth();
    a.Player().AddExperience(200, context);
    check(a.Player().level > 1 && a.Damage() > damage && a.Cooldown() < cooldown
              && a.MaximumHealth() > health,
          "XP improves damage cooldown and max health");
    a.Player().health = 10;
    a.Player().Collect(LootKind::Heal, 12, context);
    check(a.Player().health == 45, "healing drop restores health");
    a.Player().Collect(LootKind::Upgrade, 12, context);
    check(a.Player().weaponRank == 1, "upgrade drop improves weapon rank");
    a.Player().Collect(LootKind::Magnet, 12, context);
    check(a.Player().PickupRange() == 440, "magnet pickup extends collection range");
    context.Drop({220, 0}, LootKind::SoulStone, 12);
    int picked = a.pickups;
    for (int i = 0; i < 40; ++i)
    {
        a.Update(.02f, open);
    }
    check(a.pickups > picked && a.Loot().empty(), "magnetic attraction collects soul stones");
    auto& blockedLoot = wallContext.Drop({150, 0}, LootKind::Upgrade);
    ActorId lootId = blockedLoot.Id();
    for (int i = 0; i < 50; ++i)
    {
        a.Update(.02f, wall);
    }
    check(a.Graph().Get<LootActor>(lootId) && blockedLoot.Position().x == 150,
          "loot cannot pass through walls");

    LevelOne drops;
    auto dropContext = drops.Context(open);
    for (int i = 0; i < 3; ++i)
    {
        auto& victim = drops.SpawnEnemy({150, 0});
        victim.TakeDamage(1000, dropContext);
    }
    bool kinds[4] = {};
    for (auto* item : drops.Loot())
    {
        kinds[int(item->kind)] = true;
    }
    check(kinds[0] && kinds[1] && kinds[2] && kinds[3], "all four drop types are obtainable");
    check(drops.Enemies().empty() && drops.kills == 3,
          "dead actors immediately leave gameplay queries");
    drops.Graph().CollectDestroyed();

    LevelOne boss;
    boss.state = RunState::BossReady;
    boss.Portal().SetPosition({200, 0});
    boss.Interact(open);
    check(boss.state == RunState::BossReady, "boss summon requires proximity");
    boss.Portal().SetPosition({70, 0});
    boss.Interact(open);
    check(boss.state == RunState::BossFight && boss.Enemies().size() == 1
              && boss.Enemies()[0]->boss,
          "boss encounter starts once");
    boss.Enemies()[0]->speed = 0;
    boss.Enemies()[0]->attackTimer = 999;
    boss.Player().level = 10;
    boss.Player().weaponRank = 8;
    boss.Player().weapon = true;
    for (int i = 0; i < 1500 && boss.state == RunState::BossFight; ++i)
    {
        boss.Update(.02f, open);
    }
    check(boss.state == RunState::Won && boss.Enemies().empty(),
          "boss can be defeated through normal projectiles");

    LevelOne death;
    auto deathContext = death.Context(open);
    death.Player().DamagePlayer(200, deathContext);
    death.Update(.05f, open);
    check(death.state == RunState::Lost && death.Player().health == 0 && death.Enemies().empty(),
          "death stops encounter simulation");
    LevelOne cap;
    auto capContext = cap.Context(open);
    cap.Player().AddExperience(999999, capContext);
    for (int i = 0; i < 20; ++i)
    {
        cap.Player().Collect(LootKind::Upgrade, 12, capContext);
    }
    check(cap.Player().level == 10 && cap.Player().weaponRank == 8 && cap.Cooldown() >= .24f,
          "progression has safe caps");
    LevelOne unlock;
    unlock.kills = 12;
    unlock.Player().level = 3;
    unlock.Player().weaponRank = 1;
    unlock.pickups = 4;
    unlock.Update(.02f, open);
    check(unlock.state == RunState::BossReady && unlock.Portal().visible,
          "farming objectives unlock boss portal");
    LevelOne projectile;
    projectile.spawnTimer_ = 999;
    projectile.Graph().Spawn<ProjectileActor>(NoActor, FarmPoint{560, 0}, 100.f, 14.f);
    for (int i = 0; i < 30; ++i)
    {
        projectile.Update(.02f, open);
    }
    check(projectile.Bullets().empty(), "projectile expires at maximum travel distance");
    projectile.Graph().Spawn<ProjectileActor>(NoActor, FarmPoint{560, 0}, 320.f, 14.f);
    for (int i = 0; i < 10; ++i)
    {
        projectile.Update(.02f, wall);
    }
    check(projectile.Bullets().empty(), "in-flight projectiles collide with walls");
    LevelOne immunity;
    auto immunityContext = immunity.Context(open);
    immunity.Player().DamagePlayer(9, immunityContext);
    immunity.Player().DamagePlayer(9, immunityContext);
    check(immunity.Player().health == 91, "hit immunity prevents stacked contact damage");

    LevelOne warning;
    warning.spawnTimer_ = 999;
    auto& guard = warning.SpawnEnemy({150, 0}, true);
    guard.speed = 0;
    guard.attackTimer = 0;
    warning.Update(.02f, open);
    auto warnings = warning.Graph().Query<TelegraphActor>();
    check(warnings.size() == 1 && warnings[0]->Parent() == guard.Id(),
          "boss owns its telegraph child");
    warning.Shift(40, 10);
    warnings = warning.Graph().Query<TelegraphActor>();
    check(warnings.size() == 1 && warnings[0]->Position().x == -40
              && warnings[0]->Position().y == -10,
          "world rebasing shifts hierarchy exactly once and keeps telegraph anchored");
    guard.Destroy();
    warning.Graph().CollectDestroyed();
    check(warning.Graph().Query<TelegraphActor>().empty(),
          "boss destruction removes its telegraph");
    LevelOne disabled;
    disabled.spawnTimer_ = 999;
    disabled.Player().weapon = true;
    auto& visibleEnemy = disabled.SpawnEnemy({150, 0});
    visibleEnemy.speed = 0;
    disabled.Weapon().enabled = false;
    disabled.Update(.05f, open);
    check(disabled.Bullets().empty(), "disabled weapon actor stops automatic fire");
    disabled.Weapon().enabled = true;
    disabled.Update(.05f, open);
    check(!disabled.Bullets().empty(), "re-enabled weapon actor resumes automatic fire");
    LevelOne lifetime;
    auto temporaryContext = lifetime.Context(
        [](float, float, float)
        {
            return true;
        });
    check(temporaryContext.walkable(0, 0, 1),
          "gameplay context owns temporary collision callbacks");
    return ok;
}
