#include "stdafx.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include "WorldMonitor.h"
#include "LevelOne.h"
#include "WorldTypes.h"
#include "RenderDiagnostics.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <locale>
#include <limits>
#include <map>
#include <sstream>

#pragma comment(lib, "Ws2_32.lib")

namespace
{
    std::string Quote(const std::string& text)
    {
        std::ostringstream out;
        out << '"';
        for (unsigned char c : text)
        {
            if (c == '"' || c == '\\')
            {
                out << '\\' << char(c);
            }
            else if (c < 32)
            {
                out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << int(c);
            }
            else
            {
                out << char(c);
            }
        }
        out << '"';
        return out.str();
    }

    std::string Number(double value)
    {
        if (!std::isfinite(value))
        {
            return "null";
        }
        std::ostringstream out;
        out.imbue(std::locale::classic());
        out << std::setprecision(10) << value;
        return out.str();
    }

    const char* Boolean(bool value)
    {
        return value ? "true" : "false";
    }

    uint64_t UnixMs()
    {
        return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                         std::chrono::system_clock::now().time_since_epoch())
                                         .count());
    }

    std::string Position(FarmPoint point)
    {
        return "{\"x\":" + Number(point.x) + ",\"y\":" + Number(point.y) + "}";
    }

    std::string Join(const std::vector<std::string>& rows)
    {
        std::string result = "[";
        for (size_t i = 0; i < rows.size(); ++i)
        {
            if (i)
            {
                result += ',';
            }
            result += rows[i];
        }
        return result + "]";
    }

    ActorId Village(const SceneGraph& graph, const Actor& actor)
    {
        auto* parent = graph.Get(actor.Parent());
        while (parent)
        {
            if (dynamic_cast<TownActor*>(parent))
            {
                return parent->Id();
            }
            parent = graph.Get(parent->Parent());
        }
        return NoActor;
    }

    struct Totals
    {
        int npcs = 0, npcsAlive = 0, npcsRecovering = 0, npcsInactive = 0;
        int monsters = 0, monstersAlive = 0, monstersRecovering = 0, monstersInactive = 0;
        int64_t gold = 0, goods = 0, insight = 0;
        double healthPercent = 0;
        std::map<std::string, int> actions;

        void Count(bool monster, bool alive, bool recovering, bool active)
        {
            if (monster)
            {
                ++monsters;
                monstersAlive += alive && active;
                monstersRecovering += recovering && active;
                monstersInactive += !active;
            }
            else
            {
                ++npcs;
                npcsAlive += alive && active;
                npcsRecovering += recovering && active;
                npcsInactive += !active;
            }
        }

        std::string Counts() const
        {
            return "{\"npcs\":" + std::to_string(npcs)
                   + ",\"npcsAlive\":" + std::to_string(npcsAlive)
                   + ",\"npcsRecovering\":" + std::to_string(npcsRecovering) + ",\"npcsInactive\":"
                   + std::to_string(npcsInactive) + ",\"monsters\":" + std::to_string(monsters)
                   + ",\"monstersAlive\":" + std::to_string(monstersAlive)
                   + ",\"monstersRecovering\":" + std::to_string(monstersRecovering)
                   + ",\"monstersInactive\":" + std::to_string(monstersInactive) + "}";
        }
    };

    bool SendAll(SOCKET socket, const std::string& data)
    {
        size_t offset = 0;
        while (offset < data.size())
        {
            int sent = send(socket,
                            data.data() + offset,
                            static_cast<int>(std::min<size_t>(data.size() - offset, 65536)),
                            0);
            if (sent <= 0)
            {
                return false;
            }
            offset += sent;
        }
        return true;
    }
} // namespace

WorldMonitor& WorldMonitor::Get()
{
    static WorldMonitor monitor;
    return monitor;
}

WorldMonitor::~WorldMonitor()
{
    Stop();
}

std::string WorldMonitor::Snapshot(const LevelOne& level,
                                   const Location& player,
                                   size_t chunks,
                                   uint32_t seed)
{
    const auto& graph = level.Graph();
    const auto& sim = level.society;
    Totals world;
    std::map<ActorId, Totals> villages;
    for (auto* town : graph.Query<TownActor>(true))
    {
        villages.emplace(town->Id(), Totals{});
    }
    std::vector<std::string> humans, monsters, sites, villageRows, events;
    for (auto* npc : graph.Query<NpcActor>(true))
    {
        ActorId village = Village(graph, *npc);
        bool monster = npc->job == NpcJob::Wraith;
        bool active = graph.Active(npc->Id());
        bool alive = npc->health > 0 && npc->recovery <= 0;
        world.Count(monster, alive, npc->recovery > 0, active);
        if (!monster)
        {
            world.gold += npc->gold;
            world.goods += npc->goods;
            world.insight += npc->insight;
        }
        if (village)
        {
            auto& stats = villages[village];
            stats.Count(monster, alive, npc->recovery > 0, active);
            if (!monster)
            {
                stats.gold += npc->gold;
                stats.goods += npc->goods;
                stats.insight += npc->insight;
                if (npc->maximumHealth > 0 && std::isfinite(npc->health))
                {
                    stats.healthPercent +=
                        std::max(0.f, std::min(100.f, npc->health / npc->maximumHealth * 100));
                }
                ++stats.actions[LivingWorld::ActionName(npc->action)];
            }
        }
        std::ostringstream row;
        row << "{\"id\":" << Quote(std::to_string(npc->Id()))
            << ",\"villageId\":" << (village ? Quote(std::to_string(village)) : "null")
            << ",\"name\":" << Quote(npc->Name())
            << ",\"job\":" << Quote(LivingWorld::JobName(npc->job))
            << ",\"action\":" << Quote(LivingWorld::ActionName(npc->action))
            << ",\"health\":" << Number(npc->health)
            << ",\"maxHealth\":" << Number(npc->maximumHealth) << ",\"active\":" << Boolean(active)
            << ",\"alive\":" << Boolean(alive) << ",\"recoverySeconds\":" << Number(npc->recovery)
            << ",\"gold\":" << npc->gold << ",\"goods\":" << npc->goods
            << ",\"insight\":" << npc->insight << ",\"growth\":" << npc->growth
            << ",\"victories\":" << npc->victories << ",\"hostile\":" << Boolean(npc->playerHostile)
            << ",\"offender\":" << Boolean(npc->offender)
            << ",\"guardian\":" << Boolean(npc->storyGuardian)
            << ",\"fieldBoss\":" << Boolean(npc->fieldBoss)
            << ",\"monsterForm\":" << npc->monsterForm
            << ",\"attacking\":" << Boolean(npc->attackTime >= 0)
            << ",\"targetId\":" << Quote(std::to_string(npc->target))
            << ",\"position\":" << Position(npc->Position())
            << ",\"weights\":{\"survival\":" << Number(npc->survivalWeight)
            << ",\"wealth\":" << Number(npc->wealthWeight)
            << ",\"insight\":" << Number(npc->insightWeight)
            << ",\"raid\":" << Number(npc->raidWeight) << "}}";
        (monster ? monsters : humans).push_back(row.str());
    }
    for (auto* enemy : graph.Query<EnemyActor>(true))
    {
        bool active = graph.Active(enemy->Id()), alive = enemy->health > 0;
        ActorId village = Village(graph, *enemy);
        world.Count(true, alive, false, active);
        if (village)
        {
            villages[village].Count(true, alive, false, active);
        }
        monsters.push_back(
            "{\"id\":" + Quote(std::to_string(enemy->Id()))
            + ",\"villageId\":" + (village ? Quote(std::to_string(village)) : "null")
            + ",\"name\":" + Quote(enemy->boss ? "파밍 보스" : "파밍 몬스터")
            + ",\"job\":\"몬스터\",\"action\":\"전투\",\"health\":" + Number(enemy->health)
            + ",\"maxHealth\":" + Number(enemy->maximumHealth) + ",\"active\":" + Boolean(active)
            + ",\"alive\":" + Boolean(alive) + ",\"guardian\":" + Boolean(enemy->boss)
            + ",\"recoverySeconds\":0,\"position\":" + Position(enemy->Position()) + "}");
    }
    for (auto* site : graph.Query<TownSiteActor>(true))
    {
        sites.push_back("{\"id\":" + Quote(std::to_string(site->Id()))
                        + ",\"villageId\":" + Quote(std::to_string(Village(graph, *site)))
                        + ",\"name\":" + Quote(LivingWorld::SiteName(site->site))
                        + ",\"position\":" + Position(site->Position()) + "}");
    }
    for (auto* town : graph.Query<TownActor>(true))
    {
        const auto& stats = villages[town->Id()];
        std::vector<std::string> actions;
        for (const auto& action : stats.actions)
        {
            actions.push_back("{\"name\":" + Quote(action.first)
                              + ",\"count\":" + std::to_string(action.second) + "}");
        }
        std::string activity = town->Id() == sim.town
                                   ? "{\"trades\":" + std::to_string(sim.trades)
                                         + ",\"hunts\":" + std::to_string(sim.hunts)
                                         + ",\"raids\":" + std::to_string(sim.raids) + "}"
                                   : "null";
        villageRows.push_back(
            "{\"id\":" + Quote(std::to_string(town->Id())) + ",\"name\":" + Quote(town->Name())
            + ",\"active\":" + Boolean(graph.Active(town->Id())) + ",\"position\":"
            + Position(town->Position()) + ",\"counts\":" + stats.Counts() + ",\"totalGold\":"
            + std::to_string(stats.gold) + ",\"totalGoods\":" + std::to_string(stats.goods)
            + ",\"totalInsight\":" + std::to_string(stats.insight) + ",\"averageHealthPercent\":"
            + Number(stats.npcs ? stats.healthPercent / stats.npcs : 0)
            + ",\"actions\":" + Join(actions) + ",\"activity\":" + activity + "}");
    }
    for (const auto& event : sim.events)
    {
        events.push_back(Quote(event));
    }
    std::vector<std::string> incidentRows;
    for (const auto& incident : sim.incidents)
    {
        incidentRows.push_back("{\"id\":" + Quote(std::to_string(incident.id))
                               + ",\"type\":" + Quote(incident.type) + ",\"simulationSeconds\":"
                               + Number(incident.time) + ",\"text\":" + Quote(incident.text)
                               + ",\"actorId\":" + Quote(std::to_string(incident.actor))
                               + ",\"actorName\":" + Quote(incident.actorName)
                               + ",\"victimId\":" + Quote(std::to_string(incident.victim))
                               + ",\"victimName\":" + Quote(incident.victimName)
                               + ",\"witnessId\":" + Quote(std::to_string(incident.witness))
                               + ",\"witnessName\":" + Quote(incident.witnessName)
                               + ",\"villageId\":" + Quote(std::to_string(incident.village))
                               + ",\"sourceId\":" + Quote(std::to_string(incident.sourceId))
                               + ",\"verified\":" + Boolean(incident.type != "rumor") + "}");
    }
    std::ostringstream out;
    out << "{\"type\":\"world_snapshot\",\"schemaVersion\":1,\"generatedAtUnixMs\":" << UnixMs()
        << ",\"mode\":" << Quote(sim.enabled ? "living-world" : "level-one")
        << ",\"simulationSeconds\":" << Number(sim.clock) << ",\"seed\":" << seed
        << ",\"counts\":" << world.Counts() << ",\"economy\":{\"npcGold\":" << world.gold
        << ",\"npcGoods\":" << world.goods << ",\"npcInsight\":" << world.insight
        << "},\"actorCount\":" << graph.Size() << ",\"residentChunks\":" << chunks
        << ",\"villages\":" << Join(villageRows) << ",\"npcs\":" << Join(humans)
        << ",\"monsters\":" << Join(monsters) << ",\"sites\":" << Join(sites)
        << ",\"events\":" << Join(events) << ",\"incidents\":" << Join(incidentRows)
        << ",\"incidentTotals\":{\"murder\":" << sim.murders << ",\"rumor\":" << sim.rumors << "}"
        << ",\"yokaiProgress\":[";
    for (int form = 0; form < 3; ++form)
    {
        const auto& growth = level.Player().yokaiGrowth[form];
        if (form)
        {
            out << ',';
        }
        out << "{\"form\":" << form << ",\"level\":" << growth.level
            << ",\"experience\":" << growth.experience << ",\"required\":" << 30 + growth.level * 20
            << ",\"skillUnlocked\":" << Boolean(growth.level >= 3)
            << ",\"skillCooldown\":" << Number(growth.skillCooldown) << "}";
    }
    out << "],\"player\":{\"chunkX\":" << Quote(std::to_string(player.cx))
        << ",\"chunkY\":" << Quote(std::to_string(player.cy)) << ",\"localX\":" << Number(player.x)
        << ",\"localY\":" << Number(player.y) << ",\"health\":" << Number(level.Player().health)
        << ",\"maxHealth\":" << Number(level.Player().MaximumHealth())
        << ",\"level\":" << level.Player().level << ",\"gold\":" << sim.gold
        << ",\"goods\":" << sim.goods << "}"
        << ",\"quest\":{\"step\":" << sim.QuestStep() << ",\"objective\":" << Quote(sim.Objective())
        << ",\"complete\":" << Boolean(sim.story.caseClosed) << "}}";
    return out.str();
}

bool WorldMonitor::Start(unsigned short port)
{
    Stop();
    WSADATA data;
    if (WSAStartup(MAKEWORD(2, 2), &data))
    {
        return false;
    }
    networkInitialized_ = true;
    SOCKET socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    listener_ = static_cast<uintptr_t>(socket);
    BOOL exclusive = TRUE;
    setsockopt(socket,
               SOL_SOCKET,
               SO_EXCLUSIVEADDRUSE,
               reinterpret_cast<const char*>(&exclusive),
               sizeof(exclusive));
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(port);
    if (socket == INVALID_SOCKET
        || bind(socket, reinterpret_cast<sockaddr*>(&address), sizeof(address))
        || listen(socket, 8))
    {
        std::cerr << "[MONITOR] Cannot bind 127.0.0.1:" << port << " (error " << WSAGetLastError()
                  << ")\n";
        Stop();
        return false;
    }
    assetDirectory_ = RenderDiagnostics::ExecutableDirectory() + L"Monitor\\";
    sessionId_ = std::to_string(GetCurrentProcessId()) + "-" + std::to_string(UnixMs());
    latest_ = "{\"type\":\"waiting\",\"schemaVersion\":1}";
    sequence_ = 0;
    worldEpoch_ = 1;
    lastWorldId_ = 0;
    nextSnapshot_ = std::chrono::steady_clock::now();
    running_ = true;
    try
    {
        worker_ = std::thread(&WorldMonitor::Serve, this);
    }
    catch (...)
    {
        Stop();
        return false;
    }
    std::cout << "[MONITOR] Dashboard: http://127.0.0.1:" << port << "/ (JSON /api/state, 500ms)\n";
    RenderDiagnostics::Get().Event("monitor_start",
                                   "loopback_port=" + std::to_string(port) + "; interval_ms=500");
    return true;
}

void WorldMonitor::Stop()
{
    running_ = false;
    if (worker_.joinable())
    {
        worker_.join();
    }
    if (listener_ != UINTPTR_MAX)
    {
        closesocket(static_cast<SOCKET>(listener_));
    }
    listener_ = UINTPTR_MAX;
    if (networkInitialized_)
    {
        WSACleanup();
    }
    networkInitialized_ = false;
}

void WorldMonitor::Publish(
    const LevelOne& level, const Location& player, size_t chunks, uint32_t seed, uint64_t worldId)
{
    if (!running_)
    {
        return;
    }
    auto now = std::chrono::steady_clock::now();
    if (now < nextSnapshot_)
    {
        return;
    }
    nextSnapshot_ = now + std::chrono::milliseconds(500);
    if (lastWorldId_ && worldId != lastWorldId_)
    {
        ++worldEpoch_;
    }
    lastWorldId_ = worldId;
    auto data = Snapshot(level, player, chunks, seed);
    data.pop_back();
    data += ",\"sessionId\":" + Quote(sessionId_) + ",\"sequence\":" + std::to_string(++sequence_)
            + ",\"worldEpoch\":" + std::to_string(worldEpoch_) + "}";
    std::lock_guard<std::mutex> lock(mutex_);
    latest_.swap(data);
}

void WorldMonitor::Serve()
{
    SOCKET listener = static_cast<SOCKET>(listener_);
    while (running_)
    {
        fd_set readable;
        FD_ZERO(&readable);
        FD_SET(listener, &readable);
        timeval timeout{0, 100000};
        if (select(0, &readable, nullptr, nullptr, &timeout) <= 0)
        {
            continue;
        }
        SOCKET client = accept(listener, nullptr, nullptr);
        if (client == INVALID_SOCKET)
        {
            continue;
        }
        DWORD receiveMs = 300, sendMs = 500;
        setsockopt(client,
                   SOL_SOCKET,
                   SO_RCVTIMEO,
                   reinterpret_cast<const char*>(&receiveMs),
                   sizeof(receiveMs));
        setsockopt(client,
                   SOL_SOCKET,
                   SO_SNDTIMEO,
                   reinterpret_cast<const char*>(&sendMs),
                   sizeof(sendMs));
        char buffer[4096];
        std::string request;
        while (request.find("\r\n\r\n") == std::string::npos && request.size() < sizeof(buffer))
        {
            int size = recv(client, buffer, static_cast<int>(sizeof(buffer) - request.size()), 0);
            if (size <= 0)
            {
                break;
            }
            request.append(buffer, size);
        }
        std::istringstream line(request);
        std::string method, path, version;
        line >> method >> path >> version;
        path = path.substr(0, path.find('?'));
        std::string body, mime = "text/plain; charset=utf-8", status = "200 OK";
        if (method != "GET")
        {
            status = "405 Method Not Allowed";
            body = "GET required";
        }
        else if (path == "/api/state")
        {
            std::lock_guard<std::mutex> lock(mutex_);
            body = latest_;
            mime = "application/json; charset=utf-8";
        }
        else if (path == "/" || path == "/index.html" || path == "/monitor.js"
                 || path == "/monitor.css")
        {
            std::wstring filename = path == "/monitor.js"    ? L"monitor.js"
                                    : path == "/monitor.css" ? L"monitor.css"
                                                             : L"index.html";
            std::ifstream file(assetDirectory_ + filename, std::ios::binary);
            if (file)
            {
                body.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
                mime = filename == L"monitor.js"    ? "text/javascript; charset=utf-8"
                       : filename == L"monitor.css" ? "text/css; charset=utf-8"
                                                    : "text/html; charset=utf-8";
            }
            else
            {
                status = "404 Not Found";
                body = "Monitor assets missing. Rebuild the project.";
            }
        }
        else
        {
            status = "404 Not Found";
            body = "Not found";
        }
        std::string headers = "HTTP/1.1 " + status + "\r\nContent-Type: " + mime
            + "\r\nContent-Length: " + std::to_string(body.size())
            + "\r\nCache-Control: no-store\r\nConnection: close\r\nX-Content-Type-Options: nosniff\r\n\r\n";
        SendAll(client, headers + body);
        closesocket(client);
    }
}

bool WorldMonitor::SelfTest()
{
    bool ok = true;
    auto check = [&](bool value, const char* name)
    {
        std::cout << (value ? "PASS " : "FAIL ") << name << '\n';
        ok &= value;
    };
    LevelOne level(2026);
    level.society.Initialize(level.Graph(), 2026);
    auto initial = Snapshot(level, Location{}, 25, 2026);
    check(initial.find("\"npcs\":11,\"npcsAlive\":11") != std::string::npos
              && initial.find("\"monsters\":3,\"monstersAlive\":3") != std::string::npos,
          "monitor separates human and monster counts");
    auto& secondTown = level.Graph().Spawn<TownActor>(NoActor, "두 번째 마을");
    auto& nested = level.Graph().Spawn<Actor>(secondTown.Id());
    auto& npc = level.Graph().Spawn<NpcActor>(nested.Id(), "인용\"과 줄바꿈\n", NpcJob::Merchant);
    npc.gold = 37;
    npc.enabled = false;
    auto snapshot = Snapshot(level, Location{}, 25, 2026);
    check(snapshot.find("\"name\":\"두 번째 마을\"") != std::string::npos
              && snapshot.find("\"totalGold\":37") != std::string::npos
              && snapshot.find("\"npcsInactive\":1") != std::string::npos,
          "monitor aggregates nested village membership and inactive NPCs");
    check(snapshot.find("인용\\\"과 줄바꿈\\u000a") != std::string::npos,
          "monitor JSON escapes quotes and control characters");
    level.Graph().Destroy(npc.Id());
    check(Snapshot(level, Location{}, 25, 2026).find("\"npcs\":12") == std::string::npos,
          "monitor excludes actors pending destruction");
    check(Number(std::numeric_limits<double>::infinity()) == "null",
          "monitor serializes non-finite numbers as JSON null");
    return ok;
}
