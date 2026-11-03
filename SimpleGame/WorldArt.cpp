#include "stdafx.h"
#include "WorldArt.h"
#include "RenderDiagnostics.h"
#include "FrameProfiler.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <numeric>
#pragma warning(push, 0)
#include "Dependencies/AssetImport/json.hpp"
#pragma warning(pop)

namespace Art
{
    namespace
    {
        constexpr float Pi = 3.14159265359f;
    }

    std::shared_ptr<WorldArt> WorldArt::Shared()
    {
        static auto shared = std::make_shared<WorldArt>();
        return shared;
    }

    WorldArt::WorldArt() : library_(AssetLibrary::Discover())
    {
        for (const auto& item : library_.Entries())
        {
            files_[item.slot][item.index] = item.file;
        }
        std::ifstream stream(library_.Resolve("data/building_regions.json"));
        auto regions = nlohmann::json::parse(stream);
        for (const auto& name : regions.at("regions"))
        {
            std::vector<int> indices;
            for (const auto& item : regions.at("buildings"))
            {
                if (item.at("region") == name)
                {
                    indices.push_back(item.at("index"));
                }
            }
            if (indices.empty())
            {
                throw std::runtime_error("Empty building region");
            }
            regions_.push_back(indices);
        }
        // File parsing and bind bounds happen once at startup, never in the frame loop.
        File(library_.ModularFile());
        for (const auto& creature : library_.Creatures())
        {
            File(creature.file);
        }
        for (const auto& item : library_.Entries())
        {
            if (item.slot == "building" || item.slot == "structure" || item.slot == "tree"
                || item.slot == "rock")
            {
                File(item.file);
            }
        }
        RenderDiagnostics::Get().Event("world_art_ready",
                                       "models=" + std::to_string(sources_.size()));
    }

    WorldArt::Source& WorldArt::File(const std::string& file)
    {
        auto found = sources_.find(file);
        if (found != sources_.end())
        {
            return found->second;
        }
        Source source;
        source.model = library_.Load(file);
        source.bind.Sample(*source.model, "", 0, false);
        std::vector<Vec3> positions;
        for (const auto& primitive : source.model->primitives)
        {
            source.bind.SkinVertices(*source.model, primitive, positions);
            for (auto p : positions)
            {
                source.low.x = std::min(source.low.x, p.x);
                source.high.x = std::max(source.high.x, p.x);
                source.low.y = std::min(source.low.y, p.y);
                source.high.y = std::max(source.high.y, p.y);
                source.low.z = std::min(source.low.z, p.z);
                source.high.z = std::max(source.high.z, p.z);
            }
        }
        return sources_.emplace(file, std::move(source)).first->second;
    }

    std::vector<std::array<float, 16>> WorldArt::Palette(const Model& model, const Pose& pose) const
    {
        std::vector<std::array<float, 16>> result{Matrix{}.v};
        for (const auto& skin : model.skins)
        {
            for (size_t j = 0; j < skin.joints.size(); ++j)
            {
                result.push_back((pose.world[skin.joints[j]] * skin.inverseBind[j]).v);
            }
        }
        if (result.size() > 56)
        {
            throw std::runtime_error("Art skeleton exceeds GL 3.3 palette budget");
        }
        return result;
    }

    const std::vector<PrototypeRenderer::ModelVertex>& WorldArt::Geometry(
        Source& source, const std::map<std::string, int>& parts, std::string& key)
    {
        for (const auto& part : parts)
        {
            key += ":" + part.first + "=" + std::to_string(part.second);
        }
        auto found = source.geometry.find(key);
        if (found != source.geometry.end())
        {
            return found->second;
        }
        const auto& model = *source.model;
        auto selected = parts.empty() ? std::vector<size_t>{} : library_.SelectParts(model, parts);
        std::vector<uint32_t> offsets;
        uint32_t offset = 1;
        for (const auto& skin : model.skins)
        {
            offsets.push_back(offset);
            offset += uint32_t(skin.joints.size());
        }
        std::vector<PrototypeRenderer::ModelVertex> data;
        for (size_t pi = 0; pi < model.primitives.size(); ++pi)
        {
            if (!selected.empty()
                && std::find(selected.begin(), selected.end(), pi) == selected.end())
            {
                continue;
            }
            const auto& primitive = model.primitives[pi];
            Material material;
            if (primitive.material >= 0)
            {
                material = model.materials[primitive.material];
            }
            for (size_t i = 0; i < primitive.indices.size(); i += 3)
            {
                auto a = primitive.vertices[primitive.indices[i]].position;
                auto b = primitive.vertices[primitive.indices[i + 1]].position;
                auto c = primitive.vertices[primitive.indices[i + 2]].position;
                Vec3 u{b.x - a.x, b.y - a.y, b.z - a.z}, v{c.x - a.x, c.y - a.y, c.z - a.z};
                Vec3 normal{u.y * v.z - u.z * v.y, u.z * v.x - u.x * v.z, u.x * v.y - u.y * v.x};
                for (size_t k = 0; k < 3; ++k)
                {
                    const auto& vertex = primitive.vertices[primitive.indices[i + k]];
                    auto p = vertex.position;
                    auto n = normal;
                    PrototypeRenderer::ModelVertex out{};
                    if (primitive.skin < 0)
                    {
                        auto& matrix = source.bind.world[primitive.node];
                        p = matrix.Point(p);
                        auto origin = matrix.Point({});
                        n = matrix.Point(n);
                        n = {n.x - origin.x, n.y - origin.y, n.z - origin.z};
                        out.weights[0] = 1;
                    }
                    else
                    {
                        for (size_t j = 0; j < 4; ++j)
                        {
                            out.joints[j] = offsets[primitive.skin] + vertex.joints[j];
                            out.weights[j] = vertex.weights[j];
                        }
                    }
                    out.position[0] = p.x;
                    out.position[1] = p.y;
                    out.position[2] = p.z;
                    out.normal[0] = n.x;
                    out.normal[1] = n.y;
                    out.normal[2] = n.z;
                    out.color[0] = material.color.x;
                    out.color[1] = material.color.y;
                    out.color[2] = material.color.z;
                    out.color[3] = material.color.w;
                    out.emission[0] = material.emission.x;
                    out.emission[1] = material.emission.y;
                    out.emission[2] = material.emission.z;
                    data.push_back(out);
                }
            }
        }
        return source.geometry.emplace(key, std::move(data)).first->second;
    }

    void WorldArt::Animated(PrototypeRenderer& r,
                            Point origin,
                            float scale,
                            const std::string& file,
                            const std::string& name,
                            float seconds,
                            float yaw,
                            const std::map<std::string, int>& parts,
                            float opacity)
    {
        auto& source = File(file);
        const auto& info = library_.Animation(name);
        seconds = info.loop ? std::fmod(std::max(0.f, seconds), info.duration)
                            : std::clamp(seconds, 0.f, info.duration);
        int frame = int(seconds * 30);
        auto skinKey = name + ":" + std::to_string(frame);
        auto found = source.frames.find(skinKey);
        if (found == source.frames.end())
        {
            ++FrameProfiler::Get().Counters().poseBuilds;
            auto start = std::chrono::steady_clock::now();
            if (source.frames.size() >= 64)
            {
                auto oldest = std::min_element(source.frames.begin(),
                                               source.frames.end(),
                                               [](const auto& a, const auto& b)
                                               {
                                                   return a.second.used < b.second.used;
                                               });
                source.frames.erase(oldest);
            }
            Source::SkinFrame skin;
            skin.pose.Sample(*source.model, name, std::min(info.duration, frame / 30.f), false);
            skin.bones = Palette(*source.model, skin.pose);
            found = source.frames.emplace(skinKey, std::move(skin)).first;
            FrameProfiler::Get().Counters().artPoseMs +=
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
                    .count();
        }
        else
        {
            ++FrameProfiler::Get().Counters().poseHits;
        }
        found->second.used = ++access_;
        std::string key = "gpu-art-v1:" + file;
        const auto& geometry = Geometry(source, parts, key);
        r.ModelMesh(key, geometry, found->second.bones, origin, scale, yaw, {1, 1, 1}, {}, opacity);
    }

    int WorldArt::BuildingIndex(int region, uint32_t variant) const
    {
        const auto& options = regions_[size_t(std::max(0, region)) % regions_.size()];
        return options[variant % options.size()];
    }

    void WorldArt::Static(PrototypeRenderer& r,
                          const std::string& slot,
                          int index,
                          Point p,
                          float width,
                          float depth,
                          float zoom,
                          float opacity,
                          bool vertical)
    {
        const auto& file = files_.at(slot).at(index);
        auto& source = File(file);
        auto extent = Vec3{source.high.x - source.low.x,
                           source.high.y - source.low.y,
                           source.high.z - source.low.z};
        float sx = width / std::max(.01f, vertical ? extent.z : extent.x);
        float sz = depth / std::max(.01f, vertical ? extent.x : extent.z);
        std::string key = "gpu-art-v1:" + file;
        const auto& geometry = Geometry(source, {}, key);
        if (source.bindBones.empty())
        {
            source.bindBones = Palette(*source.model, source.bind);
        }
        r.ModelMesh(key,
                    geometry,
                    source.bindBones,
                    p,
                    zoom,
                    vertical ? Pi / 2 : 0,
                    {sx, std::min(sx, sz), sz},
                    {-(source.low.x + source.high.x) * .5f,
                     -source.low.y,
                     -(source.low.z + source.high.z) * .5f},
                    opacity);
    }

    void WorldArt::CreatureDraw(PrototypeRenderer& r,
                                int index,
                                const std::string& action,
                                float seconds,
                                Point p,
                                float zoom,
                                float fx,
                                float fy)
    {
        auto found = std::find_if(library_.Creatures().begin(),
                                  library_.Creatures().end(),
                                  [&](const Creature& c)
                                  {
                                      return c.index == index;
                                  });
        if (found == library_.Creatures().end())
        {
            throw std::runtime_error("Unknown world art creature index");
        }
        std::string name;
        for (const auto& clip : found->clips)
        {
            if (clip.size() >= action.size() && clip.substr(clip.size() - action.size()) == action)
            {
                name = clip;
                break;
            }
        }
        if (name.empty())
        {
            throw std::runtime_error("Missing creature action " + action);
        }
        Animated(r, p, 36 * zoom, found->file, name, seconds, std::atan2(fx, fy), {});
    }

    void WorldArt::Npc(PrototypeRenderer& r, const NpcActor& npc, Point p, float zoom, float time)
    {
        if (npc.job == NpcJob::Wraith)
        {
            int index = npc.artCreature;
            if (index < 0)
            {
                const int horn[] = {0, 1, 2}, hounds[] = {4, 5, 6};
                index = npc.storyGuardian      ? 1
                        : npc.monsterForm == 1 ? hounds[npc.Id() % 3]
                        : npc.monsterForm == 2 ? 3
                                               : horn[npc.Id() % 3];
            }
            std::string action = npc.health <= 0        ? "_Death"
                                 : npc.attackTime >= 0  ? "_AttackPrimary"
                                 : npc.moveBlend > .15f ? "_Walk"
                                                        : "_Idle";
            float seconds = npc.health <= 0 ? 10.f : time;
            if (npc.attackTime >= 0)
            {
                float windup = npc.monsterForm == 1 ? .22f : npc.monsterForm == 2 ? .55f : .35f;
                // Match the art impact marker to the existing authoritative hit time.
                const auto& creature = *std::find_if(library_.Creatures().begin(),
                                                     library_.Creatures().end(),
                                                     [&](const Creature& c)
                                                     {
                                                         return c.index == index;
                                                     });
                for (const auto& clip : creature.clips)
                {
                    if (clip.find("AttackPrimary") != std::string::npos)
                    {
                        const auto& info = library_.Animation(clip);
                        float impact =
                            info.events.empty() ? info.duration * .5f : info.events.front().time;
                        seconds = npc.attackTime <= windup ? npc.attackTime / windup * impact
                                                           : impact
                                                                 + (npc.attackTime - windup) / .35f
                                                                       * (info.duration - impact);
                    }
                }
            }
            auto direction = npc.attackTime >= 0 ? npc.attackDirection : npc.moveFacing;
            CreatureDraw(r, index, action, seconds, p, zoom, direction.x, direction.y);
            return;
        }
        size_t appearance =
            size_t(std::max(0, npc.appearancePreset)) % library_.Appearances().size();
        const auto& preset = library_.Appearances()[appearance];
        std::string action = npc.health <= 0        ? "NPC_Death"
                             : npc.moveBlend > .15f ? "NPC_Walk"
                                                    : "NPC_Idle";
        float seconds = npc.health <= 0 ? 10.f : time;
        if (npc.target != NoActor && npc.cooldown > .55f)
        {
            int prop = preset.parts.count("prop") ? preset.parts.at("prop") : -1;
            action = prop == 0   ? "NPC_Attack_Bow"
                     : prop == 1 ? "NPC_Attack_Spear"
                     : prop == 3 ? "NPC_Attack_Sword"
                     : prop == 4 ? "NPC_Attack_DualPistol"
                     : prop == 5 ? "NPC_Attack_Claw"
                     : prop == 6 ? "NPC_Attack_Scissors"
                                 : "NPC_Attack_Unarmed";
            seconds = (.9f - npc.cooldown) / .35f * library_.Animation(action).duration;
        }
        Animated(r,
                 p,
                 36 * zoom,
                 library_.ModularFile(),
                 action,
                 seconds,
                 std::atan2(npc.moveFacing.x, npc.moveFacing.y),
                 preset.parts);
    }

    void WorldArt::Player(PrototypeRenderer& r,
                          const PlayerActor& player,
                          const WeaponActor& weapon,
                          Point p,
                          float zoom,
                          float time)
    {
        auto parts = library_.Appearances()[0].parts;
        parts["prop"] = player.weapon ? (player.yokai == YokaiForm::Sword  ? 3
                                         : player.yokai == YokaiForm::Claw ? 5
                                                                           : 4)
                                      : -1;
        parts["accessory"] = 0;
        const char* attack = player.yokai == YokaiForm::Sword  ? "NPC_Attack_Sword"
                             : player.yokai == YokaiForm::Claw ? "NPC_Attack_Claw"
                                                               : "NPC_Attack_DualPistol";
        std::string action = player.health <= 0                                       ? "NPC_Death"
                             : Length(player.moveInput) > 0 && player.firingHold <= 0 ? "NPC_Walk"
                                                                                      : "NPC_Idle";
        float seconds = player.health <= 0 ? 10.f : time;
        if (player.weapon
            && (weapon.swingTime > 0 || weapon.skillTime >= 0 || player.firingHold > 0))
        {
            action = attack;
            float progress = weapon.skillTime >= 0  ? weapon.skillTime
                             : weapon.swingTime > 0 ? weapon.SwingProgress()
                                                    : (1 - player.firingHold / .16f);
            seconds = std::clamp(progress, 0.f, 1.f) * library_.Animation(action).duration;
        }
        auto direction = weapon.aim;
        if (!player.weapon && Length(player.moveInput) > 0)
        {
            direction = {player.moveInput.x + player.moveInput.y * 2,
                         -player.moveInput.x + player.moveInput.y * 2};
        }
        Animated(r,
                 p,
                 36 * zoom,
                 library_.ModularFile(),
                 action,
                 seconds,
                 std::atan2(direction.x, direction.y),
                 parts,
                 player.invulnerable > 0 && int(time * 15) % 2 ? .5f : 1.f);
    }

    void WorldArt::Enemy(
        PrototypeRenderer& r, const EnemyActor& enemy, Point p, float zoom, float time)
    {
        CreatureDraw(r,
                     enemy.boss ? 6 : int(enemy.Id() % 7),
                     "_Walk",
                     time,
                     p,
                     zoom,
                     -enemy.Position().x,
                     -enemy.Position().y);
    }
} // namespace Art
