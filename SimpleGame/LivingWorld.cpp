#include "stdafx.h"
#include "LivingWorld.h"
#include "GameplayContext.h"
#include "GameplayActors.h"
#include "RenderDiagnostics.h"
#include <algorithm>
#include <array>
#include <queue>
#include <sstream>
#include <cmath>

namespace
{
    FarmPoint Add(FarmPoint a, FarmPoint b)
    {
        return {a.x + b.x, a.y + b.y};
    }

    bool Alive(const NpcActor& npc)
    {
        return npc.health > 0 && npc.recovery <= 0;
    }

    // Small bounded BFS in town-local space. Paths survive camera origin rebasing.
    std::vector<FarmPoint> Path(FarmPoint start,
                                FarmPoint goal,
                                FarmPoint origin,
                                const Walkable& walkable)
    {
        constexpr int Width = 31;
        constexpr float Cell = 24, Offset = -120;
        auto index = [=](FarmPoint p)
        {
            int x = std::max(0, std::min(Width - 1, int(std::round((p.x - Offset) / Cell))));
            int y = std::max(0, std::min(Width - 1, int(std::round((p.y - Offset) / Cell))));
            return y * Width + x;
        };
        auto position = [=](int i)
        {
            return FarmPoint{Offset + (i % Width) * Cell, Offset + (i / Width) * Cell};
        };
        std::array<int, Width * Width> parents;
        parents.fill(-1);
        std::queue<int> open;
        int from = index(start), to = index(goal);
        parents[from] = from;
        open.push(from);
        while (!open.empty() && parents[to] < 0)
        {
            int current = open.front();
            open.pop();
            const int dx[] = {-1, 1, 0, 0}, dy[] = {0, 0, -1, 1};
            for (int d = 0; d < 4; ++d)
            {
                int x = current % Width + dx[d], y = current / Width + dy[d];
                if (x < 0 || y < 0 || x >= Width || y >= Width)
                {
                    continue;
                }
                int next = y * Width + x;
                if (parents[next] >= 0)
                {
                    continue;
                }
                auto point = Add(origin, position(next));
                if (!walkable(point.x, point.y, 12))
                {
                    continue;
                }
                parents[next] = current;
                open.push(next);
            }
        }
        if (parents[to] < 0)
        {
            return {};
        }
        std::vector<FarmPoint> result{goal};
        for (int i = to; i != from; i = parents[i])
        {
            result.push_back(position(i));
        }
        std::reverse(result.begin(), result.end());
        return result;
    }
} // namespace

NpcActor::NpcActor(std::string name, NpcJob role) : Actor(std::move(name)), job(role)
{
}

void NpcActor::Update(float dt, ActorUpdateContext& services)
{
    auto& context = dynamic_cast<GameplayContext&>(services);
    if (context.society)
    {
        context.society->UpdateNpc(*this, dt, context);
    }
}

const char* LivingWorld::JobName(NpcJob job)
{
    static const char* names[] = {"사냥꾼", "약탈자", "상인", "구도자", "경비", "기록자", "잔향귀"};
    return names[int(job)];
}

const char* LivingWorld::ActionName(NpcAction action)
{
    static const char* names[] = {"휴식", "사냥", "약탈", "거래", "명상", "순찰", "회복 중"};
    return names[int(action)];
}

const char* LivingWorld::SiteName(TownSite site)
{
    static const char* names[] = {
        "잔명 시장", "구호소", "백연의 기록소", "서진의 집", "학교 기록함", "묵의 기억"};
    return names[int(site)];
}

uint32_t LivingWorld::Random()
{
    random_ ^= random_ << 13;
    random_ ^= random_ >> 17;
    random_ ^= random_ << 5;
    return random_;
}

FarmPoint LivingWorld::Center(const SceneGraph& graph) const
{
    auto* root = graph.Get(town);
    return root ? root->Position() : FarmPoint{};
}

void LivingWorld::Note(const std::string& text, const std::string& kind)
{
    events.push_front(text);
    if (events.size() > 5)
    {
        events.pop_back();
    }
    RenderDiagnostics::Get().Event(kind, "simulation_s=" + std::to_string(clock) + "; " + text);
}

void LivingWorld::Initialize(SceneGraph& graph, uint32_t seed)
{
    enabled = true;
    random_ = seed ? seed : 1;
    town = graph.Spawn<TownActor>(NoActor).Id();
    const FarmPoint sites[] = {{-40, 0}, {0, -70}, {0, 70}, {150, 0}, {0, 250}, {0, 450}};
    for (int i = 0; i < 6; ++i)
    {
        graph.Spawn<TownSiteActor>(town, TownSite(i)).local = sites[i];
    }
    const char* names[] = {"도윤",
                           "유리",
                           "건우",
                           "갈치",
                           "붉은 눈",
                           "미령",
                           "덕수",
                           "이안",
                           "무명 순례자",
                           "초소장",
                           "백연",
                           "잔향귀 하나",
                           "잔향귀 둘",
                           "잔향귀 셋"};
    const NpcJob roles[] = {NpcJob::Hunter,
                            NpcJob::Hunter,
                            NpcJob::Hunter,
                            NpcJob::Raider,
                            NpcJob::Raider,
                            NpcJob::Merchant,
                            NpcJob::Merchant,
                            NpcJob::Seeker,
                            NpcJob::Seeker,
                            NpcJob::Guard,
                            NpcJob::Recorder,
                            NpcJob::Wraith,
                            NpcJob::Wraith,
                            NpcJob::Wraith};
    const FarmPoint positions[] = {{50, 0},
                                   {0, 125},
                                   {200, 0},
                                   {460, 150},
                                   {460, 260},
                                   {-38, -28},
                                   {-10, 25},
                                   {0, 380},
                                   {0, 430},
                                   {20, -40},
                                   {0, 70},
                                   {220, 460},
                                   {340, 460},
                                   {460, 410}};
    for (int i = 0; i < 14; ++i)
    {
        auto& npc = graph.Spawn<NpcActor>(town, names[i], roles[i]);
        npc.local = npc.home = positions[i];
        npc.wealthWeight = .5f + float(Random() % 40) / 100;
        npc.gold = 45 + int(Random() % 50);
        if (npc.job == NpcJob::Hunter)
        {
            npc.survivalWeight = .8f;
            npc.maximumHealth = npc.health = 115;
        }
        if (npc.job == NpcJob::Raider)
        {
            npc.raidWeight = .90f + float(Random() % 10) / 100;
            npc.wealthWeight = .95f;
            npc.maximumHealth = npc.health = 125;
        }
        if (npc.job == NpcJob::Merchant)
        {
            npc.gold = 250;
            npc.goods = 8;
        }
        if (npc.job == NpcJob::Seeker || npc.job == NpcJob::Recorder)
        {
            npc.insightWeight = 1;
        }
        if (npc.job == NpcJob::Guard)
        {
            npc.maximumHealth = npc.health = 180;
        }
        if (npc.job == NpcJob::Wraith)
        {
            npc.maximumHealth = npc.health = 48;
            npc.gold = 12;
            npc.goods = 1;
        }
        npc.decision = float(i) * .08f;
    }
    Note("잔명 마을: 주민 11명과 외곽 잔향귀 3체의 생활을 시작합니다.", "world_start");
}

NpcAction LivingWorld::Choose(const NpcActor& npc)
{
    if (!Alive(npc))
    {
        return NpcAction::Recover;
    }
    float ratio = npc.health / npc.maximumHealth;
    if (ratio < .38f + npc.survivalWeight * .15f)
    {
        return NpcAction::Rest;
    }
    if (npc.job == NpcJob::Guard)
    {
        return NpcAction::Patrol;
    }
    if (npc.job == NpcJob::Recorder)
    {
        return NpcAction::Meditate;
    }
    if (npc.job == NpcJob::Merchant)
    {
        return NpcAction::Trade;
    }
    if (npc.goods > 0)
    {
        return NpcAction::Trade;
    }
    // Utility comparison: predation is a tunable preference, not random PK.
    float raid = npc.raidWeight * npc.wealthWeight * 1.7f * ratio;
    float hunt = npc.wealthWeight * .8f + (npc.gold < 12 ? .6f : 0);
    float insight = npc.insightWeight * 1.2f;
    if (raid > hunt && raid > insight)
    {
        return NpcAction::Raid;
    }
    return insight > hunt ? NpcAction::Meditate : NpcAction::Hunt;
}

void LivingWorld::Decide(NpcActor& npc, GameplayContext& context)
{
    NpcAction previous = npc.action;
    npc.action = Choose(npc);
    npc.target = NoActor;
    if (npc.aggressor != NoActor)
    {
        auto* attacker = context.scene.Get<NpcActor>(npc.aggressor);
        if (attacker && Alive(*attacker) && npc.health > npc.maximumHealth * .35f
            && Length(Minus(attacker->Position(), npc.Position())) < 220)
        {
            npc.target = attacker->Id();
        }
        else
        {
            npc.aggressor = NoActor;
        }
    }
    if (npc.target == NoActor
        && (npc.action == NpcAction::Hunt || npc.action == NpcAction::Raid
            || npc.action == NpcAction::Patrol))
    {
        float best = 100000;
        for (auto* other : context.scene.Query<NpcActor>())
        {
            if (other == &npc || !Alive(*other))
            {
                continue;
            }
            bool candidate = npc.action == NpcAction::Hunt ? other->job == NpcJob::Wraith
                             : npc.action == NpcAction::Raid
                                 ? other->job != NpcJob::Wraith && other->job != NpcJob::Raider
                                       && other->job != NpcJob::Guard
                                       && other->job != NpcJob::Recorder && other->gold > 0
                                       && Length(other->local) > 130
                                 : other->offender;
            if (!candidate)
            {
                continue;
            }
            float score = Length(Minus(other->Position(), npc.Position()));
            if (npc.action == NpcAction::Raid)
            {
                score -= float(other->gold) * .5f;
            }
            if (score < best)
            {
                best = score;
                npc.target = other->Id();
            }
        }
    }
    if (npc.action != previous)
    {
        RenderDiagnostics::Get().Event(
            "npc_decision",
            "id=" + std::to_string(npc.Id()) + "; name=" + npc.Name() + "; action="
                + ActionName(npc.action) + "; raid_weight=" + std::to_string(npc.raidWeight)
                + "; hp=" + std::to_string(npc.health) + "; gold=" + std::to_string(npc.gold));
    }
}

void LivingWorld::MoveTo(NpcActor& npc, FarmPoint goal, float dt, GameplayContext& context)
{
    FarmPoint origin = Center(context.scene);
    FarmPoint position = npc.local;
    if (Length(Minus(position, goal)) < 9)
    {
        return;
    }
    FarmPoint worldGoal = Add(origin, goal);
    FarmPoint next = goal;
    if (!Navigation::Sight(npc.Position(), worldGoal, context.walkable, 12))
    {
        if (npc.repath <= 0
            && (npc.pathIndex >= npc.path.size() || Length(Minus(npc.destination, goal)) > 35))
        {
            npc.path = Path(position, goal, origin, context.walkable);
            npc.pathIndex = 0;
            npc.destination = goal;
            npc.repath = 1;
        }
        if (npc.pathIndex >= npc.path.size())
        {
            return;
        }
        next = npc.path[npc.pathIndex];
        if (Length(Minus(position, next)) < 8)
        {
            ++npc.pathIndex;
            return;
        }
    }
    FarmPoint delta = Minus(next, position);
    float speed = npc.action == NpcAction::Rest ? 82.f : npc.job == NpcJob::Wraith ? 40.f : 65.f;
    float step = std::min(Length(delta), speed * dt);
    auto direction = Unit(delta);
    auto candidate = Add(npc.Position(), {direction.x * step, direction.y * step});
    if (Navigation::Sight(npc.Position(), candidate, context.walkable, 11))
    {
        npc.SetPosition(candidate);
    }
    else
    {
        npc.path.clear();
    }
}

bool LivingWorld::Transfer(NpcActor& seller, NpcActor& buyer, int price)
{
    if (&seller == &buyer || seller.goods <= 0 || buyer.gold < price || price <= 0)
    {
        return false;
    }
    --seller.goods;
    ++buyer.goods;
    seller.gold += price;
    buyer.gold -= price;
    return true;
}

void LivingWorld::Trade(NpcActor& npc, GameplayContext& context)
{
    if (npc.job == NpcJob::Merchant)
    {
        return;
    }
    for (auto* other : context.scene.Query<NpcActor>())
    {
        if (other->job != NpcJob::Merchant || !Alive(*other)
            || Length(Minus(other->Position(), npc.Position())) > 95)
        {
            continue;
        }
        bool sold = Transfer(npc, *other, 8);
        if (sold)
        {
            ++trades;
            Note(npc.Name() + " → " + other->Name() + ": 잔향석 1개 / 8전", "npc_trade");
        }
        if (npc.health < npc.maximumHealth && Transfer(*other, npc, 12))
        {
            --npc.goods;
            npc.health = std::min(npc.maximumHealth, npc.health + 30);
            ++trades;
            Note(npc.Name() + ": 12전으로 회복 재료 구입", "npc_trade");
        }
        return;
    }
}

void LivingWorld::Defeat(NpcActor& victim, NpcActor* attacker, GameplayContext& context)
{
    bool prey = victim.job == NpcJob::Wraith;
    int money = prey ? victim.gold : victim.gold / 2;
    int cargo = victim.goods;
    victim.gold -= money;
    victim.goods = 0;
    victim.recovery = prey ? 22.f : 28.f;
    victim.action = NpcAction::Recover;
    victim.path.clear();
    victim.target = victim.aggressor = NoActor;
    victim.playerHostile = false;
    victim.offender = false;
    if (attacker)
    {
        attacker->gold += money;
        attacker->goods += cargo;
        ++attacker->victories;
        if (attacker->growth < 10)
        {
            ++attacker->growth;
            attacker->maximumHealth += 3;
        }
        attacker->health = std::min(attacker->maximumHealth, attacker->health + 8);
    }
    else
    {
        gold += money;
        goods += cargo;
        if (prey)
        {
            ++story.hunted;
            context.player.AddExperience(14, context);
            if (story.mukContracted)
            {
                story.mukSync = std::min(100, story.mukSync + 3);
            }
        }
    }
    prey ? ++hunts : ++raids;
    Note((attacker ? attacker->Name() : "윤서진") + std::string(prey ? " 사냥: " : " 약탈: ")
             + victim.Name() + " / " + std::to_string(money) + "전, 잔향석 "
             + std::to_string(cargo),
         prey ? "hunt" : "raid");
}

void LivingWorld::Damage(NpcActor& victim,
                         float amount,
                         NpcActor* attacker,
                         GameplayContext& context)
{
    if (!Alive(victim) || amount <= 0)
    {
        return;
    }
    victim.health = std::max(0.f, victim.health - amount);
    if (attacker)
    {
        victim.aggressor = attacker->Id();
        if (attacker->action == NpcAction::Raid && victim.job != NpcJob::Wraith)
        {
            attacker->offender = true;
        }
    }
    else if (victim.job != NpcJob::Wraith)
    {
        victim.playerHostile = true;
        notoriety = std::min(100, notoriety + 5);
    }
    victim.decision = 0;
    context.Effect(victim.Position(), "-" + std::to_string(int(amount)), .4f);
    if (victim.health <= 0)
    {
        Defeat(victim, attacker, context);
    }
}

void LivingWorld::UpdateNpc(NpcActor& npc, float dt, GameplayContext& context)
{
    npc.cooldown = std::max(0.f, npc.cooldown - dt);
    npc.repath = std::max(0.f, npc.repath - dt);
    if (npc.recovery > 0)
    {
        npc.recovery = std::max(0.f, npc.recovery - dt);
        if (npc.recovery == 0)
        {
            npc.local = npc.home;
            npc.health = npc.maximumHealth;
            npc.decision = 0;
            if (npc.job == NpcJob::Wraith)
            {
                npc.gold = 12;
                npc.goods = 1;
            }
            Note(npc.Name()
                     + (npc.job == NpcJob::Wraith ? ": 균열에서 재출현" : ": 구호소 치료 후 복귀"),
                 "npc_recover");
        }
        return;
    }
    npc.decision -= dt;
    if (npc.decision <= 0)
    {
        Decide(npc, context);
        npc.decision = .8f + float(npc.Id() % 5) * .07f;
    }
    auto origin = Center(context.scene);
    bool playerAlive = context.state != RunState::Lost && context.player.enabled;
    bool attacksPlayer = npc.playerHostile || (npc.job == NpcJob::Guard && notoriety > 0)
                         || (npc.action == NpcAction::Raid && gold > 0
                             && Length(Minus(context.player.Position(), origin)) > 130
                             && Length(Minus(npc.Position(), context.player.Position())) < 170)
                         || (npc.job == NpcJob::Wraith
                             && Length(Minus(npc.Position(), context.player.Position())) < 115);
    if (playerAlive && attacksPlayer && npc.health > npc.maximumHealth * .3f)
    {
        auto target = context.player.Position();
        if (Length(Minus(target, origin)) < 650)
        {
            MoveTo(npc, Minus(target, origin), dt, context);
            if (npc.cooldown <= 0 && Length(Minus(npc.Position(), target)) < 37
                && Navigation::Sight(npc.Position(), target, context.walkable))
            {
                context.player.DamagePlayer(npc.job == NpcJob::Guard ? 12.f : 7.f, context);
                npc.cooldown = 1;
                if (context.state == RunState::Lost)
                {
                    int stolen = gold / 2;
                    gold -= stolen;
                    npc.gold += stolen;
                    npc.goods += goods;
                    goods = 0;
                    ++raids;
                    Note(npc.Name() + ": 서진을 제압하고 " + std::to_string(stolen) + "전 약탈",
                         "player_defeat");
                }
            }
            return;
        }
    }
    if (npc.job == NpcJob::Wraith)
    {
        npc.action = NpcAction::Patrol;
        if (auto* enemy = context.scene.Get<NpcActor>(npc.aggressor))
        {
            if (Alive(*enemy) && Length(Minus(enemy->Position(), npc.Position())) < 48
                && npc.cooldown <= 0)
            {
                Damage(*enemy, 7, &npc, context);
                npc.cooldown = 1;
            }
        }
        return;
    }
    if (auto* target = context.scene.Get<NpcActor>(npc.target))
    {
        if (Alive(*target))
        {
            MoveTo(npc, target->local, dt, context);
            if (npc.cooldown <= 0 && Length(Minus(npc.Position(), target->Position())) < 40
                && Navigation::Sight(npc.Position(), target->Position(), context.walkable))
            {
                Damage(*target, npc.job == NpcJob::Guard ? 22.f : 14.f + npc.growth, &npc, context);
                npc.cooldown = .9f;
            }
            return;
        }
    }
    FarmPoint destination{};
    if (npc.action == NpcAction::Meditate)
    {
        destination = npc.job == NpcJob::Recorder ? npc.home : FarmPoint{0, 420};
    }
    else if (npc.action == NpcAction::Rest)
    {
        destination = {0, -60};
    }
    else if (npc.action == NpcAction::Trade)
    {
        destination = npc.job == NpcJob::Merchant ? npc.home : FarmPoint{-15, 0};
    }
    else
    {
        const FarmPoint patrol[] = {{0, 0}, {0, 460}, {460, 460}, {460, 0}};
        destination = patrol[(int(clock / 12) + int(npc.Id())) % 4];
    }
    MoveTo(npc, destination, dt, context);
    if (Length(Minus(npc.local, destination)) < 40)
    {
        npc.actionTime += dt;
        if (npc.action == NpcAction::Rest)
        {
            npc.health = std::min(npc.maximumHealth, npc.health + dt * 9);
        }
        if (npc.actionTime >= 3)
        {
            npc.actionTime = 0;
            if (npc.action == NpcAction::Meditate)
            {
                npc.insight = std::min(999, npc.insight + 1);
                if (npc.insight % 5 == 0 && npc.growth < 10)
                {
                    ++npc.growth;
                    npc.maximumHealth += 4;
                    npc.health = std::min(npc.maximumHealth, npc.health + 4);
                    Note(npc.Name() + ": 깨달음으로 최대 체력 성장", "insight");
                }
            }
            if (npc.action == NpcAction::Trade)
            {
                Trade(npc, context);
            }
        }
    }
}

void LivingWorld::Tick(float dt, GameplayContext& context)
{
    clock += dt;
    strikeCooldown_ = std::max(0.f, strikeCooldown_ - dt);
    summaryTimer_ += dt;
    if (summaryTimer_ >= 5)
    {
        summaryTimer_ = 0;
        std::ostringstream summary;
        summary << "time=" << clock << "; trades=" << trades << "; raids=" << raids
                << "; hunts=" << hunts << "; player_gold=" << gold << "; npcs=";
        for (auto* npc : context.scene.Query<NpcActor>())
        {
            summary << npc->Id() << ':' << npc->Name() << ':' << JobName(npc->job) << ':'
                    << ActionName(npc->action) << ":hp=" << npc->health << ":gold=" << npc->gold
                    << ":goods=" << npc->goods << ":max_hp=" << npc->maximumHealth
                    << ":insight=" << npc->insight << ":local_x=" << npc->local.x
                    << ":local_y=" << npc->local.y << ":target=" << npc->target
                    << ":raid_weight=" << npc->raidWeight << ":wealth_weight=" << npc->wealthWeight
                    << ":survival_weight=" << npc->survivalWeight
                    << ":insight_weight=" << npc->insightWeight << '|';
        }
        RenderDiagnostics::Get().Event("world_snapshot", summary.str());
    }
}
