#include "stdafx.h"
#include "PrototypeWorld.h"
#include <windows.h>
#include <cmath>
#include <fstream>
#include <stdexcept>
#include <system_error>

#pragma warning(push, 0)
#include "Dependencies/AssetImport/json.hpp"
#pragma warning(pop)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(StoryProgress,
                                  homeInvestigated,
                                  hasHarinStudentCard,
                                  metRecorder,
                                  mukContracted,
                                  mukSync,
                                  hunted,
                                  reported,
                                  relayAligned,
                                  signalDecoded,
                                  gateOpened,
                                  guardianDefeated,
                                  echoRecovered,
                                  weaponAwakened,
                                  caseClosed,
                                  suppliesDelivered,
                                  harinRescued)

namespace
{
    using Json = nlohmann::json;
    constexpr int SaveVersion = 1;

    int ReadInt(const Json& object, const char* key, int low, int high)
    {
        const auto& value = object.at(key);
        if (!value.is_number_integer()
            || (value.is_number_unsigned() && value.get<uint64_t>() > uint64_t(high)))
        {
            throw std::runtime_error(std::string("Invalid save field: ") + key);
        }
        auto number = value.get<int64_t>();
        if (number < low || number > high)
        {
            throw std::runtime_error(std::string("Save field out of range: ") + key);
        }
        return int(number);
    }
}

void PrototypeWorld::SaveGame(const std::filesystem::path& path) const
{
    const auto& player = level_.Player();
    Json growth = Json::array();
    for (const auto& yokai : player.yokaiGrowth)
    {
        growth.push_back({{"level", yokai.level}, {"experience", yokai.experience}});
    }
    Json save = {{"version", SaveVersion},
                 {"living", level_.society.enabled},
                 {"seed", seed_},
                 {"position", {{"cx", player_.cx}, {"cy", player_.cy},
                               {"x", player_.x}, {"y", player_.y}}},
                 {"player", {{"level", player.level}, {"experience", player.experience},
                             {"weaponRank", player.weaponRank},
                             {"health", overworld_ && player.health <= 0
                                            ? player.MaximumHealth() : player.health},
                             {"yokai", int(player.yokai)}, {"weapon", player.weapon},
                             {"growth", growth}}}};
    if (level_.society.enabled)
    {
        const auto& sim = level_.society;
        save["campaign"] = {{"story", sim.story}, {"gold", sim.gold}, {"goods", sim.goods},
                            {"notoriety", sim.notoriety}, {"bossRelics", sim.bossRelics},
                            {"bossRelicReward", sim.bossRelicReward}};
    }
    else
    {
        save["farming"] = {{"kills", level_.kills}, {"pickups", level_.pickups},
                           {"won", level_.state == RunState::Won}};
    }

    std::filesystem::create_directories(path.parent_path());
    auto pending = path;
    pending += L"." + std::to_wstring(GetCurrentProcessId()) + L".tmp";
    try
    {
        std::ofstream stream(pending, std::ios::binary | std::ios::trunc);
        stream.exceptions(std::ios::failbit | std::ios::badbit);
        stream << save.dump(2) << '\n';
        stream.close();
        if (!MoveFileExW(pending.c_str(), path.c_str(),
                         MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        {
            throw std::system_error(int(GetLastError()), std::system_category(), "Replace save");
        }
    }
    catch (...)
    {
        std::error_code ignored;
        std::filesystem::remove(pending, ignored);
        throw;
    }
}

std::unique_ptr<PrototypeWorld> PrototypeWorld::LoadGame(const std::filesystem::path& path,
                                                        bool living)
{
    std::ifstream stream(path, std::ios::binary);
    const auto save = Json::parse(stream);
    if (save.at("version") != SaveVersion || save.at("living").get<bool>() != living)
    {
        throw std::runtime_error("Unsupported save version or game mode");
    }
    const auto& seed = save.at("seed");
    if (!seed.is_number_unsigned() || seed.get<uint64_t>() > UINT32_MAX)
    {
        throw std::runtime_error("Invalid world seed");
    }
    auto world = std::make_unique<PrototypeWorld>(seed.get<uint32_t>(), living);
    auto& level = world->level_;
    auto& player = level.Player();
    const auto& stats = save.at("player");
    player.level = ReadInt(stats, "level", 1, 10);
    player.experience = ReadInt(stats, "experience", 0,
                                player.level == 10 ? 0 : player.ExperienceRequired() - 1);
    player.weaponRank = ReadInt(stats, "weaponRank", 0, 8);
    const float health = stats.at("health").get<float>();
    if (!std::isfinite(health) || health < 0 || health > player.MaximumHealth())
    {
        throw std::runtime_error("Invalid player health");
    }
    player.health = health > 0 ? health : player.MaximumHealth();
    player.invulnerable = 2.f;
    player.yokai = YokaiForm(ReadInt(stats, "yokai", 0, int(player.yokaiGrowth.size()) - 1));
    player.weapon = stats.at("weapon").get<bool>();
    const auto& growth = stats.at("growth");
    if (!growth.is_array() || growth.size() != player.yokaiGrowth.size())
    {
        throw std::runtime_error("Invalid yokai growth");
    }
    for (size_t i = 0; i < player.yokaiGrowth.size(); ++i)
    {
        auto& yokai = player.yokaiGrowth[i];
        yokai.level = ReadInt(growth[i], "level", 1, 10);
        yokai.experience = ReadInt(growth[i], "experience", 0,
                                   yokai.level == 10 ? 0 : 29 + yokai.level * 20);
    }

    if (living)
    {
        auto& sim = level.society;
        const auto& campaign = save.at("campaign");
        sim.story = campaign.at("story").get<StoryProgress>();
        sim.gold = ReadInt(campaign, "gold", 0, INT_MAX);
        sim.goods = ReadInt(campaign, "goods", 0, INT_MAX);
        sim.notoriety = ReadInt(campaign, "notoriety", 0, INT_MAX);
        sim.bossRelics = unsigned(ReadInt(campaign, "bossRelics", 0, 7));
        sim.bossRelicReward = campaign.at("bossRelicReward").get<bool>();
        sim.RestoreGuardian(level.Graph());
        sim.dialogue = "진행을 불러왔습니다. " + sim.Objective();
    }
    else
    {
        const auto& farming = save.at("farming");
        level.kills = ReadInt(farming, "kills", 0, INT_MAX);
        level.pickups = ReadInt(farming, "pickups", 0, INT_MAX);
        if (farming.at("won").get<bool>())
        {
            level.state = RunState::Won;
        }
    }
    const auto& position = save.at("position");
    Location target{position.at("cx").get<int64_t>(), position.at("cy").get<int64_t>(),
                    position.at("x").get<double>(), position.at("y").get<double>()};
    if (health > 0 && target.x >= 0 && target.x < 640 && target.y >= 0 && target.y < 640
        && !world->Blocked(target))
    {
        world->Relocate(target);
    }
    return world;
}
