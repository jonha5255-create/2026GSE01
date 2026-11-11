#include "stdafx.h"
#include "LivingWorld.h"
#include "GameplayContext.h"
#include "GameplayActors.h"
#include "WorldTypes.h"
#include <algorithm>
#include <cmath>

namespace
{
    struct PatternSpec
    {
        ArenaPattern pattern;
        const char* name;
        float windup, radius, reach, damage;
    };

    constexpr PatternSpec Patterns[3][3] = {
        {{ArenaPattern::Circle, "제단 강타", .85f, 64, 0, 23},
         {ArenaPattern::Charge, "뿔 돌진 / 옆으로 회피", 1.1f, 30, 340, 29},
         {ArenaPattern::Ring, "핏빛 고리 / 안쪽 또는 바깥으로", 1.45f, 63, 146, 25}},
        {{ArenaPattern::Pounce, "사냥 도약 / 착지 지점에서 이탈", .85f, 57, 230, 23},
         {ArenaPattern::Fan, "부채꼴 독액 / 포식견의 옆으로", 1.05f, .52f, 270, 25},
         {ArenaPattern::Poison, "독웅덩이 / 오염된 바닥에서 이탈", 1.15f, 64, 0, 14}},
        {{ArenaPattern::Circle, "거신 내려찍기", 1.05f, 72, 0, 28},
         {ArenaPattern::Cross, "십자 균열 / 대각선으로 회피", 1.3f, 36, 310, 32},
         {ArenaPattern::Ring, "붕괴 파동 / 고리 바깥이나 안으로", 1.65f, 92, 200, 31}},
    };

    constexpr PatternSpec TrialPatterns[] = {
        {ArenaPattern::Charge, "봉인 돌진 / 표시된 봉인석 뒤로 유도", 1.75f, 38, 370, 29},
        {ArenaPattern::Poison, "독웅덩이 / 배수판에서 떨어진 곳에 유도", 1.15f, 61, 0, 14},
        {ArenaPattern::Core, "핵 폭주 / 장치를 연속으로 해제", 8.f, 0, 0, 32},
    };
    constexpr PatternSpec BloodWave =
        {ArenaPattern::BloodSeal, "핏빛 파동 / 중앙 원으로", 2.6f, 78, 0, 27};
}

FarmPoint ArenaDevice(int region, unsigned index)
{
    constexpr FarmPoint positions[3][3] = {
        {{320, 432}, {222, 301}, {414, 273}},
        {{246, 396}, {395, 357}, {315, 215}},
        {{227, 360}, {408, 381}, {342, 214}},
    };
    return positions[region][index];
}

const char* ArenaDeviceName(int region)
{
    constexpr const char* names[] = {"봉인석", "배수판", "핵 장치"};
    return names[region];
}

bool ArenaAttack::Hits(FarmPoint target) const
{
    const auto delta = Minus(target, origin);
    switch (pattern)
    {
    case ArenaPattern::Circle:
    case ArenaPattern::Poison:
    case ArenaPattern::Pounce:
        return Length(delta) <= radius;
    case ArenaPattern::Ring:
        return Length(delta) >= radius && Length(delta) <= reach;
    case ArenaPattern::BloodSeal:
        return Length(delta) > radius;
    case ArenaPattern::Fan:
        return Length(delta) <= reach
               && delta.x * direction.x + delta.y * direction.y >= Length(delta) * std::cos(radius);
    case ArenaPattern::Cross:
        return (std::abs(delta.x) <= radius && std::abs(delta.y) <= reach)
               || (std::abs(delta.y) <= radius && std::abs(delta.x) <= reach);
    case ArenaPattern::Charge:
    {
        float forward = delta.x * direction.x + delta.y * direction.y;
        float side = delta.x * direction.y - delta.y * direction.x;
        return forward >= 0 && forward <= reach && std::abs(side) <= radius;
    }
    case ArenaPattern::Core:
        return true;
    default:
        return false;
    }
}

float LivingWorld::LimitArenaDamage(NpcActor& boss, float damage)
{
    if (arenaTrial.active)
    {
        return 0;
    }
    if (arenaTrial.phase >= 2)
    {
        return damage;
    }
    const float threshold = boss.maximumHealth * (arenaTrial.phase == 0 ? .50f : .15f);
    const float toThreshold = std::max(0.f, boss.health - threshold);
    if (damage < toThreshold)
    {
        return damage;
    }
    ++arenaTrial.phase;
    arenaTrial.active = true;
    arenaTrial.used = arenaTrial.casts = 0;
    arenaTrial.hold = arenaTrial.linkTime = 0;
    arenaTrial.holdingDevice = -1;
    arenaAttack = {};
    boss.attackTime = -1;
    boss.cooldown = 1.3f;
    dialogue = arenaTrial.phase == 1 ? "보스가 맵 장치에 힘을 연결했습니다."
                                     : "발악 단계! 강화된 장치 연결을 모두 끊으세요.";
    return toThreshold;
}

void LivingWorld::CompleteArenaDevice(NpcActor& boss, unsigned index)
{
    arenaTrial.used |= 1u << index;
    if (arenaTrial.UsedCount() == arenaTrial.DeviceCount())
    {
        arenaTrial.active = false;
        arenaAttack = {};
        boss.attackTime = -1;
        boss.cooldown = 2.8f;
        dialogue = "보호막이 깨졌습니다. 보스가 멈춘 사이 공격하세요.";
    }
}

bool LivingWorld::UseArenaDevice(FarmPoint player, GameplayContext& context)
{
    auto* boss = context.scene.Get<NpcActor>(arenaBoss);
    if (!arenaTrial.active || !boss || boss->bossRegion != 2)
    {
        return false;
    }
    for (unsigned i = 0; i < arenaTrial.DeviceCount(); ++i)
    {
        if ((arenaTrial.used & (1u << i)) || Length(Minus(ArenaDevice(2, i), player)) >= 34)
        {
            continue;
        }
        if (arenaTrial.used == 0)
        {
            arenaTrial.linkTime = arenaTrial.phase == 1 ? 4.f : 5.f;
        }
        CompleteArenaDevice(*boss, i);
        return true;
    }
    return false;
}

void LivingWorld::UpdateArenaBoss(NpcActor& boss, float dt, GameplayContext& context)
{
    if (boss.health <= 0)
    {
        arenaAttack = {};
        boss.attackTime = -1;
        boss.recovery -= dt;
        if (boss.recovery <= 0)
        {
            boss.Destroy();
            arenaBoss = NoActor;
        }
        return;
    }
    if (context.state == RunState::Lost)
    {
        arenaAttack = {};
        boss.attackTime = -1;
        return;
    }
    const auto origin = Minus(boss.Position(), boss.local);
    const auto player = Minus(context.player.Position(), origin);
    auto& attack = arenaAttack;
    if (arenaTrial.active && boss.bossRegion == 2 && arenaTrial.used != 0)
    {
        arenaTrial.linkTime -= dt;
        if (arenaTrial.linkTime <= 0)
        {
            arenaTrial.used = 0;
        }
    }
    if (arenaTrial.active && boss.bossRegion == 1)
    {
        int plate = -1;
        for (unsigned i = 0; i < arenaTrial.DeviceCount(); ++i)
        {
            if (!(arenaTrial.used & (1u << i))
                && Length(Minus(player, ArenaDevice(1, i))) < 34
                && !(attack.lingering && attack.Hits(player)))
            {
                plate = int(i);
                break;
            }
        }
        if (plate != arenaTrial.holdingDevice)
        {
            arenaTrial.hold = 0;
            arenaTrial.holdingDevice = plate;
        }
        if (plate >= 0)
        {
            arenaTrial.hold += dt;
            if (arenaTrial.hold >= 1.f)
            {
                CompleteArenaDevice(boss, unsigned(plate));
                arenaTrial.hold = 0;
                arenaTrial.holdingDevice = -1;
                if (!arenaTrial.active)
                {
                    return;
                }
            }
        }
    }
    if (boss.attackTime >= boss.arenaWindup)
    {
        boss.attackTime += dt;
        if (boss.attackTime >= boss.arenaWindup + .36f)
        {
            boss.attackTime = -1;
        }
    }
    if (attack.pattern != ArenaPattern::None)
    {
        attack.remaining -= dt;
        if (attack.lingering)
        {
            attack.pulse -= dt;
            if (attack.pulse <= 0)
            {
                attack.pulse = .9f;
                if (attack.Hits(player))
                {
                    context.player.DamagePlayer(attack.damage, context);
                }
            }
        }
        else if (boss.attackTime >= 0)
        {
            boss.attackTime = boss.arenaWindup
                              * std::clamp(1.f - attack.remaining / attack.windup, 0.f, 1.f);
        }
        if (attack.remaining > 0)
        {
            return;
        }
        if (!attack.lingering && attack.Hits(player))
        {
            context.player.DamagePlayer(attack.damage, context);
        }
        if (attack.pattern == ArenaPattern::Charge)
        {
            const auto start = boss.Position();
            for (float distance = 8; distance <= attack.reach; distance += 8)
            {
                FarmPoint next{start.x + attack.direction.x * distance,
                               start.y + attack.direction.y * distance};
                if (!context.walkable(next.x, next.y, 18))
                {
                    break;
                }
                boss.SetPosition(next);
                unsigned index = arenaTrial.UsedCount();
                if (arenaTrial.active
                    && Length(Minus(boss.local, ArenaDevice(0, index))) < attack.radius)
                {
                    CompleteArenaDevice(boss, index);
                    if (!arenaTrial.active)
                    {
                        return;
                    }
                    break;
                }
            }
        }
        if (attack.pattern == ArenaPattern::Pounce)
        {
            boss.SetPosition({origin.x + attack.origin.x, origin.y + attack.origin.y});
        }
        if (!attack.lingering)
        {
            boss.attackTime = boss.arenaWindup;
        }
        if (attack.pattern == ArenaPattern::Poison && !attack.lingering)
        {
            attack.lingering = true;
            attack.remaining = 3.6f;
            attack.pulse = .9f;
            return;
        }
        attack = {};
        boss.cooldown = arenaTrial.phase == 2 ? .9f : 1.35f;
        return;
    }
    boss.cooldown = std::max(0.f, boss.cooldown - dt);
    boss.repath = std::max(0.f, boss.repath - dt);
    boss.action = NpcAction::Hunt;
    const unsigned phase = arenaTurn_ % 3;
    const bool bloodWave = arenaTrial.active && boss.bossRegion == 0
                           && arenaTrial.phase == 2 && arenaTrial.casts % 2 == 1;
    const auto& spec = !arenaTrial.active ? Patterns[boss.bossRegion][phase]
                       : bloodWave ? BloodWave : TrialPatterns[boss.bossRegion];
    const auto delta = Minus(player, boss.local);
    if (boss.cooldown > 0)
    {
        return;
    }
    if (spec.pattern == ArenaPattern::Circle && Length(delta) > spec.radius - 12
        && arenaChase_ < 2.f)
    {
        arenaChase_ += dt;
        MoveTo(boss, player, dt, context);
        return;
    }
    if (arenaTrial.active && spec.pattern == ArenaPattern::Charge
        && Length(Minus(boss.local, ArenaDevice(0, arenaTrial.UsedCount()))) > spec.reach - 60.f)
    {
        MoveTo(boss, boss.home, dt, context);
        return;
    }
    arenaChase_ = 0;
    attack.pattern = spec.pattern;
    attack.name = spec.name;
    attack.origin = boss.local;
    if (spec.pattern == ArenaPattern::Poison)
    {
        attack.origin = player;
    }
    if (spec.pattern == ArenaPattern::BloodSeal)
    {
        attack.origin = {ArenaCenter, ArenaCenter};
    }
    attack.direction = Length(delta) > .01f ? Unit(delta) : boss.moveFacing;
    attack.remaining = attack.windup = spec.windup * (arenaTrial.phase == 2 ? .82f : 1.f);
    attack.radius = spec.radius;
    attack.reach = spec.reach;
    if (spec.pattern == ArenaPattern::Charge || spec.pattern == ArenaPattern::Pounce)
    {
        float travel = spec.pattern == ArenaPattern::Pounce ? std::min(spec.reach, Length(delta))
                                                           : spec.reach;
        const auto start = boss.Position();
        float reachable = 0;
        while (reachable < travel)
        {
            float next = std::min(travel, reachable + 8.f);
            if (!context.walkable(start.x + attack.direction.x * next,
                                  start.y + attack.direction.y * next, 18))
            {
                break;
            }
            reachable = next;
            if (arenaTrial.active && spec.pattern == ArenaPattern::Charge)
            {
                const auto device = ArenaDevice(0, arenaTrial.UsedCount());
                const FarmPoint point{boss.local.x + attack.direction.x * next,
                                      boss.local.y + attack.direction.y * next};
                if (Length(Minus(point, device)) < attack.radius)
                {
                    break;
                }
            }
        }
        if (spec.pattern == ArenaPattern::Pounce)
        {
            attack.origin = {boss.local.x + attack.direction.x * reachable,
                             boss.local.y + attack.direction.y * reachable};
        }
        else
        {
            attack.reach = reachable;
        }
    }
    attack.damage = spec.damage * (arenaTrial.phase == 2 ? 1.2f : 1.f);
    boss.attackDirection = attack.direction;
    boss.arenaWindup = attack.windup;
    if (spec.pattern == ArenaPattern::Pounce)
    {
        boss.arenaClip = "_Jump";
    }
    else if (spec.pattern == ArenaPattern::Poison || spec.pattern == ArenaPattern::Core
             || spec.pattern == ArenaPattern::BloodSeal)
    {
        boss.arenaClip = "_TrialCast";
    }
    else
    {
        boss.arenaClip = spec.pattern == ArenaPattern::Circle ? "_AttackPrimary" : "_AttackSecondary";
    }
    boss.attackTime = 0;
    if (arenaTrial.active)
    {
        ++arenaTrial.casts;
    }
    else
    {
        ++arenaTurn_;
    }
}
