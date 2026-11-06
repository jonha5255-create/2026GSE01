#include "stdafx.h"
#include "YokaiAssets.h"
#include <fstream>
#include <algorithm>
#include <stdexcept>
#include <iostream>
#include <cmath>
#pragma warning(push, 0)
#include "Dependencies/AssetImport/json.hpp"
#pragma warning(pop)

namespace Art
{
    YokaiAssets::YokaiAssets(const std::filesystem::path& root)
    {
        std::ifstream modelFile(root / "data/yokai_weapon_catalog.json");
        const auto models = nlohmann::json::parse(modelFile);
        if (models.at("glb_up") != "Y" || models.at("weapon_forward_glb") != "+Y")
        {
            throw std::runtime_error("Unsupported yokai coordinate convention");
        }
        for (const auto& family : models.at("families"))
        {
            YokaiFamilyAsset entry;
            entry.index = family.at("index");
            entry.name = family.at("name");
            for (int form = 0; form < 2; ++form)
            {
                auto& data = entry.forms[form];
                const auto& source = family.at("forms").at(form ? "Weapon" : "Companion");
                const auto& variants = source.at("variants");
                if (variants.size() != 4)
                {
                    throw std::runtime_error("Expected four yokai evolutions");
                }
                data.file = variants.at(3).at("file");
                for (const auto& variant : variants)
                {
                    int stage = variant.at("evolution");
                    for (const auto& node : variant.at("nodes"))
                    {
                        auto name = node.get<std::string>();
                        auto found = data.nodeStages.find(name);
                        if (found == data.nodeStages.end())
                        {
                            data.nodeStages.emplace(name, stage);
                        }
                        else
                        {
                            found->second = std::min(found->second, stage);
                        }
                    }
                }
                for (auto it = source.at("sockets").begin(); it != source.at("sockets").end(); ++it)
                {
                    if (it.value().is_array() && it.value().size() == 3)
                    {
                        data.sockets[it.key()] =
                            BlenderToGlb({it.value()[0], it.value()[1], it.value()[2]});
                    }
                }
            }
            families_.push_back(std::move(entry));
        }
        std::ifstream effectFile(root / "data/weapon_vfx_catalog.json");
        const auto effects = nlohmann::json::parse(effectFile);
        for (const auto& effect : effects.at("effects"))
        {
            WeaponEffectAsset entry;
            entry.index = effect.at("index");
            entry.family = effect.at("family_index");
            entry.id = effect.at("id");
            entry.file = effect.at("glb");
            entry.anchor = effect.at("anchor");
            const auto kind = effect.at("kind").get<std::string>();
            entry.kind = kind == "skill"    ? WeaponEffectKind::Skill
                         : kind == "impact" ? WeaponEffectKind::Impact
                                            : WeaponEffectKind::Basic;
            entry.duration = effect.at("duration_s");
            entry.fps = effect.at("fps");
            for (const auto& marker : effect.at("events"))
            {
                if (marker.at("event") == "visual_onset")
                {
                    entry.onset = marker.at("frame").get<float>() / entry.fps;
                }
            }
            if (entry.duration <= 0 || entry.fps <= 0 || entry.onset >= entry.duration)
            {
                throw std::runtime_error("Invalid weapon VFX timing");
            }
            effects_.push_back(std::move(entry));
        }
    }

    const YokaiAssets& YokaiAssets::Shared()
    {
        static const YokaiAssets data(AssetLibrary::Discover());
        return data;
    }

    const YokaiFamilyAsset& YokaiAssets::Family(int index) const
    {
        for (const auto& family : families_)
        {
            if (family.index == index)
            {
                return family;
            }
        }
        throw std::out_of_range("Unknown yokai family");
    }

    const WeaponEffectAsset& YokaiAssets::Effect(int family, WeaponEffectKind kind) const
    {
        for (const auto& effect : effects_)
        {
            if (effect.family == family && effect.kind == kind)
            {
                return effect;
            }
        }
        throw std::out_of_range("Unknown yokai effect");
    }

    const WeaponEffectAsset& YokaiAssets::Effect(int index) const
    {
        for (const auto& effect : effects_)
        {
            if (effect.index == index)
            {
                return effect;
            }
        }
        throw std::out_of_range("Unknown weapon effect index");
    }

    bool YokaiAssets::SelfTest(const std::filesystem::path& root)
    {
        try
        {
            AssetLibrary library(root);
            YokaiAssets assets(root);
            bool ok = true;
            for (int family = 0; family < 3; ++family)
            {
                for (const auto& form : assets.Family(family).forms)
                {
                    auto model = library.Load(form.file);
                    std::array<size_t, 4> vertices{};
                    for (const auto& primitive : model->primitives)
                    {
                        int stage = form.nodeStages.at(model->nodes[primitive.node].name);
                        for (int i = stage; i < 4; ++i)
                        {
                            vertices.at(i) += primitive.vertices.size();
                        }
                    }
                    ok &= model->skins.empty() && model->nodes.size() + 1 <= 56 && vertices[0] > 0
                          && vertices[0] < vertices[1] && vertices[1] < vertices[2]
                          && vertices[2] < vertices[3];
                }
                assets.Effect(family, WeaponEffectKind::Basic);
                assets.Effect(family, WeaponEffectKind::Skill);
            }
            assets.Effect(-1, WeaponEffectKind::Impact);
            auto& rifle = assets.Family(1).forms[1].sockets;
            ok &=
                rifle.at("muzzle").y > rifle.at("support_hand").y && rifle.at("support_hand").y > 0;
            for (const auto& effect : assets.Effects())
            {
                if (effect.family >= 3)
                {
                    continue;
                }
                auto model = library.Load(effect.file);
                auto* clip = model->FindClip(effect.id);
                ok &= clip && clip->duration > effect.onset && model->nodes.size() + 1 <= 56;
                Pose before, after;
                before.Sample(*model, effect.id, 0, false);
                after.Sample(*model, effect.id, effect.onset + 2 / effect.fps, false);
                bool moved = false, emissive = false;
                for (size_t i = 0; i < before.world.size(); ++i)
                {
                    moved |= before.world[i].v != after.world[i].v;
                }
                for (const auto& material : model->materials)
                {
                    emissive |=
                        std::max({material.emission.x, material.emission.y, material.emission.z})
                        > 1;
                }
                ok &= moved && emissive;
            }
            auto m = Matrix::TRS({1, 2, 3}, {.15f, .3f, .1f, .9f}, {-.7f, .7f, .7f});
            auto p = m.InverseAffine().Point(m.Point({.3f, -.4f, .9f}));
            ok &= std::abs(p.x - .3f) + std::abs(p.y + .4f) + std::abs(p.z - .9f) < .0001f;
            return ok;
        }
        catch (const std::exception& error)
        {
            std::cerr << "[YOKAI TEST] " << error.what() << '\n';
            return false;
        }
    }
} // namespace Art
