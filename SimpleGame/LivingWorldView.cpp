#include "stdafx.h"
#include "PrototypeWorld.h"
#include <algorithm>

namespace
{
    // Clip map polygons on the CPU so the minimap remains in the existing UI batch.
    void MapQuad(PrototypeRenderer& r,
                 Point a,
                 Point b,
                 Point c,
                 Point d,
                 float left,
                 float top,
                 float right,
                 float bottom,
                 Ink ink)
    {
        Point polygon[12] = {a, b, c, d}, output[12];
        int count = 4;
        for (int edge = 0; edge < 4 && count; ++edge)
        {
            auto distance = [&](Point p)
            {
                return edge == 0   ? p.x - left
                       : edge == 1 ? right - p.x
                       : edge == 2 ? p.y - top
                                   : bottom - p.y;
            };
            int nextCount = 0;
            Point previous = polygon[count - 1];
            float previousDistance = distance(previous);
            for (int i = 0; i < count; ++i)
            {
                Point current = polygon[i];
                float currentDistance = distance(current);
                if ((previousDistance >= 0) != (currentDistance >= 0))
                {
                    float t = previousDistance / (previousDistance - currentDistance);
                    output[nextCount++] = {previous.x + (current.x - previous.x) * t,
                                           previous.y + (current.y - previous.y) * t};
                }
                if (currentDistance >= 0)
                {
                    output[nextCount++] = current;
                }
                previous = current;
                previousDistance = currentDistance;
            }
            count = nextCount;
            std::copy(output, output + count, polygon);
        }
        for (int i = 1; i + 1 < count; ++i)
        {
            r.Triangle(polygon[0], polygon[i], polygon[i + 1], ink);
        }
    }

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
    float z = zoom_ * (npc.storyGuardian || npc.fieldBoss ? 1.6f : 1.f);
    Ink color = RoleColor(npc.job);
    if (npc.fieldBoss)
    {
        color = Color(246, 132, 78);
        r.Ellipse(p, 30 * zoom_, 12 * zoom_, Color(206, 78, 37, .45f));
    }
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
    if (npc.job == NpcJob::Wraith)
    {
        float windup = npc.monsterForm == 1 ? .22f : npc.monsterForm == 2 ? .55f : .35f;
        auto ease = [](float value)
        {
            float t = std::max(0.f, std::min(1.f, value));
            return t * t * (3 - 2 * t);
        };
        float phase = npc.attackTime / windup;
        float progress = npc.attackTime < 0 ? 0
                         : phase < .65f     ? -.2f * ease(phase / .65f)
                         : phase < 1        ? -.2f + 1.2f * ease((phase - .65f) / .35f)
                                            : 1 - ease((npc.attackTime - windup) / .4f);
        auto heading = npc.attackTime >= 0 ? npc.attackDirection : npc.moveFacing;
        Point origin = Project(0, 0), direction = Project(heading.x * 12, heading.y * 12);
        p.x += (direction.x - origin.x) * progress;
        p.y += (direction.y - origin.y) * progress;
        float walk = npc.attackTime < 0 ? npc.moveBlend : 0.f;
        float bob = (npc.monsterForm == 0 ? std::sin(time_ * 2 + float(npc.Id())) * 1.8f
                                          : -.7f * std::abs(std::sin(npc.gaitPhase)) * walk)
                    * z;
        auto at = [&](float x, float y)
        {
            return Point{p.x + x * z, p.y + y * z + bob};
        };
        Ink hide = Color(62, 29, 61), bone = Color(190, 164, 181);
        auto limb = [&](Point a, Point joint, Point b, float width, Ink ink)
        {
            r.Line(a, joint, (width + 1.5f) * z, Color(23, 27, 29));
            r.Line(joint, b, (width + .5f) * z, Color(23, 27, 29));
            r.Line(a, joint, width * z, ink);
            r.Line(joint, b, std::max(1.f, width - 1) * z, ink);
            r.Ellipse(joint, width * .5f * z, width * .5f * z, ink);
        };
        if (npc.monsterForm == 1)
        {
            Ink fur = Color(105, 48, 38);
            for (int leg = 0; leg < 4; ++leg)
            {
                float x = leg < 2 ? -16.f : 15.f;
                float cycle = npc.gaitPhase + (leg == 0 || leg == 3 ? 0.f : 3.14159f);
                float stride = std::sin(cycle) * 7 * walk;
                float lift = std::max(0.f, std::cos(cycle)) * 5 * walk;
                limb(at(x, -22),
                     at(x - stride * .5f, -12),
                     at(x + stride, -2 - lift),
                     4,
                     leg % 2 ? fur : Color(59, 35, 32));
                r.Ellipse(at(x + stride + 2, -2 - lift), 5 * z, 2.4f * z, bone);
            }
            r.Ellipse(at(0, -24), 25 * z, 13 * z, Color(43, 29, 28));
            r.Ellipse(at(0, -25), 23 * z, 11 * z, fur);
            r.Ellipse(at(-5, -29), 15 * z, 5 * z, Color(133, 68, 49));
            float facing = direction.x >= origin.x ? 1.f : -1.f;
            r.Ellipse(at(facing * 23, -28), 13 * z, 10 * z, fur);
            r.Ellipse(at(facing * 31, -26), 9 * z, 5 * z, Color(161, 120, 83));
            r.Ellipse(at(facing * 38, -27), 3 * z, 3 * z, Color(27, 27, 28));
            r.Triangle(at(facing * 16, -33), at(facing * 19, -48), at(facing * 26, -34), bone);
            r.Line(
                at(facing * 23, -24), at(facing * 38, -24 + std::abs(progress) * 5), 4 * z, bone);
            r.Ellipse(at(facing * 26, -31), 3 * z, 2 * z, Emissive(Color(255, 170, 51), 2));
            limb(at(-facing * 20, -24),
                 at(-facing * 29, -29),
                 at(-facing * 37, -32 + std::sin(time_ * 3) * 3),
                 4,
                 fur);
        }
        else if (npc.monsterForm == 2)
        {
            Ink stone = Color(83, 102, 89);
            r.Quad(at(-23, -47), at(21, -49), at(18, -13), at(-18, -13), stone);
            r.Quad(at(-23, -47), at(-8, -52), at(4, -28), at(-18, -13), Color(104, 122, 102));
            r.Quad(at(4, -47), at(21, -49), at(18, -13), at(4, -20), Color(51, 71, 64));
            r.Ellipse(at(0, -56), 12 * z, 11 * z, Color(48, 64, 57));
            r.Quad(at(-9, -65), at(8, -65), at(11, -51), at(-10, -48), Color(121, 135, 109));
            for (int side = -1; side <= 1; side += 2)
            {
                float stride = std::sin(npc.gaitPhase + (side < 0 ? 3.14159f : 0)) * 5 * walk;
                limb(at(side * 12.f, -18),
                     at(side * 14.f + stride * .5f, -10),
                     at(side * 16.f + stride, -1),
                     10,
                     stone);
                r.Ellipse(at(side * 16.f + stride, -1), 8 * z, 4 * z, Color(110, 120, 101));
                float lift = npc.attackTime < 0 ? -stride
                             : phase < .65f     ? -22 * ease(phase / .65f)
                             : phase < 1        ? -22 + 34 * ease((phase - .65f) / .35f)
                                                : 12 * progress;
                limb(at(side * 22.f, -43),
                     at(side * 29.f, -34 + lift * .5f),
                     at(side * 32.f, -24 + lift),
                     11,
                     stone);
                r.Ellipse(at(side * 32.f, -24 + lift), 9 * z, 10 * z, Color(130, 133, 106));
            }
            r.Line(at(-7, -55), at(7, -55), 3 * z, Emissive(Color(147, 233, 92), 2));
            r.Line(at(0, -45), at(-7, -28), 2 * z, Color(34, 49, 42));
        }
        else
        {
            r.Glow(at(0, -25), 25 * z, Color(163, 43, 110, .3f));
            r.Quad(at(-15, -39), at(13, -42), at(10, -17), at(-12, -15), hide);
            r.Quad(at(-15, -39), at(-4, -44), at(0, -18), at(-12, -15), Color(93, 48, 83));
            r.Quad(at(3, -40), at(13, -42), at(10, -17), at(2, -22), Color(38, 26, 45));
            for (int i = 0; i < 4; ++i)
            {
                float x = -13 + i * 7.f;
                r.Triangle(at(x, -23), at(x + 8, -23), at(x + 3, -5 - (i % 2) * 5.f), hide);
            }
            r.Ellipse(at(0, -45), 10 * z, 11 * z, Color(39, 23, 42));
            for (int side = -1; side <= 1; side += 2)
            {
                r.Triangle(at(side * 6.f, -50), at(side * 16.f, -62), at(side * 11.f, -42), bone);
                limb(at(side * 13.f, -35),
                     at(side * 24.f, -26 - progress * 5),
                     at(side * 25.f, -10),
                     5,
                     hide);
                for (int finger = 0; finger < 3; ++finger)
                {
                    r.Line(at(side * (22.f + finger * 3), -12),
                           at(side * (23.f + finger * 4), -3 + finger * 2.f),
                           1.5f * z,
                           bone);
                }
                r.Line(at(side * 2.f, -46),
                       at(side * 7.f, -48),
                       2 * z,
                       Emissive(Color(255, 81, 136), 2));
            }
            r.Line(at(-4, -39), at(4, -39), 2 * z, Color(191, 75, 110));
            r.Quad(at(0, -34),
                   at(4, -28),
                   at(0, -20),
                   at(-4, -28),
                   Emissive(Color(238, 75, 158), 1.8f));
        }
        if (npc.attackTime >= 0)
        {
            Ink warning =
                npc.attackTime < windup ? Color(240, 163, 62, .7f) : Color(255, 88, 57, .8f);
            r.Ellipse(p, (npc.monsterForm == 2 ? 37 : 25) * z, 10 * z, Color(210, 83, 41, .15f));
            if (npc.attackTime < windup)
            {
                r.Text(p.x - 10 * z, p.y - 80 * z, "!", warning, 1.2f * z);
            }
            else
            {
                r.Line(at(-22, -29), at(26, npc.monsterForm == 2 ? 0.f : -16.f), 3 * z, warning);
                r.Line(at(-15, -34), at(30, npc.monsterForm == 2 ? 2.f : -21.f), 2 * z, warning);
            }
        }
    }
    else
    {
        float gait = std::sin(npc.gaitPhase) * 3 * z * npc.moveBlend;
        r.Line({p.x - 4 * z, p.y - 12 * z}, {p.x - 6 * z + gait, p.y}, 4 * z, Color(27, 34, 43));
        r.Line({p.x + 4 * z, p.y - 12 * z}, {p.x + 6 * z - gait, p.y}, 4 * z, Color(27, 34, 43));
        r.Quad({p.x - 9 * z, p.y - 34 * z},
               {p.x + 9 * z, p.y - 34 * z},
               {p.x + 11 * z, p.y - 10 * z},
               {p.x - 11 * z, p.y - 10 * z},
               color);
        r.Quad({p.x + 3 * z, p.y - 34 * z},
               {p.x + 9 * z, p.y - 34 * z},
               {p.x + 11 * z, p.y - 10 * z},
               {p.x + 4 * z, p.y - 12 * z},
               Color(29, 40, 48, .42f));
        r.Triangle({p.x - 6 * z, p.y - 35 * z},
                   {p.x, p.y - 25 * z},
                   {p.x + 6 * z, p.y - 35 * z},
                   Color(201, 200, 180));
        r.Ellipse({p.x - 6 * z + gait, p.y}, 4.5f * z, 2.5f * z, Color(20, 27, 32));
        r.Ellipse({p.x + 6 * z - gait, p.y}, 4.5f * z, 2.5f * z, Color(20, 27, 32));
        r.Ellipse({p.x, p.y - 42 * z},
                  7 * z,
                  9 * z,
                  npc.job == NpcJob::Wraith ? Color(69, 29, 59) : Color(211, 184, 165));
        r.Rect(p.x - 8 * z, p.y - 50 * z, 16 * z, 5 * z, Color(26, 29, 38));
        r.Ellipse({p.x - 1 * z, p.y - 48 * z}, 7 * z, 4.5f * z, Color(31, 35, 43));
        r.Line({p.x + 5 * z, p.y - 43 * z},
               {p.x + 3 * z, p.y - 36 * z},
               1.5f * z,
               Color(169, 137, 118));
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
        // Human faces, sleeves, belts and occupation-specific equipment remain non-emissive.
        r.Line({p.x - 10 * z, p.y - 31 * z}, {p.x - 13 * z, p.y - 18 * z}, 4 * z, color);
        r.Line({p.x + 10 * z, p.y - 31 * z}, {p.x + 13 * z, p.y - 18 * z}, 4 * z, color);
        r.Ellipse({p.x - 13 * z, p.y - 17 * z}, 2.5f * z, 3 * z, Color(211, 184, 165));
        r.Ellipse({p.x + 13 * z, p.y - 17 * z}, 2.5f * z, 3 * z, Color(211, 184, 165));
        r.Rect(p.x - 9 * z, p.y - 16 * z, 18 * z, 3 * z, Color(49, 39, 35));
        r.Rect(p.x - 2 * z, p.y - 16 * z, 4 * z, 3 * z, Color(199, 171, 113));
        r.Line({p.x, p.y - 32 * z}, {p.x, p.y - 18 * z}, 1 * z, Color(44, 51, 58));
        r.Rect(p.x - 4 * z, p.y - 44 * z, 2 * z, 2 * z, Color(39, 32, 36));
        r.Rect(p.x + 2 * z, p.y - 44 * z, 2 * z, 2 * z, Color(39, 32, 36));
        if (npc.job == NpcJob::Hunter)
        {
            r.Line({p.x - 7 * z, p.y - 33 * z},
                   {p.x + 7 * z, p.y - 17 * z},
                   3 * z,
                   Color(106, 75, 48));
            r.Rect(p.x - 15 * z, p.y - 35 * z, 6 * z, 16 * z, Color(58, 78, 63));
        }
        else if (npc.job == NpcJob::Guard)
        {
            r.Rect(p.x - 9 * z, p.y - 52 * z, 18 * z, 6 * z, Color(53, 77, 102));
            r.Quad({p.x - 17 * z, p.y - 30 * z},
                   {p.x - 8 * z, p.y - 29 * z},
                   {p.x - 8 * z, p.y - 15 * z},
                   {p.x - 15 * z, p.y - 11 * z},
                   Color(91, 114, 137));
        }
        else if (npc.job == NpcJob::Raider)
        {
            r.Rect(p.x - 7 * z, p.y - 41 * z, 14 * z, 6 * z, Color(66, 29, 36));
            r.Line(
                {p.x - 8 * z, p.y - 35 * z}, {p.x + 7 * z, p.y - 17 * z}, 3 * z, Color(43, 29, 33));
        }
        else if (npc.job == NpcJob::Merchant)
        {
            r.Rect(p.x - 11 * z, p.y - 49 * z, 22 * z, 3 * z, Color(113, 79, 48));
            r.Rect(p.x - 5 * z, p.y - 28 * z, 10 * z, 14 * z, Color(220, 206, 169));
        }
        else
        {
            r.Line({p.x - 6 * z, p.y - 34 * z},
                   {p.x - 6 * z, p.y - 14 * z},
                   4 * z,
                   Color(213, 208, 185));
            r.Rect(p.x + 9 * z, p.y - 25 * z, 8 * z, 11 * z, Color(78, 57, 84));
            r.Rect(p.x + 11 * z, p.y - 23 * z, 4 * z, 7 * z, Color(213, 208, 185));
        }
    }
    r.Text(p.x - 25 * z,
           p.y - 84 * z,
           (npc.fieldBoss               ? "[중간 보스] "
            : npc.job == NpcJob::Wraith ? "[괴물] "
                                        : "[주민] ")
               + npc.Name(),
           color,
           .55f * z);
    r.Text(p.x - 25 * z,
           p.y - 68 * z,
           LivingWorld::ActionName(npc.action),
           Color(205, 214, 220),
           .43f * z);
    float barY = p.y - (npc.job == NpcJob::Wraith ? 64 : 54) * z;
    r.Rect(p.x - 17 * z, barY, 34 * z, 3 * z, Color(31, 31, 42));
    r.Rect(p.x - 17 * z, barY, 34 * z * npc.health / npc.maximumHealth, 3 * z, color);
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
    // Outdoor interaction marker, never a separate interactive house or interior.
    r.Line({p.x, p.y}, {p.x, p.y - 30 * z}, 3 * z, ink);
    r.Ellipse({p.x, p.y - 30 * z}, 8 * z, 8 * z, ink);
    r.Line({p.x - 11 * z, p.y - 18 * z}, {p.x + 11 * z, p.y - 18 * z}, 2 * z, ink);
    if (site.site == TownSite::Clinic)
    {
        r.Line({p.x, p.y - 26 * z}, {p.x, p.y - 10 * z}, 3 * z, ink);
    }
    if (site.site == TownSite::Relay || site.site == TownSite::Signal)
    {
        r.Line({p.x, p.y - 32 * z}, {p.x, p.y - 82 * z}, 4 * z, Color(146, 163, 166));
        r.Line({p.x - 17 * z, p.y - 68 * z}, {p.x + 17 * z, p.y - 68 * z}, 3 * z, ink);
        r.Ellipse({p.x, p.y - 82 * z}, 4 * z, 4 * z, Emissive(ink, 2));
        r.Rect(p.x - 9 * z, p.y - 26 * z, 18 * z, 10 * z, Color(29, 66, 69));
    }
    if (site.site == TownSite::Station)
    {
        for (int side = -1; side <= 1; side += 2)
        {
            r.Line({p.x + side * 32 * z, p.y},
                   {p.x + side * 32 * z, p.y - 86 * z},
                   6 * z,
                   Color(94, 91, 108));
        }
        r.Line({p.x - 36 * z, p.y - 86 * z}, {p.x + 36 * z, p.y - 86 * z}, 8 * z, ink);
        r.Text(p.x - 26 * z, p.y - 80 * z, "지워진 역", ink, .6f * z);
    }
    r.Text(p.x - 40 * z, p.y + 9 * z, LivingWorld::SiteName(site.site), ink, .62f * z);
    if (site.site == TownSite::Market)
    {
        auto* town = level_.Graph().Get<TownActor>(site.Parent());
        if (town)
        {
            r.Text(p.x - 70 * z, p.y - 58 * z, town->Name(), ink, .58f * z);
        }
    }
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
           std::string(player.YokaiName()) + (player.weapon ? " / 무기화" : " / 동행") + "  Q 전환",
           cyan,
           .7f);
    const auto& companion = player.Companion();
    std::string skillState = companion.level < 3 ? "LV.3 해금"
                             : companion.skillCooldown > 0
                                 ? std::to_string(int(std::ceil(companion.skillCooldown))) + "초"
                             : player.weapon ? "사용 가능"
                                             : "Q 무기화 필요";
    r.Text(500,
           72,
           "요괴 LV." + std::to_string(companion.level) + " EXP "
               + std::to_string(companion.experience) + "/"
               + std::to_string(player.YokaiExperienceRequired()) + "  우클릭 " + player.SkillName()
               + " [" + skillState + "]",
           gold,
           .52f);
    r.Text(
        w - 290, 16, sim.pk ? "PK 허용 / 클릭 공격" : "PK 차단 / P 전환", sim.pk ? red : cyan, .8f);
    r.Text(w - 290,
           49,
           "수배 " + std::to_string(sim.notoriety) + "  /  마을 시간 "
               + std::to_string(int(sim.clock)) + "초",
           muted,
           .7f);

    auto* goal = level_.Graph().Get<Actor>(sim.QuestTarget(level_.Graph()));
    bool complete = sim.story.caseClosed;
    int step = sim.QuestStep();
    r.Rect(20, 109, 340, 190, panel);
    r.Rect(20, 109, 4, 190, gold);
    r.Text(34, 120, "주요 퀘스트 / 하린의 흔적", gold, .8f);
    r.Text(34,
           148,
           complete ? "원정 완료 / 기억의 진실" : "현재 단계 " + std::to_string(step) + " / 14",
           muted,
           .65f);
    for (int i = 0; i < 14; ++i)
    {
        r.Rect(34 + i * 22.f,
               173,
               17,
               4,
               complete || i < step - 1 ? cyan
               : i == step - 1          ? gold
                                        : Color(48, 60, 69));
    }
    r.Text(34, 190, sim.Objective(), white, .82f);
    r.Text(34, 222, sim.Hint(level_.Graph()), cyan, .68f);
    r.Text(34, 250, sim.QuestInstruction(), white, .57f);
    r.Text(34,
           273,
           complete            ? "무기 강화 +1 / 동조율 +20 / 보상 획득"
           : sim.CombatQuest() ? "가까이 접근하세요. 주민은 사냥 대상이 아닙니다."
                               : "WASD 이동 / 건물을 피해 도로를 이용하세요.",
           muted,
           .53f);

    r.Rect(20, 314, 340, 77, panel);
    r.Text(34, 323, "마을 소식", cyan, .64f);
    int line = 0;
    for (const auto& event : sim.events)
    {
        if (line >= 2)
        {
            break;
        }
        r.Text(34, 348 + line * 18.f, event, muted, .46f);
        ++line;
    }

    float mx = w - 175, my = 224;
    float mapLeft = w - 314, mapRight = w - 34, mapTop = 153, mapBottom = 294;
    r.Rect(w - 328, 109, 308, 222, panel);
    r.Text(w - 314, 120, "주변 지도 / 플레이어 중심", cyan, .7f);
    r.Text(w - 314,
           140,
           "청크 " + std::to_string(player_.cx) + "," + std::to_string(player_.cy),
           muted,
           .46f);
    auto mini = [&](FarmPoint relative)
    {
        return Point{mx + (relative.x - relative.y) * .12f, my + (relative.x + relative.y) * .06f};
    };
    auto inside = [&](Point p)
    {
        return p.x >= mapLeft + 5 && p.x <= mapRight - 5 && p.y >= mapTop + 5
               && p.y <= mapBottom - 5;
    };
    for (const auto& entry : chunkActors_)
    {
        auto* chunk = level_.Graph().Get<ChunkActor>(entry.second);
        auto layout = chunks_.find(entry.first);
        if (!chunk || layout == chunks_.end())
        {
            continue;
        }
        auto origin = chunk->Position();
        auto mapRect = [&](float x, float y, float width, float depth, Ink ink)
        {
            MapQuad(r,
                    mini({origin.x + x, origin.y + y}),
                    mini({origin.x + x + width, origin.y + y}),
                    mini({origin.x + x + width, origin.y + y + depth}),
                    mini({origin.x + x, origin.y + y + depth}),
                    mapLeft,
                    mapTop,
                    mapRight,
                    mapBottom,
                    ink);
        };
        mapRect(0, 0, 640, 640, Color(24, 37, 39));
        for (const auto& plot : layout->second.plots)
        {
            mapRect(plot.x, plot.y, plot.width, plot.depth, Color(53, 69, 78));
        }
        for (const auto& road : layout->second.roads)
        {
            mapRect(road.x, road.y, road.width, road.depth, Color(106, 111, 101));
        }
    }
    for (auto* site : level_.Graph().Query<TownSiteActor>())
    {
        Point p = mini(site->Position());
        if (inside(p))
        {
            r.Rect(p.x - 3, p.y - 3, 6, 6, gold);
        }
    }
    for (auto* npc : level_.Graph().Query<NpcActor>())
    {
        Point p = mini(npc->Position());
        if (npc->health > 0 && inside(p))
        {
            r.Ellipse(p, npc->storyGuardian ? 4.f : 3.f, 3, RoleColor(npc->job));
        }
    }
    if (goal)
    {
        Point p = mini(goal->Position());
        p.x = std::max(mapLeft + 8, std::min(mapRight - 8, p.x));
        p.y = std::max(mapTop + 12, std::min(mapBottom - 8, p.y));
        r.Triangle({p.x - 5, p.y - 7}, {p.x + 5, p.y - 7}, {p.x, p.y + 1}, gold);
    }
    r.Ellipse({mx, my}, 5, 5, white);
    r.Line({mx, my - 8}, {mx, my - 4}, 2, cyan);
    r.Text(w - 314, 305, "중앙: 서진 / 금색: 목표 / 분홍: 괴물", muted, .55f);
    if (goal && level_.state != RunState::Lost)
    {
        Point world = Project(goal->Position().x, goal->Position().y);
        Point marker{world.x / scale, world.y / scale - 65 * zoom_ / scale};
        Point raw = marker;
        marker.x = std::max(390.f, std::min(w - 350, marker.x));
        marker.y = std::max(125.f, std::min(h - 190, marker.y));
        float dx = raw.x - marker.x, dy = raw.y - marker.y;
        float length = std::sqrt(dx * dx + dy * dy);
        bool edge = length > 1;
        if (edge)
        {
            dx /= length;
            dy /= length;
            r.Triangle({marker.x + dx * 14, marker.y + dy * 14},
                       {marker.x - dx * 7 - dy * 7, marker.y - dy * 7 + dx * 7},
                       {marker.x - dx * 7 + dy * 7, marker.y - dy * 7 - dx * 7},
                       gold);
        }
        else
        {
            r.Triangle({marker.x - 9, marker.y - 12}, {marker.x + 9, marker.y - 12}, marker, gold);
        }
        float labelX = std::max(370.f, std::min(w - 530, marker.x - 50));
        r.Rect(labelX - 6, marker.y + 19, 174, 27, panel);
        // Overlay markers stay visible even when a building covers the target.
        r.Text(labelX,
               marker.y + 24,
               "목표 " + std::to_string(int(Length(goal->Position()))) + "m",
               gold,
               .7f);
        if (!sim.CombatQuest() && sim.QuestStep() != 7 && Length(goal->Position()) < 72)
        {
            r.Rect(w * .5f - 155, h - 190, 310, 33, panel);
            r.Text(w * .5f - 140, h - 183, "[E] 조사 / 대화 — 퀘스트 진행", gold, .78f);
        }
    }
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
           "WASD 이동   왼쪽 클릭 대상 공격   E 조사/대화   Q 영혼포   T 판매   Y 회복 구입   P PK "
           "전환   R 새 시작",
           muted,
           .63f);
    r.Text(22,
           h - 18,
           "1 검   2 원거리   3 클로 / 동행 상태로 교체   TAB 주민 목록   C 사거리   H 후처리   "
           "ESC 종료",
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
