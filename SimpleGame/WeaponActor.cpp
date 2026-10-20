#include "stdafx.h"
#include "GameplayActors.h"
#include "GameplayContext.h"
#include "LivingWorld.h"
#include <algorithm>

bool WeaponActor::CastSkill(GameplayContext& context)
{
    auto* owner = Scene().Get<PlayerActor>(Parent());
    if (!owner || !owner->weapon || owner->health <= 0 || context.state == RunState::Lost
        || context.state == RunState::Won || skillTime >= 0)
    {
        return false;
    }
    auto& growth = owner->yokaiGrowth[int(owner->yokai)];
    if (growth.level < 3 || growth.skillCooldown > 0)
    {
        context.Effect(
            Position(), growth.level < 3 ? "요괴 LV.3에 스킬 해금" : "스킬 재사용 대기 중", 1);
        return false;
    }
    skillForm = owner->yokai;
    skillTime = 0;
    skillPulses = 0;
    skillDamage = (skillForm == YokaiForm::Claw ? 13.f : 32.f) + growth.level * 3.f;
    growth.skillCooldown = skillForm == YokaiForm::Claw ? 6.f : 8.f;
    float nearest = owner->Range();
    for (auto* npc : context.scene.Query<NpcActor>())
    {
        float distance = Length(Minus(npc->Position(), Position()));
        if (npc->job == NpcJob::Wraith && npc->health > 0 && distance < nearest
            && Navigation::Sight(Position(), npc->Position(), context.walkable))
        {
            nearest = distance;
            aim = Unit(Minus(npc->Position(), Position()));
        }
    }
    for (auto* enemy : context.scene.Query<EnemyActor>())
    {
        float distance = Length(Minus(enemy->Position(), Position()));
        if (enemy->health > 0 && distance < nearest
            && Navigation::Sight(Position(), enemy->Position(), context.walkable))
        {
            nearest = distance;
            aim = Unit(Minus(enemy->Position(), Position()));
        }
    }
    context.Effect(Position(), owner->SkillName(), 1.1f);
    return true;
}

void WeaponActor::UpdateSkill(float dt, GameplayContext& context)
{
    if (skillTime < 0)
    {
        return;
    }
    auto* owner = Scene().Get<PlayerActor>(Parent());
    if (!owner || !owner->weapon || owner->yokai != skillForm || owner->health <= 0
        || context.state == RunState::Lost || context.state == RunState::Won)
    {
        skillTime = -1;
        return;
    }
    skillTime += dt;
    int pulses = skillForm == YokaiForm::Claw ? 3 : 1;
    while (skillPulses < pulses && skillTime >= .25f + skillPulses * .14f)
    {
        ++skillPulses;
        if (skillForm == YokaiForm::Ranged)
        {
            for (int ray = -2; ray <= 2; ++ray)
            {
                float angle = ray * .16f;
                FarmPoint direction{aim.x * std::cos(angle) - aim.y * std::sin(angle),
                                    aim.x * std::sin(angle) + aim.y * std::cos(angle)};
                auto& bullet = context.scene.Spawn<ProjectileActor>(
                    NoActor,
                    FarmPoint{direction.x * 620, direction.y * 620},
                    owner->Range(),
                    skillDamage);
                bullet.SetPosition(Position());
            }
            muzzleFlash = .3f;
            owner->ApplyShotRecoil(aim, true);
        }
        else
        {
            float radius = skillForm == YokaiForm::Sword ? 145.f : 100.f;
            auto inReach = [&](Actor& target)
            {
                auto delta = Minus(target.Position(), Position());
                return Length(delta) <= radius
                       && (skillForm == YokaiForm::Sword
                           || delta.x * aim.x + delta.y * aim.y >= -10)
                       && Navigation::Sight(Position(), target.Position(), context.walkable);
            };
            for (auto* npc : context.scene.Query<NpcActor>())
            {
                if (npc->job == NpcJob::Wraith && npc->health > 0 && inReach(*npc)
                    && context.society)
                {
                    context.society->Damage(*npc, skillDamage, nullptr, context);
                }
            }
            for (auto* enemy : context.scene.Query<EnemyActor>())
            {
                if (enemy->health > 0 && inReach(*enemy))
                {
                    enemy->TakeDamage(skillDamage, context);
                }
            }
            StartSwing({Position().x + aim.x * 70, Position().y + aim.y * 70});
        }
    }
    if (skillTime >= 1.f)
    {
        skillTime = -1;
    }
}

void WeaponActor::StartSwing(FarmPoint target)
{
    auto* owner = Scene().Get<PlayerActor>(Parent());
    if (!owner || !owner->weapon || owner->yokai == YokaiForm::Ranged)
    {
        return;
    }
    auto direction = Unit(Minus(target, Position()));
    if (Length(direction) > 0)
    {
        aim = direction;
    }
    swingSide = -swingSide;
    swingDuration = owner->yokai == YokaiForm::Claw ? .18f : .42f;
    swingTime = swingDuration;
}

float WeaponActor::SwingProgress() const
{
    return swingTime > 0 ? 1 - swingTime / swingDuration : 0;
}

float WeaponActor::SwingLunge() const
{
    return swingTime > 0 ? std::sin(SwingProgress() * 3.14159265f) * 7 : 0;
}

void WeaponActor::Update(float dt, ActorUpdateContext& services)
{
    auto& context = dynamic_cast<GameplayContext&>(services);
    UpdateSkill(dt, context);
    shotTimer_ = std::max(0.f, shotTimer_ - dt);
    muzzleFlash = std::max(0.f, muzzleFlash - dt);
    auto* owner = Scene().Get<PlayerActor>(Parent());
    swingTime = std::max(0.f, swingTime - dt);
    if (!owner || !owner->weapon || owner->yokai == YokaiForm::Ranged)
    {
        swingTime = 0;
    }
    if (owner && owner->weapon && owner->yokai == YokaiForm::Ranged && skillTime < 0
        && context.state != RunState::Won && context.state != RunState::Lost)
    {
        Shoot(context);
    }
}

bool WeaponActor::Shoot(GameplayContext& context)
{
    auto* owner = Scene().Get<PlayerActor>(Parent());
    if (!owner || owner->yokai != YokaiForm::Ranged || shotTimer_ > 0)
    {
        return false;
    }
    Actor* target = nullptr;
    float nearest = owner->Range();
    for (auto* enemy : context.scene.Query<EnemyActor>())
    {
        FarmPoint delta = Minus(enemy->Position(), Position());
        float distance = Length(delta);
        if (enemy->health > 0 && distance <= nearest
            && Navigation::Sight(Position(), enemy->Position(), context.walkable))
        {
            target = enemy;
            nearest = distance;
        }
    }
    if (context.society)
    {
        for (auto* npc : context.scene.Query<NpcActor>())
        {
            float distance = Length(Minus(npc->Position(), Position()));
            if (npc->job == NpcJob::Wraith && npc->health > 0 && distance < nearest
                && Navigation::Sight(Position(), npc->Position(), context.walkable))
            {
                target = npc;
                nearest = distance;
            }
        }
    }
    if (!target)
    {
        return false;
    }
    return ShootAt(*target, context);
}

bool WeaponActor::ShootAt(Actor& target, GameplayContext& context)
{
    auto* owner = Scene().Get<PlayerActor>(Parent());
    if (!owner || owner->yokai != YokaiForm::Ranged || shotTimer_ > 0
        || context.state == RunState::Lost || context.state == RunState::Won
        || Length(Minus(target.Position(), Position())) > owner->Range()
        || !Navigation::Sight(Position(), target.Position(), context.walkable))
    {
        return false;
    }
    auto* npc = dynamic_cast<NpcActor*>(&target);
    if (npc
        && (npc->health <= 0
            || (npc->job != NpcJob::Wraith && (!context.society || !context.society->pk))))
    {
        return false;
    }
    aim = Unit(Minus(target.Position(), Position()));
    auto& bullet = context.scene.Spawn<ProjectileActor>(
        NoActor, FarmPoint{aim.x * 560, aim.y * 560}, owner->Range(), owner->Damage());
    bullet.SetPosition(Position());
    if (npc && npc->job != NpcJob::Wraith)
    {
        bullet.humanTarget = npc->Id();
    }
    shotTimer_ = owner->Cooldown();
    muzzleFlash = .12f;
    owner->ApplyShotRecoil(aim);
    return true;
}
