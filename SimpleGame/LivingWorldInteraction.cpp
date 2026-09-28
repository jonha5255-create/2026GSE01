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
    if (site == TownSite::Home)
    {
        story.homeInvestigated = true;
        dialogue = "서진: 사진 속 하린의 얼굴만 지워졌어. 학교 기록함을 확인해야겠어.";
    }
    else if (site == TownSite::School)
    {
        if (!story.homeInvestigated)
        {
            dialogue = "잠긴 기록함. 먼저 서진의 집에서 하린의 흔적을 조사하세요.";
        }
        else
        {
            story.hasHarinStudentCard = true;
            dialogue = "하린의 학생증을 찾았다. 존재하지 않는 학생이라니... 백연에게 보여주자.";
        }
    }
    else if (site == TownSite::Archive)
    {
        if (story.hasHarinStudentCard)
        {
            story.metRecorder = true;
            dialogue = "백연: 기억관리국이 이름을 지웠군요. 남쪽 기억의 제단에서 묵을 찾아보세요.";
        }
        else
        {
            dialogue =
                "백연: 집과 학교 기록함을 조사하세요. 이름이 지워져도 물건에는 기억이 남아요.";
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
            dialogue = "묵: 하린을 함께 찾자. 나는 네 검이 될게. J로 검격, 사냥으로 동조율을 높여.";
        }
        else
        {
            dialogue =
                "묵: 이 도시에서 살아가는 이들을 지켜봐. 다음 흔적은 존재하지 않는 역에 있어.";
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
    for (auto* site : context.scene.Query<TownSiteActor>())
    {
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
        AdvanceStory(nearest->site);
    }
    return true;
}

void LivingWorld::PlayerTrade(bool buy, GameplayContext& context)
{
    if (context.state == RunState::Lost)
    {
        return;
    }
    if (Length(Minus(context.player.Position(), Center(context.scene))) > 100)
    {
        dialogue = "거래는 잔명 시장 근처에서만 가능합니다.";
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
        return;
    }
    dialogue = "거래 불가: 체력, 잔향석, 소지금 또는 살아 있는 상인의 재고/자금을 확인하세요.";
}

void LivingWorld::TogglePK()
{
    pk = !pk;
    dialogue = pk ? "PK 허용: F로 가까운 인간을 공격합니다. 경비의 추적과 배상 책임이 발생합니다."
                  : "PK 해제: F 인간 공격이 차단됩니다. 기존 적대와 수배는 남습니다.";
}

void LivingWorld::PlayerStrike(bool human, GameplayContext& context)
{
    if (context.state == RunState::Lost || strikeCooldown_ > 0)
    {
        return;
    }
    if ((human && !pk) || (!human && !story.mukContracted))
    {
        dialogue =
            human ? "인간 공격은 P로 PK를 허용한 뒤 F를 누르세요."
                  : "검격은 묵과 계약한 뒤 사용할 수 있습니다. 먼저 Q의 임시 영혼포로 사냥하세요.";
        return;
    }
    NpcActor* target = nullptr;
    float nearest = 85;
    for (auto* npc : context.scene.Query<NpcActor>())
    {
        float distance = Length(npc->Position());
        if (Alive(*npc) && (human ? npc->job != NpcJob::Wraith : npc->job == NpcJob::Wraith)
            && distance < nearest && Navigation::Sight({}, npc->Position(), context.walkable))
        {
            nearest = distance;
            target = npc;
        }
    }
    if (!target)
    {
        dialogue = "공격할 대상이 사거리 안에 없습니다.";
        return;
    }
    Damage(*target, 24 + context.player.weaponRank * 3.f, nullptr, context);
    context.Effect(target->Position(), human ? "결투" : "묵 / 검격", .65f);
    strikeCooldown_ = .5f;
}

std::string LivingWorld::Objective() const
{
    if (!story.homeInvestigated)
    {
        return "1 / 서진의 집 조사 [E]";
    }
    if (!story.hasHarinStudentCard)
    {
        return "2 / 학교 기록함에서 학생증 찾기 [E]";
    }
    if (!story.metRecorder)
    {
        return "3 / 백연의 기록소에 학생증 전달 [E]";
    }
    if (!story.mukContracted)
    {
        return "4 / 남쪽 제단에서 묵과 계약 [E]";
    }
    if (story.hunted < 3)
    {
        return "5 / 잔향귀 3체 사냥: " + std::to_string(story.hunted) + "/3";
    }
    return "기억의 실마리 확보 / 자유 탐험과 주민 생활";
}

std::string LivingWorld::Hint(const SceneGraph& graph) const
{
    TownSite goal = !story.homeInvestigated      ? TownSite::Home
                    : !story.hasHarinStudentCard ? TownSite::School
                    : !story.metRecorder         ? TownSite::Archive
                                                 : TownSite::Shrine;
    for (auto* site : graph.Query<TownSiteActor>())
    {
        if (site->site == goal)
        {
            auto p = site->Position();
            // Isometric screen directions correspond to WASD controls.
            float x = p.x - p.y, y = (p.x + p.y) * .5f;
            std::string direction = std::abs(x) > std::abs(y) ? (x > 0 ? "D 오른쪽" : "A 왼쪽")
                                                              : (y > 0 ? "S 아래쪽" : "W 위쪽");
            return std::string(SiteName(goal)) + " / " + std::to_string(int(Length(p))) + "m / "
                   + (Length(p) < InteractionRange ? "E 상호작용" : direction);
        }
    }
    return "";
}
