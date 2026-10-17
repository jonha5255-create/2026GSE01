#include "stdafx.h"
#include "LivingWorld.h"
#include "GameplayContext.h"
#include "GameplayActors.h"
#include "RenderDiagnostics.h"
#include <algorithm>

namespace
{
    constexpr float InteractionRange = 72;

    bool Alive(const NpcActor& npc)
    {
        return npc.health > 0 && npc.recovery <= 0;
    }
} // namespace

void LivingWorld::AdvanceStory(TownSite site)
{
    if (site == TownSite::Relay || site == TownSite::Signal || site == TownSite::Station)
    {
        if (site == TownSite::Relay && story.suppliesDelivered >= 2)
        {
            story.relayAligned = true;
            dialogue = "묵: 중계기에서 하린의 목소리가 들려. 동쪽 신호 장치를 따라가자.";
        }
        else if (site == TownSite::Signal && story.relayAligned)
        {
            story.signalDecoded = true;
            dialogue =
                "하린의 기록: 이름을 빼앗겨도 나는 여기 있어. 더 동쪽, 지워진 역에서 기다릴게.";
        }
        else if (site == TownSite::Station && story.signalDecoded)
        {
            if (!story.gateOpened)
            {
                story.gateOpened = true;
                dialogue = "문지기가 깨어났다! 요괴를 무기화하고 문지기를 제압하세요.";
            }
            else if (story.guardianDefeated)
            {
                story.echoRecovered = true;
                dialogue =
                    "하린: 나를 기억해 줘. 묵과 함께라면 길을 열 수 있어. 기억의 제단으로 "
                    "돌아가자.";
            }
            else
            {
                dialogue = "문지기가 기록 장치를 막고 있습니다. 먼저 문지기를 제압하세요.";
            }
        }
        else
        {
            dialogue = "신호가 잠겨 있습니다. 현재 퀘스트의 증거와 보급을 먼저 준비하세요.";
        }
        Note(dialogue, "story");
        return;
    }
    if (site == TownSite::Home)
    {
        story.homeInvestigated = true;
        dialogue = "서진: 사진 속 하린의 얼굴만 지워졌어. 학교 기록함을 확인해야겠어.";
    }
    else if (site == TownSite::School)
    {
        if (!story.homeInvestigated)
        {
            dialogue = "잠긴 기록함. 먼저 야외 사진 흔적을 조사하세요.";
        }
        else
        {
            story.hasHarinStudentCard = true;
            dialogue = "하린의 학생증을 찾았다. 존재하지 않는 학생이라니... 백연에게 보여주자.";
        }
    }
    else if (site == TownSite::Archive)
    {
        if (story.weaponAwakened && !story.caseClosed)
        {
            story.caseClosed = true;
            gold += 80;
            dialogue = "백연: 하린은 살아 있어요. 기억을 지우는 자들의 흔적을 찾았군요. 보상 80전.";
        }
        else if (story.mukContracted && story.hunted >= 3 && !story.reported)
        {
            story.reported = true;
            gold += 20;
            dialogue =
                "백연: 잔향석에 동쪽 역의 좌표가 있어요. 시장에서 2개를 판매해 원정 보급을 "
                "마련하세요. 보상 20전.";
        }
        else if (story.reported)
        {
            dialogue =
                "백연: 원정에서 하린의 기억을 회수하세요. 돌아온 뒤 묵을 각성시키면 진실을 읽을 수 "
                "있어요.";
        }
        else if (story.hasHarinStudentCard)
        {
            story.metRecorder = true;
            dialogue = "백연: 기억관리국이 이름을 지웠군요. 남쪽 기억의 제단에서 묵을 찾아보세요.";
        }
        else
        {
            dialogue = "백연: 야외 사진 흔적과 학교 기록함을 조사하세요. 물건에는 기억이 남아요.";
        }
    }
    else if (site == TownSite::Shrine)
    {
        if (!story.metRecorder)
        {
            dialogue = "희미한 목소리: 나를 깨울 기억의 증거를 찾아 줘. 백연이라면 알 거야.";
        }
        else if (!story.mukContracted)
        {
            story.mukContracted = true;
            story.mukSync = 10;
            dialogue =
                "묵: 하린을 함께 찾자. 적을 왼쪽 클릭하면 검격을 쓸 수 있어. 사냥으로 동조율을 "
                "높여.";
        }
        else
        {
            dialogue =
                "묵: 잔향귀의 잔향석을 모으면 하린의 흔적을 읽을 수 있어. 3체 사냥 후 백연을 "
                "만나자.";
        }
    }
    Note(dialogue, "story");
}

bool LivingWorld::Interact(GameplayContext& context)
{
    if (context.state == RunState::Lost)
    {
        dialogue = "쓰러졌습니다. R로 새 세션을 시작하세요. 주민들의 시간은 계속 흐릅니다.";
        return true;
    }
    TownSiteActor* nearest = nullptr;
    float distance = InteractionRange;
    auto* questSite = context.scene.Get<TownSiteActor>(QuestTarget(context.scene));
    if (questSite && Length(questSite->Position()) < InteractionRange)
    {
        nearest = questSite;
        distance = Length(questSite->Position());
    }
    for (auto* site : context.scene.Query<TownSiteActor>())
    {
        if (questSite && nearest == questSite)
        {
            break;
        }
        float candidate = Length(site->Position());
        if (candidate < distance)
        {
            distance = candidate;
            nearest = site;
        }
    }
    if (!nearest)
    {
        NpcActor* speaker = nullptr;
        for (auto* npc : context.scene.Query<NpcActor>())
        {
            float candidate = Length(npc->Position());
            if (Alive(*npc) && npc->job != NpcJob::Wraith && candidate < distance)
            {
                distance = candidate;
                speaker = npc;
            }
        }
        if (speaker)
        {
            dialogue = speaker->Name() + ": 나는 " + JobName(speaker->job) + ". 지금은 "
                       + ActionName(speaker->action) + " 중이야. 가진 돈은 "
                       + std::to_string(speaker->gold) + "전, 깨달음은 "
                       + std::to_string(speaker->insight) + ".";
        }
        else
        {
            dialogue =
                "가까운 시설이나 주민에게 다가가 E를 누르세요. TAB으로 주민 상태를 확인하세요.";
        }
        return true;
    }
    if (nearest->site == TownSite::Market)
    {
        dialogue =
            "시장: T로 잔향석 1개 판매(8전), Y로 회복 재료 구입(15전). 상인의 재고와 자금이 "
            "필요합니다.";
    }
    else if (nearest->site == TownSite::Clinic)
    {
        if (notoriety > 0 && gold >= 30)
        {
            gold -= 30;
            notoriety = 0;
            for (auto* npc : context.scene.Query<NpcActor>())
            {
                npc->playerHostile = false;
            }
            dialogue = "구호소: 배상금 30전 납부. 적대와 수배가 해제되었습니다.";
        }
        else if (notoriety > 0)
        {
            dialogue = "구호소: 수배 해제에는 배상금 30전이 필요합니다.";
        }
        else
        {
            context.player.health = context.player.MaximumHealth();
            dialogue = "구호소: 치료가 끝났습니다. 주민끼리의 약탈도 조심하세요.";
        }
    }
    else
    {
        bool awakened = story.weaponAwakened;
        bool closed = story.caseClosed;
        if (nearest->site == TownSite::Shrine && story.echoRecovered && !awakened)
        {
            story.weaponAwakened = true;
            story.mukSync = std::min(100, story.mukSync + 20);
            context.player.weaponRank = std::min(8, context.player.weaponRank + 1);
            context.player.health = context.player.MaximumHealth();
            dialogue =
                "묵: 하린의 기억과 함께 성장했어. 무기 강화 +1, 동조율 +20. 백연에게 진실을 "
                "전하자.";
            Note(dialogue, "story");
        }
        else
        {
            AdvanceStory(nearest->site);
        }
        if (!closed && story.caseClosed)
        {
            context.player.AddExperience(60, context);
        }
        if (nearest->site == TownSite::Station && story.gateOpened && guardian == NoActor)
        {
            auto& guard =
                context.scene.Spawn<NpcActor>(town, "기억을 삼키는 문지기", NpcJob::Wraith);
            guard.local = guard.home = {2502, 0};
            guard.storyGuardian = true;
            guard.health = guard.maximumHealth = 240;
            guard.gold = 40;
            guard.goods = 3;
            guardian = guard.Id();
        }
    }
    return true;
}

void LivingWorld::PlayerTrade(bool buy, GameplayContext& context)
{
    if (context.state == RunState::Lost)
    {
        return;
    }
    bool nearMarket = false;
    for (auto* site : context.scene.Query<TownSiteActor>())
    {
        nearMarket |= site->site == TownSite::Market && Length(site->Position()) <= 100;
    }
    if (!nearMarket)
    {
        dialogue = "거래는 각 마을 시장 근처에서 가능합니다.";
        return;
    }
    for (auto* npc : context.scene.Query<NpcActor>())
    {
        if (npc->job != NpcJob::Merchant || !Alive(*npc) || Length(npc->Position()) > 110)
        {
            continue;
        }
        if (buy && gold >= 15 && npc->goods > 0
            && context.player.health < context.player.MaximumHealth())
        {
            gold -= 15;
            npc->gold += 15;
            --npc->goods;
            context.player.health =
                std::min(context.player.MaximumHealth(), context.player.health + 40);
        }
        else if (!buy && goods > 0 && npc->gold >= 8)
        {
            --goods;
            ++npc->goods;
            gold += 8;
            npc->gold -= 8;
        }
        else
        {
            continue;
        }
        ++trades;
        dialogue = buy ? "회복 재료 구입: 15전 지불, 체력 +40."
                       : "잔향석 판매: 1개를 넘기고 8전을 받았습니다.";
        Note(dialogue, "player_trade");
        if (!buy && story.reported && story.suppliesDelivered < 2)
        {
            ++story.suppliesDelivered;
            if (story.suppliesDelivered == 2)
            {
                dialogue =
                    "미령: 원정 보급을 마련했어요. 마을 동쪽 도로를 따라 동부 중계기로 가세요.";
                Note(dialogue, "story");
            }
        }
        return;
    }
    dialogue = "거래 불가: 체력, 잔향석, 소지금 또는 살아 있는 상인의 재고/자금을 확인하세요.";
}

void LivingWorld::TogglePK()
{
    pk = !pk;
    dialogue = pk ? "PK 허용: 인간을 왼쪽 클릭해 공격합니다. 경비의 추적과 배상 책임이 발생합니다."
                  : "PK 해제: 인간 공격이 차단됩니다. 기존 적대와 수배는 남습니다.";
}

void LivingWorld::PlayerStrike(bool human,
                               GameplayContext& context,
                               ActorId selected,
                               bool basicAttack)
{
    if (context.state == RunState::Lost || strikeCooldown_ > 0)
    {
        return;
    }
    WeaponActor* weapon = nullptr;
    for (auto* candidate : context.scene.Query<WeaponActor>())
    {
        if (candidate->Parent() == context.player.Id())
        {
            weapon = candidate;
            break;
        }
    }
    if (!weapon || weapon->shotTimer_ > 0)
    {
        return;
    }
    if ((human && !pk)
        || (!human && !story.mukContracted && !basicAttack && !context.player.weapon))
    {
        dialogue =
            human ? "인간 공격은 P로 PK를 허용한 뒤 대상을 왼쪽 클릭하세요."
                  : "검격은 묵과 계약한 뒤 사용할 수 있습니다. 먼저 Q의 임시 영혼포로 사냥하세요.";
        return;
    }
    NpcActor* target = nullptr;
    float nearest = context.player.StrikeRange();
    for (auto* npc : context.scene.Query<NpcActor>())
    {
        float distance = Length(npc->Position());
        if ((selected == NoActor || npc->Id() == selected) && Alive(*npc)
            && (human ? npc->job != NpcJob::Wraith : npc->job == NpcJob::Wraith)
            && distance < nearest && Navigation::Sight({}, npc->Position(), context.walkable))
        {
            nearest = distance;
            target = npc;
        }
    }
    if (!target)
    {
        dialogue = "공격 불가: 대상에게 가까이 다가가세요. 벽 너머는 공격할 수 없습니다.";
        return;
    }
    if (context.player.weapon && context.player.yokai == YokaiForm::Ranged)
    {
        if (weapon->ShootAt(*target, context))
        {
            strikeCooldown_ = context.player.Cooldown();
        }
        return;
    }
    float baseDamage = !human && !story.mukContracted ? 14.f : 24.f;
    float damage = context.player.weapon ? context.player.StrikeDamage()
                                         : baseDamage + context.player.weaponRank * 3.f;
    Damage(*target, damage, nullptr, context);
    weapon->StartSwing(target->Position());
    context.Effect(target->Position(),
                   context.player.weapon
                       ? (context.player.yokai == YokaiForm::Claw ? "클로 / 연격" : "검 / 참격")
                   : human               ? "결투"
                   : story.mukContracted ? "묵 / 검격"
                                         : "기본 공격",
                   .65f);
    strikeCooldown_ = context.player.StrikeCooldown();
    weapon->shotTimer_ = strikeCooldown_;
}

int LivingWorld::QuestStep() const
{
    if (!story.homeInvestigated)
    {
        return 1;
    }
    if (!story.hasHarinStudentCard)
    {
        return 2;
    }
    if (!story.metRecorder)
    {
        return 3;
    }
    if (!story.mukContracted)
    {
        return 4;
    }
    if (story.hunted < 3)
    {
        return 5;
    }
    if (!story.reported)
    {
        return 6;
    }
    if (story.suppliesDelivered < 2)
    {
        return 7;
    }
    if (!story.relayAligned)
    {
        return 8;
    }
    if (!story.signalDecoded)
    {
        return 9;
    }
    if (!story.gateOpened)
    {
        return 10;
    }
    if (!story.guardianDefeated)
    {
        return 11;
    }
    if (!story.echoRecovered)
    {
        return 12;
    }
    if (!story.weaponAwakened)
    {
        return 13;
    }
    if (!story.caseClosed)
    {
        return 14;
    }
    return 15;
}

bool LivingWorld::CombatQuest() const
{
    return QuestStep() == 5 || QuestStep() == 11;
}

std::string LivingWorld::Objective() const
{
    static const char* objectives[] = {"야외 사진 흔적 조사",
                                       "학교 기록함에서 학생증 찾기",
                                       "백연에게 학생증 전달",
                                       "기억의 제단에서 묵과 계약",
                                       "잔향귀 사냥",
                                       "백연에게 잔향석 조사 보고",
                                       "시장에서 원정 보급 마련",
                                       "동부 중계기 복구",
                                       "승강장의 신호 해독",
                                       "존재하지 않는 역 조사",
                                       "역의 문지기 제압",
                                       "역에서 하린의 기억 회수",
                                       "기억의 제단에서 묵 각성",
                                       "백연에게 하린의 기억 전달",
                                       "하린의 흔적 / 사건 해결"};
    int step = QuestStep();
    std::string result = objectives[step - 1];
    if (step == 5)
    {
        result += " " + std::to_string(story.hunted) + "/3";
    }
    if (step == 7)
    {
        result += " " + std::to_string(story.suppliesDelivered) + "/2";
    }
    return result;
}

std::string LivingWorld::QuestInstruction() const
{
    if (CombatQuest())
    {
        return "2 선택 → Q 무기화 → 괴물 왼쪽 클릭";
    }
    if (QuestStep() == 7)
    {
        return "시장 근처에서 T: 잔향석 2개 판매";
    }
    if (story.caseClosed)
    {
        return "원정 보상 획득! 거래와 탐험을 이어가세요.";
    }
    if (QuestStep() >= 8 && QuestStep() <= 12)
    {
        return "동쪽 도로를 따라 목표에서 E 조사";
    }
    return "금색 목표에 접근한 뒤 E로 진행하세요.";
}

ActorId LivingWorld::QuestTarget(const SceneGraph& graph) const
{
    int step = QuestStep();
    if (step == 15)
    {
        return NoActor;
    }
    if (step == 11)
    {
        return guardian;
    }
    if (step == 5)
    {
        ActorId target = NoActor;
        float distance = 1e20f;
        for (auto* npc : graph.Query<NpcActor>())
        {
            float candidate = Length(npc->Position());
            if (npc->job == NpcJob::Wraith && !npc->storyGuardian && Alive(*npc)
                && candidate < distance)
            {
                target = npc->Id();
                distance = candidate;
            }
        }
        return target;
    }
    TownSite goal = step == 1                              ? TownSite::Home
                    : step == 2                            ? TownSite::School
                    : step == 3 || step == 6 || step == 14 ? TownSite::Archive
                    : step == 7                            ? TownSite::Market
                    : step == 8                            ? TownSite::Relay
                    : step == 9                            ? TownSite::Signal
                    : step == 10 || step == 12             ? TownSite::Station
                                                           : TownSite::Shrine;
    for (auto* site : graph.Query<TownSiteActor>())
    {
        if (site->site == goal)
        {
            return site->Id();
        }
    }
    return NoActor;
}

std::string LivingWorld::Hint(const SceneGraph& graph) const
{
    if (story.caseClosed)
    {
        return "현재 프로토타입의 퀘스트를 완료했습니다.";
    }
    auto* target = graph.Get<Actor>(QuestTarget(graph));
    if (!target)
    {
        return "잔향귀가 다시 나타날 때까지 기다리세요.";
    }
    auto p = target->Position();
    float x = p.x - p.y, y = (p.x + p.y) * .5f;
    std::string direction = std::abs(x) > std::abs(y) ? (x > 0 ? "D 오른쪽" : "A 왼쪽")
                                                      : (y > 0 ? "S 아래쪽" : "W 위쪽");
    return std::to_string(int(Length(p))) + "m / "
           + (Length(p) < InteractionRange && !CombatQuest()
                  ? (QuestStep() == 7 ? "도착! T로 잔향석 판매" : "도착! E를 눌러 진행")
                  : direction + "으로 이동");
}
