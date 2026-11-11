#include "stdafx.h"
#include "WorldArt.h"
#include "RenderDiagnostics.h"
#include "FrameProfiler.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <numeric>
#pragma warning(push, 0)
#include "Dependencies/AssetImport/json.hpp"
#pragma warning(pop)

namespace Art
{
    namespace
    {
        constexpr float Pi = 3.14159265359f;
        constexpr float BossScale = 16.f;
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
                    const auto file = item.at("file").get<std::string>();
                    // Floating concept assets belong to authored landmarks, not
                    // ordinary homes or randomly scattered wilderness ruins.
                    if (file.find("Floating") != std::string::npos)
                    {
                        RenderDiagnostics::Get().Event(
                            "world_art_placement_excluded",
                            file + "; random_building_requires_grounded_shell");
                        continue;
                    }
                    indices.push_back(item.at("index"));
                }
            }
            if (indices.empty())
            {
                // A theme with only floating landmarks can still place grounded homes.
                indices.push_back(0);
            }
            regions_.push_back(indices);
        }
        // File parsing and bind bounds happen once at startup, never in the frame loop.
        File(library_.ModularFile());
        for (const auto& creature : library_.Creatures())
        {
            File(creature.file);
        }
        for (int region = 0; region < 3; ++region)
        {
            if (const auto* arena = library_.BossArena(region))
            {
                File(arena->roomFile);
                File(arena->boss.file);
            }
        }
        auto combatPath = library_.Resolve("data/boss_combat_catalog.json");
        if (std::filesystem::exists(combatPath))
        {
            std::ifstream combatStream(combatPath);
            auto combat = nlohmann::json::parse(combatStream);
            float meters = combat.at("arena_m_per_game_unit");
            if (!std::isfinite(meters) || meters <= 0)
            {
                throw std::runtime_error("Invalid boss effect scale");
            }
            bossFxScale_ = 1.f / meters;
            for (const auto& effect : combat.at("effects"))
            {
                BossEffect data{effect.at("file"), effect.at("authored_windup_s"),
                                effect.at("game_radius"), effect.at("game_reach")};
                File(data.file);
                bossEffects_.emplace(effect.at("id").get<std::string>(), std::move(data));
            }
            for (const auto& device : combat.at("devices"))
            {
                for (int region = 0; region < int(bossDevices_.size()); ++region)
                {
                    const auto* arena = library_.BossArena(region);
                    if (arena && device.at("boss_id") == arena->boss.id)
                    {
                        bossDevices_[region] = device.at("file").get<std::string>();
                        File(bossDevices_[region]);
                    }
                }
            }
            bossHit_ = combat.at("hit_confirm").at("file").get<std::string>();
            File(bossHit_);
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
        yokai_ = &YokaiAssets::Shared();
        for (int family = 0; family < 3; ++family)
        {
            for (const auto& form : yokai_->Family(family).forms)
            {
                File(form.file);
            }
        }
        for (const auto& effect : yokai_->Effects())
        {
            if (effect.family < 3)
            {
                File(effect.file);
            }
        }
        RenderDiagnostics::Get().Event(
            "yokai_assets_ready",
            "playable_families=3; evolution_stages=4; node_mesh_sharing=enabled");
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
        Source& source,
        const std::map<std::string, int>& parts,
        std::string& key,
        bool rigidNodes,
        bool authoredNormals)
    {
        if (rigidNodes)
        {
            key += ":rigid-nodes";
        }
        if (authoredNormals)
        {
            key += ":authored-normals";
        }
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
                    auto n = authoredNormals ? vertex.normal : normal;
                    PrototypeRenderer::ModelVertex out{};
                    if (primitive.skin < 0)
                    {
                        if (rigidNodes)
                        {
                            out.joints[0] = uint32_t(primitive.node + 1);
                        }
                        else
                        {
                            auto& matrix = source.bind.world[primitive.node];
                            p = matrix.Point(p);
                            auto origin = matrix.Point({});
                            n = matrix.Point(n);
                            n = {n.x - origin.x, n.y - origin.y, n.z - origin.z};
                        }
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
                            float opacity,
                            bool authoredNormals,
                            bool linearMaterial)
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
        const auto& geometry = Geometry(source, parts, key, false, authoredNormals);
        r.ModelMesh(key, geometry, found->second.bones, origin, scale, yaw,
                    {1, 1, 1}, {}, opacity, false, false, linearMaterial);
    }

    int WorldArt::BuildingIndex(int region, uint32_t variant) const
    {
        const auto& options = regions_[size_t(std::max(0, region)) % regions_.size()];
        return options[variant % options.size()];
    }

    int WorldArt::RuinBuildingIndex(uint32_t variant) const
    {
        // Stable LowPolyKit IDs: fractured apartment and grounded leaning shop.
        // 16 is a deliberately separated upper floor and must never enter this pool.
        const int grounded[] = {15, 17};
        return grounded[variant % 2];
    }

    bool WorldArt::VerifyBuildingPlacement() const
    {
        bool ok = regions_.size() == 7;
        for (size_t region = 0; region < regions_.size(); ++region)
        {
            for (uint32_t seed = 0; seed < 512; ++seed)
            {
                int index = BuildingIndex(int(region), seed);
                ok &= files_.at("building").at(index).find("Floating") == std::string::npos;
                int ruin = RuinBuildingIndex(seed);
                const auto& file = files_.at("building").at(ruin);
                ok &= file.find("Floating") == std::string::npos;
                const auto& source = sources_.at(file);
                ok &= std::isfinite(source.low.y) && source.high.y > source.low.y;
            }
        }
        std::cout << (ok ? "PASS " : "FAIL ")
                  << "random town and wilderness building pools exclude floating floors across "
                     "seven regions\n";
        return ok;
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

    bool WorldArt::Arena(PrototypeRenderer& r, int region, Point p, float scale)
    {
        const auto* arena = library_.BossArena(region);
        if (!arena)
        {
            return false;
        }
        auto& source = File(arena->roomFile);
        if (source.bindBones.empty())
        {
            source.bindBones = Palette(*source.model, source.bind);
        }
        std::string key = "gpu-arena:" + arena->roomFile;
        const auto& geometry = Geometry(source, {}, key, false, true);
        r.ModelMesh(key, geometry, source.bindBones, p, scale, 0, {1, 1, 1}, {}, 1,
                    false, false, true);
        return true;
    }

    void WorldArt::BossAttackFx(PrototypeRenderer& r,
                                 int region,
                                 const ArenaAttack& attack,
                                 Point p,
                                 float zoom,
                                 float releaseAge)
    {
        const char* id = nullptr;
        switch (attack.pattern)
        {
        case ArenaPattern::Circle: id = region == 0 ? "AltarSlam" : "StoneSlam"; break;
        case ArenaPattern::Charge: id = "HornCharge"; break;
        case ArenaPattern::Pounce: id = "Bite"; break;
        case ArenaPattern::Fan: id = "VenomJet"; break;
        case ArenaPattern::Cross: id = "Fissure"; break;
        case ArenaPattern::Ring: id = region == 0 ? "AltarSlam" : "StoneSlam"; break;
        case ArenaPattern::BloodSeal: id = "BloodSeal"; break;
        case ArenaPattern::Poison: id = "PoisonPool"; break;
        case ArenaPattern::Core: id = "CoreOverload"; break;
        default: return;
        }
        const bool released = releaseAge >= 0 || attack.lingering;
        if (!released && (attack.pattern == ArenaPattern::Fan || attack.pattern == ArenaPattern::Ring))
        {
            // The authored effects for these attacks have a different footprint.
            return;
        }
        auto effect = bossEffects_.find(id);
        if (effect == bossEffects_.end())
        {
            return;
        }
        const auto& data = effect->second;
        auto& source = File(data.file);
        if (source.model->clips.empty())
        {
            return;
        }
        const auto& clip = source.model->clips.front();
        float time = data.release * std::clamp(1.f - attack.remaining / attack.windup, 0.f, 1.f);
        if (released)
        {
            time = data.release + (attack.lingering ? 3.6f - attack.remaining : releaseAge);
        }
        if (time >= clip.duration)
        {
            return;
        }
        Pose pose;
        pose.Sample(*source.model, clip.name, time, false);
        float yaw = std::atan2(attack.direction.x, attack.direction.y);
        float width = data.radius > 0 ? attack.radius / data.radius : 1.f;
        float length = data.reach > 0 ? attack.reach / data.reach : width;
        if (attack.pattern == ArenaPattern::Fan)
        {
            width = 1.f;
        }
        if (attack.pattern == ArenaPattern::Ring)
        {
            width = length = 1.f;
        }
        auto stretch = Matrix::TRS({}, {0, 0, 0, 1}, {width, 1.f, length});
        const int directions = attack.pattern == ArenaPattern::Cross ? 4 : 1;
        for (int i = 0; i < directions; ++i)
        {
            float rotation = directions == 4 ? i * Pi / 2 : yaw;
            NodeDraw(r, source, data.file, pose, stretch, nullptr, 0, p,
                     zoom * bossFxScale_, rotation, released ? 1.f : .58f, false, true);
            ++FrameProfiler::Get().Counters().vfxDraws;
        }
    }

    void WorldArt::BossDevice(PrototypeRenderer& r, int region, Point p, float zoom, float opacity)
    {
        const auto& file = bossDevices_[region];
        if (file.empty())
        {
            return;
        }
        auto& source = File(file);
        NodeDraw(r, source, file, source.bind, {}, nullptr, 0, p, zoom * bossFxScale_, 0, opacity);
    }

    void WorldArt::BossHitFx(PrototypeRenderer& r, Point p, float zoom, float age)
    {
        if (bossHit_.empty())
        {
            return;
        }
        auto& source = File(bossHit_);
        if (source.model->clips.empty() || age >= source.model->clips.front().duration)
        {
            return;
        }
        Pose pose;
        pose.Sample(*source.model, source.model->clips.front().name, age, false);
        NodeDraw(r, source, bossHit_, pose, {}, nullptr, 0, p, zoom * bossFxScale_, 0, 1, false, true);
        ++FrameProfiler::Get().Counters().vfxDraws;
    }

    Point WorldArt::BossScreenBounds(int region)
    {
        const auto* arena = library_.BossArena(region);
        if (!arena)
        {
            return {22 * 1.6f, 60 * 1.6f};
        }
        const auto& source = File(arena->boss.file);
        float x = std::max(std::abs(source.low.x), std::abs(source.high.x));
        float z = std::max(std::abs(source.low.z), std::abs(source.high.z));
        float radius = std::hypot(x, z);
        constexpr float scale = BossScale * 1.6f;
        return {std::max(36.f, radius * 1.21f * scale),
                (source.high.y + radius * .61f) * scale};
    }

    const Creature& WorldArt::CreatureModel(int index) const
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
        return *found;
    }

    void WorldArt::CreatureDraw(PrototypeRenderer& r,
                                const Creature& creature,
                                const std::string& action,
                                float seconds,
                                Point p,
                                float zoom,
                                float fx,
                                float fy,
                                bool boss)
    {
        std::string name;
        for (const auto& clip : creature.clips)
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
        // Preserve the sculpted shading across the revised skinned joints.
        Animated(r, p, (boss ? BossScale : 36.f) * zoom, creature.file, name, seconds,
                 std::atan2(fx, fy), {}, 1.f, true, boss);
    }

    void WorldArt::Npc(PrototypeRenderer& r, const NpcActor& npc, Point p, float zoom, float time,
                       float hurtAge)
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
                                 : npc.attackTime >= 0  ? npc.arenaClip
                                 : hurtAge >= 0         ? "_Hit"
                                 : npc.bossRegion == 1 && npc.moveBlend > .7f ? "_Run"
                                 : npc.moveBlend > .15f ? "_Walk"
                                                        : "_Idle";
            const auto* arena = library_.BossArena(npc.bossRegion);
            const auto& creature = arena ? arena->boss : CreatureModel(index);
            if (action == "_TrialCast"
                && (!arena || !File(creature.file).model->FindClip(creature.id + action)))
            {
                action = "_AttackSecondary";
            }
            float seconds = npc.health <= 0 ? (arena ? 2.5f - npc.recovery : 10.f)
                            : action == "_Hit" ? hurtAge : time;
            if (npc.attackTime >= 0)
            {
                float windup = npc.AttackWindup();
                // Match the art impact marker to the existing authoritative hit time.
                for (const auto& clip : creature.clips)
                {
                    if (clip == creature.id + action
                        || (clip.size() >= action.size()
                            && clip.compare(clip.size() - action.size(), action.size(), action) == 0))
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
            CreatureDraw(r, creature, action, seconds, p, zoom, direction.x, direction.y,
                         arena != nullptr);
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
        parts["prop"] = -1;
        parts["accessory"] = 0;
        auto start = std::chrono::steady_clock::now();
        auto& source = File(library_.ModularFile());
        auto& pose = playerAnimator_.Evaluate(*source.model, library_, player, weapon);
        if (player.weapon && player.yokai == YokaiForm::Ranged && player.health > 0)
        {
            FitRifleHands(source, pose, player.WeaponGripBlend());
        }
        auto bones = Palette(*source.model, pose);
        ++FrameProfiler::Get().Counters().poseBuilds;
        FrameProfiler::Get().Counters().artPoseMs +=
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
                .count();
        std::string key = "gpu-art-v1:" + library_.ModularFile();
        const auto& geometry = Geometry(source, parts, key);
        r.ModelMesh(key,
                    geometry,
                    bones,
                    p,
                    36 * zoom,
                    playerAnimator_.Yaw(),
                    {1, 1, 1},
                    {},
                    player.invulnerable > 0 && int(time * 15) % 2 ? .5f : 1.f);
        AttachWeapon(r,
                     player,
                     pose,
                     source,
                     p,
                     zoom,
                     player.invulnerable > 0 && int(time * 15) % 2 ? .5f : 1.f);
    }

    void WorldArt::Enemy(
        PrototypeRenderer& r, const EnemyActor& enemy, Point p, float zoom, float time)
    {
        CreatureDraw(r,
                     CreatureModel(enemy.boss ? 6 : int(enemy.Id() % 7)),
                     "_Walk",
                     time,
                     p,
                     zoom,
                     -enemy.Position().x,
                     -enemy.Position().y);
    }
} // namespace Art
