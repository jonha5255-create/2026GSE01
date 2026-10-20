#include "stdafx.h"
#include "AssetLibrary.h"
#include "RenderDiagnostics.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <set>
#include <stdexcept>
#pragma warning(push, 0)
#include "Dependencies/AssetImport/json.hpp"
#pragma warning(pop)

namespace Art
{
    namespace
    {
        using Json = nlohmann::json;

        Json Read(const std::filesystem::path& file)
        {
            std::ifstream stream(file);
            if (!stream || std::filesystem::file_size(file) > 8 * 1024 * 1024)
            {
                throw std::runtime_error("Cannot read asset catalog: " + file.u8string());
            }
            auto json = Json::parse(stream);
            if (json.at("schema_version") != 1)
            {
                throw std::runtime_error("Unsupported asset catalog schema");
            }
            return json;
        }
    } // namespace

    AssetLibrary::AssetLibrary(std::filesystem::path root) : root_(std::filesystem::canonical(root))
    {
        ReloadCatalogs();
    }

    std::filesystem::path AssetLibrary::Discover()
    {
        for (auto start : {std::filesystem::current_path(),
                           std::filesystem::path(RenderDiagnostics::ExecutableDirectory())})
        {
            for (int level = 0; level < 6; ++level)
            {
                auto candidate = start / "Art" / "LowPolyKit";
                if (std::filesystem::exists(candidate / "data" / "animation_manifest.json"))
                {
                    return candidate;
                }
                candidate = start / "2026GSE01" / "Art" / "LowPolyKit";
                if (std::filesystem::exists(candidate / "data" / "animation_manifest.json"))
                {
                    return candidate;
                }
                auto parent = start.parent_path();
                if (parent == start)
                {
                    break;
                }
                start = parent;
            }
        }
        throw std::runtime_error("LowPolyKit not found; pass --asset-root=<folder>");
    }

    std::filesystem::path AssetLibrary::Resolve(const std::string& file) const
    {
        auto relative = std::filesystem::u8path(file);
        if (relative.empty() || relative.is_absolute() || relative.has_root_name())
        {
            throw std::runtime_error("Asset path must be relative to LowPolyKit");
        }
        auto path = std::filesystem::weakly_canonical(root_ / relative);
        auto within = path.lexically_relative(root_);
        if (within.empty() || *within.begin() == "..")
        {
            throw std::runtime_error("Asset path escapes kit root");
        }
        return path;
    }

    std::shared_ptr<const Model> AssetLibrary::Load(const std::string& file)
    {
        auto path = Resolve(file);
        if (path.extension() != L".glb")
        {
            throw std::runtime_error("Runtime asset must be GLB, not Blender source");
        }
        auto stamp = std::filesystem::last_write_time(path);
        auto size = std::filesystem::file_size(path);
        auto& cached = cache_[path.u8string()];
        if (cached.model && cached.modified == stamp && cached.size == size)
        {
            return cached.model;
        }
        auto model = Model::Load(path);
        if (std::filesystem::last_write_time(path) != stamp
            || std::filesystem::file_size(path) != size)
        {
            throw std::runtime_error(
                "GLB changed during read; retry after Blender export completes");
        }
        cached = {stamp, size, model};
        ++fileLoads_;
        RenderDiagnostics::Get().Event("asset_load",
                                       file + "; primitives="
                                           + std::to_string(model->primitives.size())
                                           + "; skins=" + std::to_string(model->skins.size())
                                           + "; clips=" + std::to_string(model->clips.size()));
        return model;
    }

    void AssetLibrary::ReloadCatalogs()
    {
        // Build a complete replacement first. Bad/incomplete authoring writes preserve the old
        // catalog.
        std::vector<AssetEntry> entries;
        std::vector<Appearance> appearances;
        std::vector<Creature> creatures;
        std::map<std::string, ClipInfo> clips;
        std::map<std::string, std::map<int, std::string>> nodes;
        auto catalog = Read(Resolve("data/mesh_catalog.json"));
        std::set<std::pair<std::string, int>> ids;
        for (const auto& item : catalog.at("meshes"))
        {
            AssetEntry entry{item.at("mesh"), item.at("slot"), item.at("file"), item.at("index")};
            if (entry.index < 0 || !ids.insert({entry.slot, entry.index}).second)
            {
                throw std::runtime_error("Duplicate or negative mesh slot/index");
            }
            if (!std::filesystem::exists(Resolve(entry.file)))
            {
                throw std::runtime_error("Catalog references missing GLB: " + entry.file);
            }
            entries.push_back(entry);
        }
        auto manifest = Read(Resolve("data/animation_manifest.json"));
        auto modular = manifest.at("modular_library").get<std::string>();
        if (!std::filesystem::exists(Resolve(modular)))
        {
            throw std::runtime_error("Missing modular GLB");
        }
        auto parts = Read(Resolve("data/rigged_part_catalog.json"));
        for (const auto& part : parts.at("parts"))
        {
            auto slot = part.at("slot").get<std::string>();
            int index = part.at("index");
            if (index < 0 || nodes[slot].count(index) || part.at("library") != modular)
            {
                throw std::runtime_error("Invalid modular part catalog");
            }
            nodes[slot][index] = part.at("mesh_node");
        }
        for (const auto& item : manifest.at("npc_exports"))
        {
            Appearance appearance;
            appearance.id = item.at("id");
            appearance.name = item.at("name");
            appearance.file = item.at("file");
            if (!std::filesystem::exists(Resolve(appearance.file)))
            {
                throw std::runtime_error("Missing NPC export GLB");
            }
            appearance.parts = item.at("parts").get<std::map<std::string, int>>();
            appearances.push_back(appearance);
        }
        auto presets = Read(Resolve("data/npc_appearance_presets.json"));
        for (const auto& preset : presets.at("npcs"))
        {
            auto found = std::find_if(appearances.begin(),
                                      appearances.end(),
                                      [&](const Appearance& a)
                                      {
                                          return a.id == preset.at("appearance_id");
                                      });
            if (found == appearances.end())
            {
                throw std::runtime_error("Preset has no NPC export entry");
            }
            auto selection = preset.at("parts").get<std::map<std::string, int>>();
            if (selection != found->parts)
            {
                throw std::runtime_error("NPC preset and animation manifest parts disagree");
            }
            found->paletteIndex = preset.value("palette_index", 0);
        }
        auto addClip = [&](const Json& item)
        {
            ClipInfo clip;
            clip.name = item.at("name");
            clip.loop = item.at("loop");
            clip.rootMotion = item.at("root_motion");
            clip.duration = item.at("duration_s");
            clip.speed = item.value("speed_mps", 0.f);
            if (!std::isfinite(clip.duration) || clip.duration <= 0 || !std::isfinite(clip.speed))
            {
                throw std::runtime_error("Invalid animation metadata");
            }
            for (const auto& e : item.at("events"))
            {
                AnimationEvent event{e.at("time_s"), e.at("event"), e.value("degrees", 0.f)};
                if (!std::isfinite(event.time) || event.time < 0
                    || event.time > clip.duration + .001f)
                {
                    throw std::runtime_error("Animation event outside clip");
                }
                clip.events.push_back(event);
            }
            std::stable_sort(clip.events.begin(),
                             clip.events.end(),
                             [](const auto& a, const auto& b)
                             {
                                 return a.time < b.time;
                             });
            if (!clips.emplace(clip.name, clip).second)
            {
                throw std::runtime_error("Duplicate animation metadata");
            }
        };
        for (const auto& item : manifest.at("npc_clips"))
        {
            addClip(item);
        }
        std::set<int> creatureIds;
        for (const auto& item : manifest.at("creatures"))
        {
            Creature creature;
            creature.id = item.at("id");
            creature.name = item.at("name");
            creature.file = item.at("file");
            creature.index = item.at("index");
            if (creature.index < 0 || !creatureIds.insert(creature.index).second
                || !std::filesystem::exists(Resolve(creature.file)))
            {
                throw std::runtime_error("Invalid creature catalog");
            }
            for (const auto& clip : item.at("clips"))
            {
                addClip(clip);
                creature.clips.push_back(clip.at("name"));
            }
            creatures.push_back(creature);
        }
        for (const auto& appearance : appearances)
        {
            for (const auto& part : appearance.parts)
            {
                if (part.second >= 0
                    && (!nodes.count(part.first) || !nodes.at(part.first).count(part.second)))
                {
                    throw std::runtime_error("Appearance references unknown rigged part");
                }
            }
        }
        entries_ = std::move(entries);
        appearances_ = std::move(appearances);
        creatures_ = std::move(creatures);
        clips_ = std::move(clips);
        partNodes_ = std::move(nodes);
        modularFile_ = modular;
        RenderDiagnostics::Get().Event("asset_catalog",
                                       "meshes=" + std::to_string(entries_.size())
                                           + "; appearances=" + std::to_string(appearances_.size())
                                           + "; creatures=" + std::to_string(creatures_.size())
                                           + "; clips=" + std::to_string(clips_.size()));
    }

    const ClipInfo& AssetLibrary::Animation(const std::string& name) const
    {
        auto it = clips_.find(name);
        if (it == clips_.end())
        {
            throw std::runtime_error("Animation metadata not found: " + name);
        }
        return it->second;
    }

    std::vector<size_t> AssetLibrary::SelectParts(const Model& model,
                                                  const std::map<std::string, int>& parts) const
    {
        for (const auto* required : {"body", "hair", "top", "legs", "feet"})
        {
            if (!parts.count(required) || parts.at(required) < 0)
            {
                throw std::runtime_error("Required NPC part missing");
            }
        }
        if (parts.count("hair") && parts.at("hair") == 1 && parts.count("accessory")
            && parts.at("accessory") == 2)
        {
            throw std::runtime_error(
                "Long hair and quiver overlap; choose a different part combination");
        }
        std::set<std::string> names;
        for (const auto& part : parts)
        {
            if (part.second == -1)
            {
                continue;
            }
            auto slot = partNodes_.find(part.first);
            if (part.second < 0 || slot == partNodes_.end() || !slot->second.count(part.second))
            {
                throw std::runtime_error("Unknown rigged part slot/index");
            }
            names.insert(slot->second.at(part.second));
        }
        std::vector<size_t> selected;
        std::set<std::string> found;
        for (size_t i = 0; i < model.primitives.size(); ++i)
        {
            const auto& name = model.nodes[model.primitives[i].node].name;
            if (names.count(name))
            {
                selected.push_back(i);
                found.insert(name);
            }
        }
        if (found != names)
        {
            throw std::runtime_error("Rigged library is missing named part nodes");
        }
        return selected;
    }
} // namespace Art
