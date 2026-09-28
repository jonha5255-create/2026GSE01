#include "stdafx.h"
#include "PrototypeWorld.h"
#include <algorithm>

namespace
{
    Ink RoleColor(NpcJob job)
    {
        switch (job)
        {
        case NpcJob::Hunter:
            return Color(113, 199, 163);
        case NpcJob::Raider:
            return Color(234, 103, 117);
        case NpcJob::Merchant:
            return Color(244, 195, 106);
        case NpcJob::Seeker:
            return Color(174, 142, 243);
        case NpcJob::Guard:
            return Color(111, 182, 243);
        case NpcJob::Recorder:
            return Color(230, 222, 201);
        default:
            return Color(217, 87, 150);
        }
    }
} // namespace

void PrototypeWorld::Draw(const NpcActor& npc)
{
    auto& r = *activeRenderer_;
    Point p = Project(npc.Position().x, npc.Position().y);
    if (p.x < -120 || p.x > r.Width() + 120 || p.y < -120 || p.y > r.Height() + 140)
    {
        return;
    }
    float z = zoom_;
    Ink color = RoleColor(npc.job);
    r.Ellipse(p, 14 * z, 5 * z, Color(0, 0, 0, .65f));
    if (npc.health <= 0)
    {
        r.Line({p.x - 12 * z, p.y - 5 * z}, {p.x + 12 * z, p.y - 5 * z}, 7 * z, color);
        r.Text(p.x - 24 * z,
               p.y - 24 * z,
               "회복 " + std::to_string(int(npc.recovery)),
               color,
               .48f * z);
        return;
    }
    float gait = std::sin(time_ * 8 + float(npc.Id())) * 3 * z;
    r.Line({p.x - 4 * z, p.y - 12 * z}, {p.x - 6 * z + gait, p.y}, 4 * z, Color(27, 34, 43));
    r.Line({p.x + 4 * z, p.y - 12 * z}, {p.x + 6 * z - gait, p.y}, 4 * z, Color(27, 34, 43));
    r.Quad({p.x - 9 * z, p.y - 34 * z},
           {p.x + 9 * z, p.y - 34 * z},
           {p.x + 11 * z, p.y - 10 * z},
           {p.x - 11 * z, p.y - 10 * z},
           color);
    r.Ellipse({p.x, p.y - 42 * z},
              7 * z,
              9 * z,
              npc.job == NpcJob::Wraith ? Color(69, 29, 59) : Color(211, 184, 165));
    r.Rect(p.x - 8 * z, p.y - 50 * z, 16 * z, 5 * z, Color(26, 29, 38));
    if (npc.job == NpcJob::Hunter || npc.job == NpcJob::Guard || npc.job == NpcJob::Raider)
    {
        r.Line({p.x + 11 * z, p.y - 30 * z},
               {p.x + 22 * z, p.y - 48 * z},
               3 * z,
               Emissive(color, 1.5f));
    }
    if (npc.job == NpcJob::Merchant)
    {
        r.Rect(p.x - 16 * z, p.y - 23 * z, 9 * z, 12 * z, Color(128, 91, 52));
    }
    r.Text(p.x - 25 * z, p.y - 84 * z, npc.Name(), color, .55f * z);
    r.Text(p.x - 25 * z,
           p.y - 68 * z,
           LivingWorld::ActionName(npc.action),
           Color(205, 214, 220),
           .43f * z);
    r.Rect(p.x - 17 * z, p.y - 54 * z, 34 * z, 3 * z, Color(31, 31, 42));
    r.Rect(p.x - 17 * z, p.y - 54 * z, 34 * z * npc.health / npc.maximumHealth, 3 * z, color);
}

void PrototypeWorld::Draw(const TownSiteActor& site)
{
    auto& r = *activeRenderer_;
    Point p = Project(site.Position().x, site.Position().y);
    if (p.x < -180 || p.x > r.Width() + 180 || p.y < -150 || p.y > r.Height() + 160)
    {
        return;
    }
    float z = zoom_;
    Ink ink = site.site == TownSite::Shrine   ? Color(179, 118, 231)
              : site.site == TownSite::Clinic ? Color(114, 223, 171)
                                              : Color(235, 185, 107);
    r.Ellipse(p, 30 * z, 12 * z, Color(28, 42, 45));
    r.Glow({p.x, p.y - 18 * z}, 45 * z, ink);
    r.Rect(p.x - 17 * z, p.y - 30 * z, 34 * z, 29 * z, Color(47, 48, 53));
    r.Quad({p.x - 23 * z, p.y - 32 * z},
           {p.x + 23 * z, p.y - 32 * z},
           {p.x + 17 * z, p.y - 47 * z},
           {p.x - 17 * z, p.y - 47 * z},
           ink);
    r.Line({p.x - 11 * z, p.y - 18 * z}, {p.x + 11 * z, p.y - 18 * z}, 2 * z, ink);
    if (site.site == TownSite::Clinic)
    {
        r.Line({p.x, p.y - 26 * z}, {p.x, p.y - 10 * z}, 3 * z, ink);
    }
    r.Text(p.x - 40 * z, p.y + 9 * z, LivingWorld::SiteName(site.site), ink, .62f * z);
    if (Length(site.Position()) < 72)
    {
        r.Text(p.x - 18 * z, p.y + 27 * z, "E 조사", Color(245, 243, 222), .53f * z);
    }
}

void PrototypeWorld::LivingHud(PrototypeRenderer& r)
{
    const auto& sim = level_.society;
    const auto& player = level_.Player();
    float scale = std::min(1.f, std::min(r.Width() / 1280.f, r.Height() / 800.f));
    r.SetScale(scale);
    float w = r.Width() / scale, h = r.Height() / scale;
    Ink panel = Color(8, 15, 24, .94f), white = Color(226, 235, 229);
    Ink muted = Color(148, 167, 181), gold = Color(242, 195, 110);
    Ink cyan = Color(104, 227, 196), red = Color(241, 112, 132);
    r.Rect(0, 0, w, 91, panel);
    r.Text(24, 10, "MAP 01 / 뒤틀린 상업지구", muted, .67f);
    r.Text(24, 33, "잔명 마을", white, 1.4f);
    r.Text(260, 16, "윤서진  LV." + std::to_string(player.level), cyan, .8f);
    r.Rect(260, 49, 205, 9, Color(49, 29, 43));
    r.Rect(260, 49, 205 * player.health / player.MaximumHealth(), 9, red);
    r.Text(260,
           66,
           "체력 " + std::to_string(int(player.health)) + "/"
               + std::to_string(int(player.MaximumHealth())),
           muted,
           .6f);
    r.Text(
        500, 16, std::to_string(sim.gold) + "전   잔향석 " + std::to_string(sim.goods), gold, .87f);
    r.Text(500,
           49,
           sim.story.mukContracted ? "묵 / 검 계약  동조율 " + std::to_string(sim.story.mukSync)
                                   : "임시 영혼포 / Q로 자동 조준",
           cyan,
           .7f);
    r.Text(w - 290, 16, sim.pk ? "PK 허용 / F 공격" : "PK 차단 / P 전환", sim.pk ? red : cyan, .8f);
    r.Text(w - 290,
           49,
           "수배 " + std::to_string(sim.notoriety) + "  /  마을 시간 "
               + std::to_string(int(sim.clock)) + "초",
           muted,
           .7f);

    r.Rect(20, 109, 340, 116, panel);
    r.Text(34, 120, "사라진 이름 / 하린의 흔적", gold, .8f);
    r.Text(34, 151, sim.Objective(), white, .71f);
    r.Text(34, 182, sim.Hint(level_.Graph()), cyan, .57f);
    r.Text(34, 204, "E 조사  /  Q 사냥  /  TAB 주민 목록", muted, .56f);

    r.Rect(20, 240, 340, 151, panel);
    r.Text(34, 251, "마을 소식 / 실시간", cyan, .72f);
    int line = 0;
    for (const auto& event : sim.events)
    {
        r.Text(34, 279 + line * 20.f, event, muted, .46f);
        ++line;
    }

    float mx = w - 175, my = 215;
    r.Rect(w - 328, 109, 308, 222, panel);
    r.Text(w - 314, 120, "마을 지도 / 화면 밖에서도 생활 지속", cyan, .65f);
    auto mini = [&](FarmPoint local)
    {
        return Point{mx + (local.x - local.y) * .21f, my + (local.x + local.y - 440) * .105f};
    };
    Point a = mini({0, 0}), b = mini({460, 0}), c = mini({460, 460}), d = mini({0, 460});
    r.Line(a, b, 2, Color(67, 77, 85));
    r.Line(b, c, 2, Color(67, 77, 85));
    r.Line(c, d, 2, Color(67, 77, 85));
    r.Line(d, a, 2, Color(67, 77, 85));
    for (auto* site : level_.Graph().Query<TownSiteActor>())
    {
        Point p = mini(site->local);
        r.Rect(p.x - 3, p.y - 3, 6, 6, gold);
    }
    for (auto* npc : level_.Graph().Query<NpcActor>())
    {
        if (npc->health > 0)
        {
            r.Ellipse(mini(npc->local), 3, 3, RoleColor(npc->job));
        }
    }
    auto origin = sim.Center(level_.Graph());
    auto pp = mini({-origin.x, -origin.y});
    pp.x = std::max(w - 315, std::min(w - 35, pp.x));
    pp.y = std::max(149.f, std::min(302.f, pp.y));
    r.Ellipse(pp, 5, 5, white);
    r.Text(w - 314, 305, "흰색: 서진 / 금색: 시설 / 분홍: 잔향귀", muted, .55f);
    if (sim.panel)
    {
        r.Rect(w - 328, 341, 308, 284, panel);
        r.Text(w - 314, 353, "주민 / 행동 / HP / 자산 / 깨달음", cyan, .61f);
        int row = 0;
        for (auto* npc : level_.Graph().Query<NpcActor>())
        {
            if (npc->job == NpcJob::Wraith)
            {
                continue;
            }
            r.Text(w - 314,
                   383 + row * 20.f,
                   npc->Name() + " " + LivingWorld::ActionName(npc->action) + " "
                       + std::to_string(int(npc->health)) + " " + std::to_string(npc->gold) + "전 "
                       + std::to_string(npc->insight),
                   RoleColor(npc->job),
                   .55f);
            ++row;
        }
    }
    r.Rect(20, h - 148, w - 40, 88, panel);
    r.Text(34,
           h - 139,
           "거래 " + std::to_string(sim.trades) + " / 사냥 " + std::to_string(sim.hunts)
               + " / 약탈 " + std::to_string(sim.raids)
               + "   |   주민은 생존·재산·깨달음 목표로 행동합니다.",
           gold,
           .66f);
    r.Text(34, h - 106, sim.dialogue, white, .66f);
    r.Rect(0, h - 48, w, 48, panel);
    r.Text(22,
           h - 40,
           "WASD 이동   E 조사/대화   Q 영혼포   J 묵 검격   T 판매   Y 회복 구입   P PK 전환   F "
           "인간 공격   R 새 시작",
           muted,
           .63f);
    r.Text(22,
           h - 18,
           "TAB 주민 목록   C 사거리   H 후처리   B 블룸   N 비네트   M 흐림   ESC 종료   /   "
           "구호소 E: 치료 또는 수배 배상",
           cyan,
           .57f);
    if (level_.state == RunState::Lost)
    {
        r.Rect(w * .5f - 210, h * .45f - 35, 420, 80, panel);
        r.Text(w * .5f - 185, h * .45f - 20, "쓰러졌습니다 / R로 새 세션", red, 1);
        r.Text(w * .5f - 185, h * .45f + 13, "주민들의 생활은 계속됩니다.", white, .7f);
    }
    r.SetScale(1);
}
