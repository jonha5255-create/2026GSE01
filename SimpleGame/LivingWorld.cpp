#include "stdafx.h"
#include "LivingWorld.h"
#include "GameplayContext.h"
#include "GameplayActors.h"
#include "RenderDiagnostics.h"
#include "WorldTypes.h"
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

    FarmPoint Times(FarmPoint p, float factor)
    {
        return {p.x * factor, p.y * factor};
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
        constexpr int Width = 81;
        constexpr float Cell = 24, Offset = -660;
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
        auto before = local;
        context.society->UpdateNpc(*this, dt, context);
        auto delta = Minus(local, before);
        float distance = Length(delta);
        bool moving = dt > 0 && distance > .001f && distance < 100 * dt && health > 0;
        float blend = 1 - std::exp(-12 * std::max(0.f, dt));
        moveBlend += ((moving ? 1.f : 0.f) - moveBlend) * blend;
        if (moving)
        {
            gaitPhase =
                std::fmod(gaitPhase + distance * (monsterForm == 2 ? .12f : .2f), 6.2831853f);
            auto direction = Unit(delta);
            moveFacing = Unit({moveFacing.x + (direction.x - moveFacing.x) * blend,
                               moveFacing.y + (direction.y - moveFacing.y) * blend});
        }
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
    static const char* names[] = {"마을 시장",
                                  "구호소",
                                  "백연의 기록소",
                                  "하린의 사진 흔적",
                                  "학교 기록함",
                                  "묵의 기억",
                                  "동부 중계기",
                                  "사라진 승강장 신호",
                                  "존재하지 않는 역"};
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

void LivingWorld::RecordIncident(WorldIncident incident)
{
    incident.id = nextIncident_++;
    incident.time = clock;
    if (incident.type == "murder")
    {
        ++murders;
    }
    else if (incident.type == "rumor")
    {
        ++rumors;
    }
    Note(incident.text, incident.type);
    RenderDiagnostics::Get().Event(
        "incident_record",
        "id=" + std::to_string(incident.id) + "; source=" + std::to_string(incident.sourceId)
            + "; actor=" + std::to_string(incident.actor) + "; victim="
            + std::to_string(incident.victim) + "; witness=" + std::to_string(incident.witness)
            + "; village=" + std::to_string(incident.village));
    incidents.push_front(std::move(incident));
    if (incidents.size() > 128)
    {
        incidents.pop_back();
    }
}

void LivingWorld::Initialize(SceneGraph& graph, uint32_t seed)
{
    enabled = true;
    random_ = seed ? seed : 1;
    town = graph.Spawn<TownActor>(NoActor).Id();
    graph.Get<TownActor>(town)->index = 0;
    const FarmPoint sites[] = {
        {-40, 0}, {0, -70}, {0, 70}, {150, 0}, {0, 250}, {0, 450}, {1190, 0}, {1830, 0}, {2470, 0}};
    for (int i = 0; i < 9; ++i)
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
            npc.monsterForm = (i - 11) % 3;
            npc.maximumHealth = npc.health = 48;
            npc.gold = 12;
            npc.goods = 1;
        }
        npc.decision = float(i) * .08f;
    }
    auto* homeTown = graph.Get<TownActor>(town);
    int slot = 0;
    for (auto* npc : graph.Query<NpcActor>())
    {
        if (npc->job != NpcJob::Wraith)
        {
            npc->homeSlot = slot;
            npc->home = HomeDoor(slot);
            npc->storyResident = npc->job == NpcJob::Recorder;
            homeTown->homes[slot++] = npc->Id();
        }
    }
    const auto& layouts = VillageLayouts();
    for (int village = 0; village < 7; ++village)
    {
        auto* settlement =
            village == 0 ? homeTown : &graph.Spawn<TownActor>(NoActor, layouts[village].name);
        settlement->index = village;
        settlement->local = {layouts[village].x * 640.f, layouts[village].y * 640.f};
        settlement->dispatchTimer = 10 + village * 2.f;
        if (village != 0)
        {
            graph.Spawn<TownSiteActor>(settlement->Id(), TownSite::Market).local = {-25, 0};
            graph.Spawn<TownSiteActor>(settlement->Id(), TownSite::Clinic).local = {0, -25};
        }
        const NpcJob roles[] = {NpcJob::Hunter,
                                NpcJob::Merchant,
                                NpcJob::Seeker,
                                NpcJob::Guard,
                                NpcJob::Hunter,
                                NpcJob::Raider};
        for (int resident = village == 0 ? 11 : 0; resident < 30; ++resident)
        {
            auto job = roles[resident % 6];
            auto& npc = graph.Spawn<NpcActor>(settlement->Id(),
                                              std::string(layouts[village].name) + " 주민 "
                                                  + std::to_string(resident + 1),
                                              job);
            npc.homeSlot = resident;
            npc.local = npc.home = HomeDoor(resident);
            npc.settlementResident = true;
            npc.courier = resident == (village == 0 ? 11 : 0);
            npc.gold = job == NpcJob::Merchant ? 200 : 40 + int(Random() % 65);
            npc.goods = job == NpcJob::Merchant ? 8 : 1;
            npc.wealthWeight = .3f + float(Random() % 70) / 100;
            npc.survivalWeight = .3f + float(Random() % 70) / 100;
            npc.insightWeight = job == NpcJob::Seeker ? 1.f : float(Random() % 70) / 100;
            npc.raidWeight = job == NpcJob::Raider ? .9f : .05f;
            npc.migrationCooldown = 35 + float(Random() % 70);
            settlement->homes[resident] = npc.Id();
        }
    }
    Note("7개 마을 / 주민 210명: 전령이 편지와 소식을 전달합니다.", "world_start");
}

FarmPoint LivingWorld::HomeDoor(int slot)
{
    int chunk = slot / 4;
    return {float((chunk % 3 - 1) * 640 + 129 + (slot % 2) * 230),
            float((chunk / 3 - 1) * 640 + 196 + (slot % 4 / 2) * 230)};
}

TownActor* LivingWorld::Village(SceneGraph& graph, int index) const
{
    for (auto* settlement : graph.Query<TownActor>())
    {
        if (settlement->index == index)
        {
            return settlement;
        }
    }
    return nullptr;
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
            if (Length(Minus(other->Position(), npc.Position())) > 600)
            {
                continue;
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
    auto* parent = context.scene.Get(npc.Parent());
    FarmPoint origin = parent ? parent->Position() : FarmPoint{};
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
    if (npc.wilderness)
    {
        auto local = Minus(candidate, origin);
        if (local.x < 18 || local.y < 18 || local.x > 622 || local.y > 622)
        {
            return;
        }
    }
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
    if (!prey)
    {
        WorldIncident incident;
        incident.type = "murder";
        incident.actor = attacker ? attacker->Id() : context.player.Id();
        incident.actorName = attacker ? attacker->Name() : "윤서진";
        incident.victim = victim.Id();
        incident.victimName = victim.Name();
        for (auto* ancestor = context.scene.Get(victim.Parent()); ancestor;
             ancestor = context.scene.Get(ancestor->Parent()))
        {
            if (dynamic_cast<TownActor*>(ancestor))
            {
                incident.village = ancestor->Id();
                break;
            }
        }
        float nearest = 180.f * 180.f;
        for (auto* witness : context.scene.Query<NpcActor>())
        {
            if (witness == &victim || witness == attacker || witness->job == NpcJob::Wraith
                || !Alive(*witness))
            {
                continue;
            }
            auto a = witness->Position(), b = victim.Position();
            float distance = (a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y);
            if (distance < nearest)
            {
                nearest = distance;
                incident.witness = witness->Id();
                incident.witnessName = witness->Name();
            }
        }
        incident.text = incident.actorName + " → " + victim.Name()
                        + ": 살인 사건(현행 규칙에서는 치명적 제압 후 회복)";
        RecordIncident(std::move(incident));
    }
    int money = prey ? victim.gold : victim.gold / 2;
    int cargo = victim.goods;
    victim.gold -= money;
    victim.goods = 0;
    victim.recovery = victim.fieldBoss ? 90.f : prey ? 22.f : 28.f;
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
            if (victim.bossRegion >= 0 && victim.bossRegion < 3)
            {
                unsigned bit = 1u << victim.bossRegion;
                if (!(bossRelics & bit))
                {
                    bossRelics |= bit;
                    const char* relics[] = {"핏빛 인장", "독안개 심장", "석정 핵"};
                    dialogue = std::string("유물 획득: ") + relics[victim.bossRegion];
                    Note(dialogue, "quest");
                    context.Effect(victim.Position(), dialogue, 2.5f);
                    if (bossRelics == 7 && !bossRelicReward)
                    {
                        bossRelicReward = true;
                        gold += 150;
                        context.player.AddExperience(120, context);
                        dialogue = "삼역의 유물 완료! 보상: 150전 / 경험치 120";
                        Note(dialogue, "quest");
                    }
                }
            }
            if (victim.storyGuardian && story.gateOpened && !story.guardianDefeated)
            {
                story.guardianDefeated = true;
                victim.enabled = false;
                Note("묵: 문지기가 쓰러졌어. 역의 기록 장치를 E로 조사하자.", "story");
                dialogue = "문지기 제압! 역의 기록 장치에서 하린의 기억을 회수하세요.";
            }
            ++story.hunted;
            context.player.AddExperience(victim.fieldBoss ? 80 : 14, context);
            context.player.AddYokaiExperience(victim.fieldBoss ? 80 : 14, context);
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

void LivingWorld::BeginMonsterAttack(
    NpcActor& npc, ActorId target, bool player, float damage, FarmPoint position)
{
    npc.attackTime = 0;
    npc.attackHit = false;
    npc.attackPlayer = player;
    npc.attackVictim = target;
    npc.attackDamage = damage;
    npc.attackDirection = Unit(Minus(position, npc.Position()));
}

void LivingWorld::UpdateNpc(NpcActor& npc, float dt, GameplayContext& context)
{
    if (npc.health <= 0)
    {
        npc.attackTime = -1;
    }
    if (Travel(npc, dt, context))
    {
        return;
    }
    if (npc.attackTime >= 0 && npc.health > 0)
    {
        npc.attackTime += dt;
        float windup = npc.monsterForm == 1 ? .22f : npc.monsterForm == 2 ? .55f : .35f;
        if (!npc.attackHit && npc.attackTime >= windup)
        {
            npc.attackHit = true;
            auto* victim = context.scene.Get<NpcActor>(npc.attackVictim);
            FarmPoint target = npc.attackPlayer ? context.player.Position()
                               : victim         ? victim->Position()
                                                : npc.Position();
            bool valid = npc.attackPlayer
                             ? context.state != RunState::Lost
                             : victim && Alive(*victim) && context.scene.Active(victim->Id());
            if (valid && Length(Minus(target, npc.Position())) < (npc.fieldBoss ? 62.f : 50.f)
                && Navigation::Sight(npc.Position(), target, context.walkable))
            {
                if (npc.attackPlayer)
                {
                    context.player.DamagePlayer(npc.attackDamage, context);
                }
                else
                {
                    Damage(*victim, npc.attackDamage, &npc, context);
                }
            }
        }
        if (npc.attackTime >= windup + .4f)
        {
            npc.attackTime = -1;
            npc.cooldown = npc.monsterForm == 1 ? .45f : .8f;
        }
        return;
    }
    npc.cooldown = std::max(0.f, npc.cooldown - dt);
    npc.repath = std::max(0.f, npc.repath - dt);
    if (npc.storyGuardian)
    {
        if (npc.health <= 0)
        {
            return;
        }
        npc.action = NpcAction::Hunt;
        FarmPoint delta = Minus(context.player.Position(), npc.Position());
        if (context.state != RunState::Lost && Length(delta) < 245
            && Length(Minus(npc.local, npc.home)) < 240)
        {
            auto direction = Unit(delta);
            auto destination =
                Add(npc.Position(), Times(direction, std::min(Length(delta), dt * 38)));
            if (context.walkable(destination.x, destination.y, 12))
            {
                npc.SetPosition(destination);
            }
            if (npc.cooldown <= 0 && Length(delta) < 42
                && Navigation::Sight(npc.Position(), context.player.Position(), context.walkable))
            {
                BeginMonsterAttack(npc, NoActor, true, 12, context.player.Position());
                npc.cooldown = 1.2f;
            }
        }
        else
        {
            auto direction = Unit(Minus(npc.home, npc.local));
            auto destination =
                Add(npc.Position(),
                    Times(direction, std::min(Length(Minus(npc.home, npc.local)), dt * 30)));
            if (context.walkable(destination.x, destination.y, 12))
            {
                npc.SetPosition(destination);
            }
        }
        return;
    }
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
                npc.gold = npc.fieldBoss ? 65 : 12;
                npc.goods = npc.fieldBoss ? 5 : 1;
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
    auto* parent = context.scene.Get(npc.Parent());
    auto origin = parent ? parent->Position() : FarmPoint{};
    bool playerAlive = context.state != RunState::Lost && context.player.enabled;
    bool attacksPlayer = npc.playerHostile || (npc.job == NpcJob::Guard && notoriety > 0)
                         || (npc.action == NpcAction::Raid && gold > 0
                             && Length(Minus(context.player.Position(), origin)) > 130
                             && Length(Minus(npc.Position(), context.player.Position())) < 170)
                         || (npc.job == NpcJob::Wraith
                             && Length(Minus(npc.Position(), context.player.Position()))
                                    < (npc.fieldBoss ? 190.f : 115.f));
    if (playerAlive && attacksPlayer && npc.health > npc.maximumHealth * .3f)
    {
        auto target = context.player.Position();
        if (Length(Minus(target, origin)) < 650)
        {
            MoveTo(npc, Minus(target, origin), dt, context);
            if (npc.cooldown <= 0 && Length(Minus(npc.Position(), target)) < 37
                && Navigation::Sight(npc.Position(), target, context.walkable))
            {
                if (npc.job == NpcJob::Wraith)
                {
                    BeginMonsterAttack(npc,
                                       NoActor,
                                       true,
                                       npc.fieldBoss          ? 18.f
                                       : npc.monsterForm == 2 ? 11.f
                                                              : 7.f,
                                       target);
                }
                else
                {
                    context.player.DamagePlayer(npc.fieldBoss              ? 18.f
                                                : npc.job == NpcJob::Guard ? 12.f
                                                                           : 7.f,
                                                context);
                }
                npc.cooldown = npc.fieldBoss ? 1.4f : 1.f;
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
        if (npc.wilderness)
        {
            npc.wanderTimer -= dt;
            if (npc.wanderTimer <= 0 || Length(Minus(npc.local, npc.wanderGoal)) < 12)
            {
                npc.wanderTimer = 5 + float(Random() % 6);
                npc.wanderGoal = npc.local;
                for (int attempt = 0; attempt < 12; ++attempt)
                {
                    FarmPoint goal{36.f + float(Random() % 560), 36.f + float(Random() % 560)};
                    if (context.walkable(origin.x + goal.x, origin.y + goal.y, 16))
                    {
                        npc.wanderGoal = goal;
                        break;
                    }
                }
            }
            MoveTo(npc, npc.wanderGoal, dt, context);
        }
        if (auto* enemy = context.scene.Get<NpcActor>(npc.aggressor))
        {
            if (Alive(*enemy) && Length(Minus(enemy->Position(), npc.Position())) < 48
                && npc.cooldown <= 0)
            {
                BeginMonsterAttack(
                    npc, enemy->Id(), false, npc.fieldBoss ? 18.f : 7.f, enemy->Position());
                npc.cooldown = 1;
            }
        }
        return;
    }
    if (auto* target = context.scene.Get<NpcActor>(npc.target))
    {
        if (Alive(*target))
        {
            MoveTo(npc, Minus(target->Position(), origin), dt, context);
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
        destination = npc.job == NpcJob::Recorder && npc.storyResident ? FarmPoint{0, 70}
                      : npc.job == NpcJob::Recorder                    ? npc.home
                                                                       : FarmPoint{0, 420};
    }
    else if (npc.action == NpcAction::Rest)
    {
        destination = npc.homeSlot >= 0 ? npc.home : FarmPoint{0, -60};
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
    if (npc.settlementResident)
    {
        destination = npc.action == NpcAction::Rest || npc.courier ? npc.home
                      : npc.action == NpcAction::Trade             ? FarmPoint{-15, 0}
                      : npc.action == NpcAction::Meditate
                          ? FarmPoint{0, 420}
                          : FarmPoint{0, std::sin(float(npc.Id()) + clock * .03f) * 650};
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
    UpdateSettlements(dt, context);
    std::vector<WorldIncident> reports;
    for (auto& incident : incidents)
    {
        if (incident.type != "murder" || incident.rumorIssued || clock - incident.time < 3)
        {
            continue;
        }
        incident.rumorIssued = true;
        auto* witness = context.scene.Get<NpcActor>(incident.witness);
        if (!witness || !context.scene.Active(witness->Id()) || !Alive(*witness))
        {
            continue;
        }
        WorldIncident rumor = incident;
        rumor.type = "rumor";
        rumor.sourceId = incident.id;
        rumor.text = witness->Name() + "의 소문: “" + incident.actorName + "이(가) "
                     + incident.victimName + "을(를) 쓰러뜨렸대.” (목격담·미검증)";
        reports.push_back(std::move(rumor));
    }
    for (auto& report : reports)
    {
        RecordIncident(std::move(report));
    }
    strikeCooldown_ = std::max(0.f, strikeCooldown_ - dt);
    summaryTimer_ += dt;
    if (summaryTimer_ >= 5)
    {
        summaryTimer_ = 0;
        std::ostringstream summary;
        summary << "time=" << clock << "; trades=" << trades << "; raids=" << raids
                << "; hunts=" << hunts << "; player_gold=" << gold << "; quest_step=" << QuestStep()
                << "; supplies=" << story.suppliesDelivered << "; npcs=";
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

TownNews LivingWorld::MakeNews(const TownActor& settlement, GameplayContext& context) const
{
    TownNews news;
    news.known = true;
    news.source = settlement.index;
    news.time = clock;
    int count = 0, raiders = 0;
    for (auto* npc : context.scene.Query<NpcActor>())
    {
        if (npc->Parent() != settlement.Id() || npc->job == NpcJob::Wraith)
        {
            continue;
        }
        ++count;
        news.wealth += npc->gold;
        news.insight += npc->insight;
        raiders += npc->offender || npc->raidWeight > .8f ? 1 : 0;
    }
    news.wealth = news.wealth / std::max(1, count) / 200.f + (settlement.index % 3) * .2f;
    news.insight = news.insight / std::max(1, count) / 20.f + (settlement.index % 4) * .15f;
    news.safety = std::max(.05f, 1.f - raiders * .045f);
    for (const auto& incident : incidents)
    {
        if (incident.village == settlement.Id() && incident.type == "murder"
            && clock - incident.time < 180)
        {
            news.safety = std::max(.05f, news.safety - .12f);
        }
    }
    news.text = settlement.Name() + ": 주민 " + std::to_string(count) + "명 / 치안 "
                + std::to_string(int(news.safety * 100)) + "% / 교역과 수련 소식";
    for (const auto& incident : incidents)
    {
        if (incident.village == settlement.Id() && incident.type == "rumor"
            && clock - incident.time < 180)
        {
            news.text += " / 목격 소문: " + incident.text;
            break;
        }
    }
    return news;
}

bool LivingWorld::BeginTravel(NpcActor& npc,
                              TownActor& target,
                              bool migrate,
                              GameplayContext& context)
{
    if (npc.travelTown >= 0 || !Alive(npc) || target.index < 0)
    {
        return false;
    }
    int slot = -1;
    if (migrate)
    {
        for (int i = 0; i < 36; ++i)
        {
            if (!target.homes[i])
            {
                slot = i;
                break;
            }
        }
        if (slot < 0)
        {
            return false;
        }
        target.homes[slot] = npc.Id(); // Reserve before departing; release on failure/death.
    }
    auto* parent = context.scene.Get<TownActor>(npc.Parent());
    if (!parent)
    {
        return false;
    }
    auto delta = Minus(target.Position(), parent->Position());
    auto goal = migrate ? HomeDoor(slot) : FarmPoint{0, 0};
    goal = Add(delta, goal);
    float roadX = std::floor((npc.local.x + 90) / 640.f) * 640;
    float roadY = std::floor((npc.local.y + 90) / 640.f) * 640;
    float targetRoadX = std::floor((goal.x + 90) / 640.f) * 640;
    npc.travelRoute = {
        {roadX, npc.local.y}, {roadX, roadY}, {targetRoadX, roadY}, {targetRoadX, goal.y}, goal};
    npc.travelTown = target.index;
    npc.travelSlot = slot;
    npc.migrating = migrate;
    npc.travelStep = 0;
    npc.target = NoActor;
    npc.path.clear();
    if (migrate)
    {
        WorldIncident incident;
        incident.type = "migration_depart";
        incident.actor = npc.Id();
        incident.actorName = npc.Name();
        incident.village = parent->Id();
        incident.text =
            npc.Name() + ": 전달받은 소식과 성향에 따라 " + target.Name() + "로 이사 출발";
        RecordIncident(std::move(incident));
    }
    return true;
}

bool LivingWorld::Travel(NpcActor& npc, float dt, GameplayContext& context)
{
    if (npc.travelTown < 0)
    {
        return false;
    }
    auto* destination = Village(context.scene, npc.travelTown);
    auto* source = context.scene.Get<TownActor>(npc.Parent());
    if (!destination || !source || !Alive(npc))
    {
        if (destination && npc.travelSlot >= 0)
        {
            destination->homes[npc.travelSlot] = NoActor;
        }
        npc.travelTown = -1;
        npc.travelSlot = -1;
        npc.migrating = false;
        npc.travelRoute.clear();
        npc.letter = {};
        return false;
    }
    npc.action = NpcAction::Patrol;
    if (npc.travelStep < npc.travelRoute.size())
    {
        auto goal = npc.travelRoute[npc.travelStep];
        if (Length(Minus(npc.local, goal)) <= 8)
        {
            ++npc.travelStep;
            npc.path.clear();
        }
        else
        {
            auto step = Times(Unit(Minus(goal, npc.local)),
                              std::min(110.f * dt, Length(Minus(goal, npc.local))));
            auto candidate = Add(npc.Position(), step);
            if (Navigation::Sight(npc.Position(), candidate, context.walkable, 11))
            {
                npc.SetPosition(candidate);
            }
            else
            {
                MoveTo(npc, goal, dt, context);
            }
        }
        return true;
    }
    bool migrated = npc.migrating;
    npc.travelTown = -1;
    npc.travelRoute.clear();
    if (migrated)
    {
        if (npc.homeSlot >= 0 && source->homes[npc.homeSlot] == npc.Id())
        {
            source->homes[npc.homeSlot] = NoActor;
        }
        context.scene.Reparent(npc.Id(), destination->Id(), true);
        npc.homeSlot = npc.travelSlot;
        npc.home = HomeDoor(npc.homeSlot);
        npc.settlementResident = true;
        npc.travelSlot = -1;
        npc.migrating = false;
        npc.migrationCooldown = 180;
        ++migrations;
        WorldIncident incident;
        incident.type = "migration_arrive";
        incident.actor = npc.Id();
        incident.actorName = npc.Name();
        incident.village = destination->Id();
        incident.text = npc.Name() + ": " + destination->Name() + "의 "
                        + std::to_string(npc.homeSlot + 1) + "번 집으로 전입 완료";
        RecordIncident(std::move(incident));
    }
    else if (npc.letter.known)
    {
        auto letter = npc.letter;
        destination->news[letter.source] = letter;
        ++lettersDelivered;
        WorldIncident incident;
        incident.type = "letter_delivered";
        incident.actor = npc.Id();
        incident.actorName = npc.Name();
        incident.village = destination->Id();
        incident.text =
            npc.Name() + "이(가) " + destination->Name() + "에 편지를 전달: " + letter.text;
        RecordIncident(std::move(incident));
        npc.letter = {};
        BeginTravel(npc, *source, false, context);
    }
    return true;
}

void LivingWorld::UpdateSettlements(float dt, GameplayContext& context)
{
    auto people = context.scene.Query<NpcActor>();
    for (auto* npc : people)
    {
        npc->migrationCooldown = std::max(0.f, npc->migrationCooldown - dt);
    }
    for (auto* settlement : context.scene.Query<TownActor>())
    {
        if (settlement->index < 0)
        {
            continue;
        }
        settlement->news[settlement->index] = MakeNews(*settlement, context);
        settlement->dispatchTimer -= dt;
        settlement->migrationTimer -= dt;
        if (settlement->dispatchTimer <= 0)
        {
            settlement->dispatchTimer = 25;
            std::vector<TownActor*> neighbors;
            for (auto* other : context.scene.Query<TownActor>())
            {
                if (other != settlement && other->index >= 0
                    && Length(Minus(other->Position(), settlement->Position())) <= 5800)
                {
                    neighbors.push_back(other);
                }
            }
            for (auto* npc : people)
            {
                if (npc->Parent() == settlement->Id() && npc->courier && npc->travelTown < 0
                    && Alive(*npc) && !neighbors.empty())
                {
                    auto* destination = neighbors[Random() % neighbors.size()];
                    npc->letter = settlement->news[settlement->index];
                    if (BeginTravel(*npc, *destination, false, context))
                    {
                        ++lettersSent;
                        WorldIncident incident;
                        incident.type = "letter_sent";
                        incident.actor = npc->Id();
                        incident.actorName = npc->Name();
                        incident.village = settlement->Id();
                        incident.text =
                            settlement->Name() + " → " + destination->Name() + ": 전령 편지 발송";
                        RecordIncident(std::move(incident));
                    }
                    break;
                }
            }
        }
        if (settlement->migrationTimer > 0)
        {
            continue;
        }
        settlement->migrationTimer = 15;
        for (auto* npc : people)
        {
            if (npc->Parent() != settlement->Id() || npc->job == NpcJob::Wraith || npc->courier
                || npc->storyResident || npc->travelTown >= 0 || !Alive(*npc)
                || npc->migrationCooldown > 0)
            {
                continue;
            }
            auto score = [&](const TownNews& report)
            {
                return npc->wealthWeight * report.wealth + npc->survivalWeight * report.safety
                       + npc->insightWeight * report.insight
                       + npc->raidWeight * (1 - report.safety);
            };
            float best = score(settlement->news[settlement->index]) + .08f;
            TownActor* target = nullptr;
            for (const auto& report : settlement->news)
            {
                if (report.known && report.source != settlement->index && clock - report.time < 300
                    && score(report) > best)
                {
                    auto* candidate = Village(context.scene, report.source);
                    if (candidate
                        && std::find(candidate->homes.begin(), candidate->homes.end(), NoActor)
                               != candidate->homes.end())
                    {
                        best = score(report);
                        target = candidate;
                    }
                }
            }
            if (target && BeginTravel(*npc, *target, true, context))
            {
                break; // Bound departures so a single letter cannot empty a village.
            }
        }
    }
}
