#include "stdafx.h"
#include "AssetPreview.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace Art
{
    void ModelRenderer::Draw(PrototypeRenderer& renderer,
                             const Model& model,
                             const Pose& pose,
                             const std::vector<size_t>& selected,
                             Point origin,
                             float scale,
                             float yaw)
    {
        faces_.clear();
        float cosine = std::cos(yaw), sine = std::sin(yaw);
        auto rotate = [&](Vec3 v)
        {
            return Vec3{v.x * cosine + v.z * sine, v.y, -v.x * sine + v.z * cosine};
        };
        auto project = [&](Vec3 v)
        {
            return Point{origin.x + (v.x - v.z) * .85f * scale,
                         origin.y + ((v.x + v.z) * .425f - v.y) * scale};
        };
        for (size_t pi = 0; pi < model.primitives.size(); ++pi)
        {
            if (!selected.empty()
                && std::find(selected.begin(), selected.end(), pi) == selected.end())
            {
                continue;
            }
            const auto& primitive = model.primitives[pi];
            pose.SkinVertices(model, primitive, positions_);
            Material material;
            if (primitive.material >= 0)
            {
                material = model.materials[primitive.material];
            }
            for (size_t i = 0; i < primitive.indices.size(); i += 3)
            {
                Vec3 a = rotate(positions_[primitive.indices[i]]),
                     b = rotate(positions_[primitive.indices[i + 1]]),
                     c = rotate(positions_[primitive.indices[i + 2]]);
                Vec3 u{b.x - a.x, b.y - a.y, b.z - a.z}, v{c.x - a.x, c.y - a.y, c.z - a.z};
                Vec3 n{u.y * v.z - u.z * v.y, u.z * v.x - u.x * v.z, u.x * v.y - u.y * v.x};
                float length = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
                if (length < 1e-8f)
                {
                    continue;
                }
                if (!material.doubleSided && n.x + n.y * 1.6f + n.z <= 0)
                {
                    continue;
                }
                float light =
                    .48f + .52f * std::max(0.f, (n.x * .3f + n.y * .85f + n.z * .4f) / length);
                const auto& color = material.color;
                Ink ink{color.x * light + material.emission.x,
                        color.y * light + material.emission.y,
                        color.z * light + material.emission.z,
                        color.w};
                faces_.push_back({project(a),
                                  project(b),
                                  project(c),
                                  ink,
                                  a.x + b.x + c.x + a.z + b.z + c.z + 1.6f * (a.y + b.y + c.y)});
            }
        }
        std::stable_sort(faces_.begin(),
                         faces_.end(),
                         [](const Face& a, const Face& b)
                         {
                             return a.depth < b.depth;
                         });
        // Existing renderer batches the immutable triangle primitive: skinning does not
        // allocate a new GL mesh or issue one draw call per material/triangle.
        for (const auto& face : faces_)
        {
            renderer.Triangle(face.a, face.b, face.c, face.color);
        }
    }

    void AssetPreview::Draw(PrototypeRenderer& renderer, float seconds)
    {
        float sx = renderer.Width() / 1280.f, sy = renderer.Height() / 800.f;
        float scale = std::min(sx, sy);
        renderer.Rect(0, 0, float(renderer.Width()), float(renderer.Height()), Color(16, 24, 34));
        renderer.Text(25 * sx,
                      18 * sy,
                      "LowPolyKit / GLB 메시·골격·애니메이션 미리보기",
                      Color(209, 235, 229),
                      .9f * scale);
        renderer.Text(25 * sx,
                      50 * sy,
                      "1 대기  2 걷기  3 달리기  4 검  5 활  6 권총  7 클로  8 점프  9 사망 / "
                      "SPACE 정지 / R 다시 읽기",
                      Color(131, 172, 180),
                      .6f * scale);
        auto shared = library_.Load(library_.ModularFile());
        const char* actions[] = {"NPC_Idle",
                                 "NPC_Walk",
                                 "NPC_Run",
                                 "NPC_Attack_Sword",
                                 "NPC_Attack_Bow",
                                 "NPC_Attack_DualPistol",
                                 "NPC_Attack_Claw",
                                 "NPC_Jump",
                                 "NPC_Death"};
        const auto& npcClip = library_.Animation(actions[action_]);
        pose_.Sample(*shared, npcClip.name, seconds, npcClip.loop);
        size_t count = std::min<size_t>(11, library_.Appearances().size());
        for (size_t i = 0; i < count; ++i)
        {
            const auto& appearance = library_.Appearances()[i];
            float x = 65 + float(i) * 1140.f / float(std::max<size_t>(1, count - 1));
            auto selection = appearance.parts;
            if (action_ >= 3 && action_ <= 6)
            {
                const int props[] = {3, 0, 4, 5};
                selection["prop"] = props[action_ - 3];
            }
            auto parts = library_.SelectParts(*shared, selection);
            painter_.Draw(renderer, *shared, pose_, parts, {x * sx, 240 * sy}, 58 * scale, .4f);
            renderer.Text(
                (x - 20) * sx, 255 * sy, appearance.name, Color(187, 215, 223), .5f * scale);
        }
        renderer.Text(25 * sx,
                      300 * sy,
                      "공유 NPC 골격 + 슬롯별 스킨 메시 / " + npcClip.name,
                      Color(102, 206, 180),
                      .6f * scale);
        count = std::min<size_t>(7, library_.Creatures().size());
        for (size_t i = 0; i < count; ++i)
        {
            const auto& creature = library_.Creatures()[i];
            auto model = library_.Load(creature.file);
            auto clip = std::find_if(creature.clips.begin(),
                                     creature.clips.end(),
                                     [&](const std::string& c)
                                     {
                                         const char* suffixes[] = {"_Idle",
                                                                   "_Walk",
                                                                   "_Run",
                                                                   "_AttackPrimary",
                                                                   "_AttackPrimary",
                                                                   "_AttackPrimary",
                                                                   "_AttackPrimary",
                                                                   "_Jump",
                                                                   "_Death"};
                                         std::string suffix = suffixes[action_];
                                         return c.size() >= suffix.size()
                                                && c.substr(c.size() - suffix.size()) == suffix;
                                     });
            if (clip == creature.clips.end())
            {
                throw std::runtime_error("Creature has no matching preview clip");
            }
            pose_.Sample(*model, *clip, seconds, library_.Animation(*clip).loop);
            float x = 95 + float(i) * 1080.f / float(std::max<size_t>(1, count - 1));
            painter_.Draw(renderer, *model, pose_, {}, {x * sx, 460 * sy}, 48 * scale, .45f);
            renderer.Text(
                (x - 45) * sx, 480 * sy, creature.name, Color(228, 181, 156), .49f * scale);
        }
        renderer.Text(25 * sx,
                      525 * sy,
                      "종별 골격 / 몹 7종 / 공격·점프·사망은 숫자키를 다시 눌러 재시작",
                      Color(102, 206, 180),
                      .6f * scale);
        size_t shown = 0;
        std::map<std::string, size_t> shownBySlot;
        const std::map<std::string, size_t> limits{
            {"building", 4}, {"tree", 2}, {"rock", 1}, {"structure", 1}};
        for (const auto& entry : library_.Entries())
        {
            auto limit = limits.find(entry.slot);
            if (limit == limits.end() || shownBySlot[entry.slot] >= limit->second)
            {
                continue;
            }
            if (shown >= 8)
            {
                break;
            }
            auto model = library_.Load(entry.file);
            pose_.Sample(*model, "", 0, false);
            Vec3 low{1e9f, 1e9f, 1e9f}, high{-1e9f, -1e9f, -1e9f};
            std::vector<Vec3> positions;
            for (const auto& p : model->primitives)
            {
                pose_.SkinVertices(*model, p, positions);
                for (auto v : positions)
                {
                    low.x = std::min(low.x, v.x);
                    low.y = std::min(low.y, v.y);
                    low.z = std::min(low.z, v.z);
                    high.x = std::max(high.x, v.x);
                    high.y = std::max(high.y, v.y);
                    high.z = std::max(high.z, v.z);
                }
            }
            float extent =
                std::max({high.y - low.y, (high.x - low.x + high.z - low.z) * .85f, .01f});
            float x = 80 + float(shown) * 158;
            float unit = 115.f / extent;
            renderer.CachedMesh("art-static-v1:" + entry.file + ":"
                                    + std::to_string(library_.FileLoads()),
                                {x * sx, 710 * sy},
                                unit * scale,
                                1,
                                [&]()
                                {
                                    painter_.Draw(renderer, *model, pose_, {}, {0, 0}, 1, .4f);
                                });
            renderer.Text((x - 40) * sx,
                          735 * sy,
                          entry.slot + " " + std::to_string(entry.index),
                          Color(161, 185, 199),
                          .45f * scale);
            ++shown;
            ++shownBySlot[entry.slot];
        }
        renderer.Text(25 * sx,
                      770 * sy,
                      "단위: m / Y 위 · +Z 전방 / 제자리 클립 사용 / 기존 렌더 배칭 경로",
                      Color(131, 172, 180),
                      .52f * scale);
    }
} // namespace Art
