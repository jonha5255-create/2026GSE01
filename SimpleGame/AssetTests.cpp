#include "stdafx.h"
#include "AssetLibrary.h"
#include <cmath>
#include <fstream>
#include <iostream>
#include <set>

namespace Art
{
    bool AssetLibrary::SelfTest(const std::filesystem::path& root)
    {
        bool ok = true;
        auto check = [&](bool pass, const std::string& label)
        {
            std::cout << (pass ? "PASS " : "FAIL ") << label << '\n';
            ok &= pass;
        };
        try
        {
            AssetLibrary library(root);
            check(library.Entries().size() >= 63 && library.Creatures().size() >= 7
                      && library.Appearances().size() >= 11,
                  "LowPolyKit catalogs retain stable slots and all authored families");
            std::set<std::string> files;
            for (const auto& entry : library.Entries())
            {
                files.insert(entry.file);
            }
            for (const auto& creature : library.Creatures())
            {
                files.insert(creature.file);
            }
            files.insert(library.ModularFile());
            for (const auto& appearance : library.Appearances())
            {
                files.insert(appearance.file);
            }
            for (const auto& entry : std::filesystem::directory_iterator(library.Resolve("rigged")))
            {
                if (entry.path().extension() == ".glb")
                {
                    files.insert("rigged/" + entry.path().filename().u8string());
                }
            }
            size_t totalVertices = 0, totalClips = 0;
            bool binds = true, finite = true;
            for (const auto& file : files)
            {
                auto model = library.Load(file);
                auto again = library.Load(file);
                check(model == again, "shared immutable GLB cache: " + file);
                Pose pose;
                pose.Sample(*model, "", 0, false);
                std::vector<Vec3> positions;
                for (const auto& primitive : model->primitives)
                {
                    pose.SkinVertices(*model, primitive, positions);
                    totalVertices += positions.size();
                    for (size_t i = 0; i < positions.size(); ++i)
                    {
                        auto expected =
                            pose.world[primitive.node].Point(primitive.vertices[i].position);
                        auto p = positions[i];
                        binds &= std::abs(p.x - expected.x) + std::abs(p.y - expected.y)
                                     + std::abs(p.z - expected.z)
                                 < .003f;
                    }
                }
                for (const auto& clip : model->clips)
                {
                    ++totalClips;
                    for (float phase : {0.f, .37f, 1.f, 2.1f})
                    {
                        pose.Sample(*model, clip.name, clip.duration * phase, false);
                        for (const auto& primitive : model->primitives)
                        {
                            pose.SkinVertices(*model, primitive, positions);
                            for (auto p : positions)
                            {
                                finite &=
                                    std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z);
                            }
                        }
                    }
                }
            }
            check(binds,
                  "inverse-bind skinning matches mesh-node world transform without double "
                  "transforms");
            check(finite && totalVertices > 0 && totalClips >= 104,
                  "all static and rigged GLBs produce finite animated poses");
            auto npc = library.Load(library.ModularFile());
            bool parts = true;
            for (const auto& appearance : library.Appearances())
            {
                auto selected = library.SelectParts(*npc, appearance.parts);
                parts &= !selected.empty() && selected.size() < npc->primitives.size();
            }
            check(parts,
                  "named NPC presets select only their own modular parts from one shared skeleton");
            bool metadata = true;
            for (const auto& clip : npc->clips)
            {
                metadata &= std::abs(library.Animation(clip.name).duration - clip.duration) < .04f;
            }
            for (const auto& creature : library.Creatures())
            {
                auto model = library.Load(creature.file);
                for (const auto& name : creature.clips)
                {
                    auto* clip = model->FindClip(name);
                    metadata &=
                        clip && std::abs(clip->duration - library.Animation(name).duration) < .04f;
                }
            }
            check(metadata, "manifest clip names and seconds match actual GLB animations");
            Pose a, b;
            a.Sample(*npc, "NPC_Walk", .05f, true);
            b.Sample(*npc, "NPC_Walk", .45f, true);
            bool changed = false;
            for (auto index : library.SelectParts(*npc, library.Appearances()[0].parts))
            {
                std::vector<Vec3> x, y;
                a.SkinVertices(*npc, npc->primitives[index], x);
                b.SkinVertices(*npc, npc->primitives[index], y);
                for (size_t i = 0; i < x.size(); ++i)
                {
                    changed |= std::abs(x[i].x - y[i].x) + std::abs(x[i].y - y[i].y)
                                   + std::abs(x[i].z - y[i].z)
                               > .0001f;
                }
            }
            check(changed, "skeletal animation changes actual weighted vertex positions");
            a.Sample(*npc, "NPC_Walk_RootMotion", .7f, false, true);
            b.Sample(*npc, "NPC_Walk_RootMotion", .7f, false, false);
            check(std::abs(a.rootDisplacement.x) + std::abs(a.rootDisplacement.y)
                          + std::abs(a.rootDisplacement.z)
                      > .1f,
                  "root-motion displacement is exposed separately from in-place skinning");
            AnimationPlayer player;
            ClipInfo clip;
            clip.name = "event-test";
            clip.duration = 1;
            clip.loop = true;
            clip.events = {{.2f, "impact", 0}, {.7f, "fire", 0}};
            player.Play(clip);
            auto events = player.Advance(2.8f);
            check(events.size() == 6 && std::abs(player.Time() - .8f) < .0001f,
                  "large animation steps emit crossed events exactly once across multiple loops");
            clip.loop = false;
            player.Play(clip, true);
            check(player.Advance(2).size() == 2 && player.Advance(1).empty(),
                  "one-shot attack events do not repeat after completion");
            bool bad = false;
            try
            {
                Model::Parse({0, 1, 2, 3});
            }
            catch (const std::exception&)
            {
                bad = true;
            }
            check(bad, "truncated GLB fails without publishing a partial model");
            bad = false;
            try
            {
                library.Resolve("../Characters_Rigged.blend");
            }
            catch (const std::exception&)
            {
                bad = true;
            }
            check(bad, "asset references cannot escape the selected kit folder");
            auto invalid = library.Appearances()[0].parts;
            invalid["body"] = 9999;
            bad = false;
            try
            {
                library.SelectParts(*npc, invalid);
            }
            catch (const std::exception&)
            {
                bad = true;
            }
            check(bad, "unknown part indices fail instead of silently changing appearance");
            Model synthetic;
            synthetic.nodes.resize(1);
            Channel channel;
            channel.path = ChannelPath::Translation;
            channel.interpolation = Interpolation::Step;
            channel.times = {0, 1};
            channel.values = {{0, 0, 0, 0}, {1, 0, 0, 0}};
            Clip probe;
            probe.name = "probe";
            probe.duration = 1;
            probe.channels = {channel};
            synthetic.clips = {probe};
            a.Sample(synthetic, "probe", .5f, false);
            check(std::abs(a.world[0].Point({}).x) < .0001f,
                  "STEP interpolation holds the preceding key");
            channel.interpolation = Interpolation::Cubic;
            channel.values = {{0, 0, 0, 0},
                              {0, 0, 0, 0},
                              {1, 0, 0, 0},
                              {-1, 0, 0, 0},
                              {0, 0, 0, 0},
                              {0, 0, 0, 0}};
            synthetic.clips[0].channels = {channel};
            a.Sample(synthetic, "probe", .5f, false);
            check(std::abs(a.world[0].Point({}).x - .25f) < .0001f,
                  "CUBICSPLINE uses timed Hermite tangents");
            channel.path = ChannelPath::Rotation;
            channel.interpolation = Interpolation::Linear;
            channel.values = {{0, 0, 0, 1}, {0, 1, 0, 0}};
            synthetic.clips[0].channels = {channel};
            a.Sample(synthetic, "probe", .5f, false);
            auto rotated = a.world[0].Point({1, 0, 0});
            check(std::abs(rotated.x) < .0001f && std::abs(rotated.z + 1) < .0001f,
                  "LINEAR quaternion channels interpolate rotations along the shortest arc");
            std::cout << "[ASSET] files=" << files.size() << " vertices=" << totalVertices
                      << " GLB_clips=" << totalClips << " disk_loads=" << library.FileLoads()
                      << '\n';
        }
        catch (const std::exception& error)
        {
            std::cerr << "FAIL asset validation: " << error.what() << '\n';
            ok = false;
        }
        return ok;
    }
} // namespace Art
