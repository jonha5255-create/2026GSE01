/*
Copyright 2022 Lee Taek Hee (Tech University of Korea)
This program is free software: you can redistribute it and/or modify
it under the terms of the What The Hell License. Do it plz.
This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY.
Prototype scene extensions, 2026.
*/
#include "stdafx.h"
#include <windows.h>
#include "PrototypeRenderer.h"
#include "PrototypeWorld.h"
#include "FrameProfiler.h"
#include "FramePacer.h"
#include "RenderDiagnostics.h"
#include "WorldMonitor.h"
#include "AssetPreview.h"
#include "GlfwPlatform.h"
#include "WorldArt.h"
#include <algorithm>
#include <chrono>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

namespace
{
    PrototypeRenderer renderer;
    GlfwPlatform platform;
    std::unique_ptr<PrototypeWorld> world;
    std::unique_ptr<Art::AssetPreview> assetGallery;
    float assetTime = 0;
    bool assetPaused = false;
    std::string assetError;
    int width = 1280, height = 800;
    bool initialized = false;
    bool profileTest = false;
    bool profileYokai = false;
    int profileFrames = 0;
    int profileFrameLimit = 180;
    int fpsLimit = 60;
    FramePacer framePacer;
    bool effectKeys[256] = {};
    auto lastTick = std::chrono::steady_clock::now();
    std::filesystem::path savePath;
    bool gameSession = false;
    bool saveErrorReported = false;
    int savedCheckpoint = 0;
    auto lastSave = std::chrono::steady_clock::now();

    void SaveProgress()
    {
        if (!gameSession || savePath.empty())
        {
            return;
        }
        lastSave = std::chrono::steady_clock::now();
        savedCheckpoint = world->ProgressCheckpoint();
        try
        {
            world->SaveGame(savePath);
            saveErrorReported = false;
        }
        catch (const std::exception& error)
        {
            if (!saveErrorReported)
            {
                std::cerr << "[SAVE] " << error.what() << '\n';
                RenderDiagnostics::Get().Event("save_error", error.what());
                saveErrorReported = true;
            }
        }
    }

    void Close()
    {
        WorldMonitor::Get().Stop();
        if (initialized)
        {
            SaveProgress();
            RenderDiagnostics::Get().Event("session_end", "renderer shutdown");
            RenderDiagnostics::Get().Flush();
            renderer.Shutdown();
            initialized = false;
        }
    }

    void DrawContents()
    {
        if (assetGallery)
        {
            try
            {
                assetGallery->Draw(renderer, assetTime);
                assetError.clear();
            }
            catch (const std::exception& error)
            {
                if (assetError != error.what())
                {
                    assetError = error.what();
                    std::cerr << "[ASSET] " << assetError << '\n';
                    RenderDiagnostics::Get().Event("asset_error", assetError);
                }
                renderer.Text(25, 90, "에셋 로딩 오류: " + assetError, Color(255, 130, 130), .65f);
            }
        }
        else
        {
            world->Draw(renderer);
        }
        renderer.CompositeScene();
        if (!assetGallery)
        {
            world->DrawUI(renderer);
        }
    }

    void Display()
    {
        if (!initialized)
        {
            return;
        }
        if (!renderer.Begin(width, height))
        {
            platform.RequestClose();
            return;
        }
        DrawContents();
        renderer.Flush();
        FrameProfiler::Get().EndRender();
        auto presentStart = std::chrono::steady_clock::now();
        platform.Swap();
        FrameProfiler::Get().RecordPresent(std::chrono::duration<double, std::milli>(
                                               std::chrono::steady_clock::now() - presentStart)
                                               .count());
        FrameProfiler::Get().RecordPacing(framePacer.Wait(fpsLimit));
        FrameProfiler::Get().EndFrame();
        if (profileTest && ++profileFrames >= profileFrameLimit)
        {
            platform.RequestClose();
        }
    }

    void Resize(int w, int h)
    {
        RenderDiagnostics::Get().Event("resize", std::to_string(w) + "x" + std::to_string(h));
        width = std::max(w, 1);
        height = std::max(h, 1);
    }

    void KeyDown(unsigned char key, int, int)
    {
        if (key == 27)
        {
            platform.RequestClose();
            return;
        }
        if (key >= 'A' && key <= 'Z')
        {
            key += 32;
        }
        if (assetGallery)
        {
            if (!effectKeys[key])
            {
                if (key == ' ')
                {
                    assetPaused = !assetPaused;
                }
                if (key >= '1' && key <= '9')
                {
                    assetGallery->SetAction(key - '1');
                    assetTime = 0;
                }
                if (key == 'r')
                {
                    try
                    {
                        assetGallery->Reload();
                    }
                    catch (const std::exception& error)
                    {
                        std::cerr << "[ASSET] " << error.what() << '\n';
                    }
                }
            }
            effectKeys[key] = true;
            return;
        }
        if (!effectKeys[key])
        {
            RenderDiagnostics::Get().Event("key_down", std::string(1, char(key)));
            PostSettings& post = renderer.Effects();
            if (key == 'h')
            {
                post.enabled = !post.enabled;
            }
            if (key == 'b')
            {
                post.bloom = !post.bloom;
            }
            if (key == 'n')
            {
                post.vignette = !post.vignette;
            }
            if (key == 'm')
            {
                post.edgeBlur = !post.edgeBlur;
            }
            if (key == '[')
            {
                post.exposure = std::max(.25f, post.exposure - .1f);
            }
            if (key == ']')
            {
                post.exposure = std::min(3.f, post.exposure + .1f);
            }
            if (key == '0')
            {
                post = PostSettings{};
            }
        }
        effectKeys[key] = true;
        if (gameSession && key == 'r')
        {
            world->Respawn();
            return;
        }
        world->Key(key, true);
    }

    void KeyUp(unsigned char key, int, int)
    {
        RenderDiagnostics::Get().Event("key_up", std::string(1, char(key)));
        if (key >= 'A' && key <= 'Z')
        {
            key += 32;
        }
        effectKeys[key] = false;
        world->Key(key, false);
    }

    void MouseButton(int button, int state, int x, int y)
    {
        if (assetGallery)
        {
            return;
        }
        if (initialized && world && button == GLFW_MOUSE_BUTTON_LEFT && state == GLFW_PRESS)
        {
            RenderDiagnostics::Get().Event("mouse_attack",
                                           std::to_string(x) + "," + std::to_string(y));
            world->MouseAttack(x, y, width, height);
        }
        else if (initialized && world && button == GLFW_MOUSE_BUTTON_RIGHT && state == GLFW_PRESS)
        {
            RenderDiagnostics::Get().Event("mouse_skill", "right_button_down");
            world->MouseSkill();
        }
    }

    void UpdateGame()
    {
        if (!initialized)
        {
            return;
        }
        auto now = std::chrono::steady_clock::now();
        float dt = std::chrono::duration<float>(now - lastTick).count();
        lastTick = now;
        auto updateStart = std::chrono::steady_clock::now();
        if (assetGallery)
        {
            if (!assetPaused)
            {
                assetTime += std::min(dt, .1f);
            }
        }
        else
        {
            world->Update(dt);
            if (gameSession && !savePath.empty()
                && (world->ProgressCheckpoint() != savedCheckpoint
                    || now - lastSave >= std::chrono::seconds(5)))
            {
                SaveProgress();
            }
        }
        FrameProfiler::Get().RecordUpdate(std::chrono::duration<double, std::milli>(
                                              std::chrono::steady_clock::now() - updateStart)
                                              .count());
    }

    std::wstring ExecutableDirectory()
    {
        wchar_t path[32768] = {};
        GetModuleFileNameW(nullptr, path, 32768);
        std::wstring full(path);
        return full.substr(0, full.find_last_of(L"\\/") + 1);
    }

    bool Snapshot(const wchar_t* name)
    {
        if (!renderer.Begin(width, height))
        {
            return false;
        }
        DrawContents();
        renderer.Flush();
        glFinish();
        bool ok = renderer.Capture(ExecutableDirectory() + name);
        GLenum error = glGetError();
        std::cout << "Snapshot: " << ok << " GL error: " << error << '\n';
        std::cout << "DrawCalls/frame: " << FrameProfiler::Get().DrawCalls() << '\n';
        return ok && error == GL_NO_ERROR;
    }

    bool VerifyPlayerMotion()
    {
        bool ok = true;
        auto art = Art::WorldArt::Shared();
        const auto saved = renderer.Effects();
        renderer.Effects().bloom = false;
        renderer.Effects().vignette = false;
        renderer.Effects().edgeBlur = false;
        for (auto form : {YokaiForm::Sword, YokaiForm::Ranged, YokaiForm::Claw})
        {
            LevelOne scene;
            auto& actor = scene.Player();
            auto& item = scene.Weapon();
            actor.SelectYokai(form);
            actor.weapon = true;
            actor.weaponRaise = PlayerActor::WeaponRaiseDuration;
            auto services = scene.Context(
                [](float, float, float)
                {
                    return true;
                });
            services.movePlayer = [](float, float)
            {
            };
            for (int frame = 0; frame < 120; ++frame)
            {
                actor.moveInput = frame < 12 || frame >= 96 ? FarmPoint{}
                                  : frame < 48              ? FarmPoint{1, 0}
                                                            : FarmPoint{0, -1};
                actor.Update(1.f / 60, services);
                item.Update(1.f / 60, services);
                if (frame == 20 || frame == 66 || (form == YokaiForm::Claw && frame == 34))
                {
                    if (form == YokaiForm::Ranged)
                    {
                        item.aim = Unit({1, .3f});
                        item.BeginAttackAnimation(form, .20f);
                        actor.ApplyShotRecoil(item.aim);
                    }
                    else
                    {
                        item.StartSwing({80, 24});
                    }
                }
                ok = renderer.Begin(640, 480) && ok;
                renderer.Rect(0, 0, 640, 480, Color(22, 32, 39));
                renderer.Ellipse({320, 380}, 60, 18, Color(0, 0, 0, .6f));
                art->Player(renderer, actor, item, {320, 380}, 2.6f, actor.animationClock);
                renderer.CompositeScene();
                renderer.Text(20,
                              20,
                              std::string(actor.YokaiName()) + " / 장착, 이동 및 공격 전환 검증",
                              Color(220, 235, 230),
                              .7f);
                renderer.Text(
                    20, 448, "대기 / 이동 및 공격 / 방향 전환 / 정지", Color(144, 184, 181), .6f);
                renderer.Flush();
                if (frame % 3 == 0)
                {
                    auto file = ExecutableDirectory() + L"player-motion-"
                                + std::to_wstring(int(form)) + L"-" + std::to_wstring(frame)
                                + L".bmp";
                    ok = renderer.Capture(file) && ok;
                }
                ok = glGetError() == GL_NO_ERROR && ok;
                if (frame > 3)
                {
                    ok = FrameProfiler::Get().Counters().gpuModelUploads == 0 && ok;
                }
            }
        }
        renderer.Effects() = saved;
        std::cout << (ok ? "PASS " : "FAIL ")
                  << "player weapon motion captures and immutable GPU mesh reuse\n";
        return ok;
    }

    bool VerifyYokaiArt()
    {
        auto art = Art::WorldArt::Shared();
        const auto effects = renderer.Effects();
        renderer.Effects().edgeBlur = false;
        renderer.Effects().vignette = false;
        bool ok = true;
        auto capture = [&](const std::wstring& name)
        {
            renderer.Flush();
            return renderer.Capture(ExecutableDirectory() + name) && glGetError() == GL_NO_ERROR;
        };
        auto tick = [](LevelOne& scene, GameplayContext& services)
        {
            constexpr float dt = 1.f / 60;
            scene.Player().Update(dt, services);
            scene.Weapon().Update(dt, services);
            for (auto* effect : scene.Graph().Query<WeaponVfxActor>())
            {
                effect->Update(dt, services);
            }
            scene.Graph().CollectDestroyed();
        };
        const int levels[] = {1, 3, 6, 9};
        for (bool armed : {false, true})
        {
            ok = renderer.Begin(1280, 800) && ok;
            renderer.Rect(0, 0, 1280, 800, Color(22, 32, 39));
            for (int family = 0; family < 3; ++family)
            {
                LevelOne scene;
                auto& player = scene.Player();
                player.SelectYokai(YokaiForm(family));
                player.weapon = armed;
                for (int stage = 0; stage < 4; ++stage)
                {
                    auto uploads = FrameProfiler::Get().Counters().gpuModelUploads;
                    player.yokaiGrowth[family].level = levels[stage];
                    Point at{stage * 320.f + 116, family * 260.f + 228};
                    art->Player(renderer, player, scene.Weapon(), at, 2, 0);
                    if (!armed)
                    {
                        art->Companion(renderer, player, at, 2, 0);
                    }
                    if (stage > 0)
                    {
                        ok = FrameProfiler::Get().Counters().gpuModelUploads == uploads && ok;
                    }
                }
            }
            renderer.CompositeScene();
            for (int family = 0; family < 3; ++family)
            {
                for (int stage = 0; stage < 4; ++stage)
                {
                    renderer.Text(stage * 320.f + 20,
                                  family * 260.f + 20,
                                  Art::YokaiAssets::Shared().Family(family).name + " / LV."
                                      + std::to_string(levels[stage]),
                                  Color(218, 235, 231),
                                  .74f);
                }
            }
            ok = capture(armed ? L"yokai-weapons.bmp" : L"yokai-companions.bmp") && ok;
        }
        for (int family = 0; family < 3; ++family)
        {
            for (bool skill : {false, true})
            {
                LevelOne scene;
                auto& actor = scene.Player();
                auto& item = scene.Weapon();
                actor.SelectYokai(YokaiForm(family));
                actor.weapon = true;
                actor.yokaiGrowth[family].level = 9;
                auto services = scene.Context(
                    [](float, float, float)
                    {
                        return true;
                    });
                auto& target = scene.SpawnEnemy({100, 20});
                target.health = target.maximumHealth = 500;
                if (skill)
                {
                    ok = item.CastSkill(services) && ok;
                }
                else if (family == 1)
                {
                    ok = item.ShootAt(target, services) && ok;
                }
                else
                {
                    item.StartSwing(target.Position());
                }
                int steps = skill ? 20 : family == 0 ? 17 : family == 1 ? 3 : 14;
                for (int i = 0; i < steps; ++i)
                {
                    tick(scene, services);
                }
                ok = renderer.Begin(720, 600) && ok;
                renderer.Rect(0, 0, 720, 600, Color(22, 32, 39));
                Point origin{350, 410};
                float zoom = 1.8f;
                art->Player(renderer, actor, item, origin, zoom, actor.animationClock);
                for (auto* effect : scene.Graph().Query<WeaponVfxActor>())
                {
                    auto at = effect->Position();
                    art->Vfx(renderer,
                             *effect,
                             {origin.x + (at.x - at.y) * .85f * zoom,
                              origin.y + (at.x + at.y) * .425f * zoom},
                             zoom);
                }
                ok = FrameProfiler::Get().Counters().vfxDraws > 0 && ok;
                renderer.CompositeScene();
                renderer.Text(20,
                              20,
                              Art::YokaiAssets::Shared().Family(family).name
                                  + (skill ? " / 스킬" : " / 일반 공격"),
                              Color(232, 240, 235),
                              .8f);
                ok = capture(L"yokai-vfx-" + std::to_wstring(family)
                             + (skill ? L"-skill.bmp" : L"-basic.bmp"))
                     && ok;
            }
        }
        std::cout
            << (ok ? "PASS " : "FAIL ")
            << "three yokai families: four evolution stages, cached meshes, six attack VFX captures\n";
        renderer.Effects() = effects;
        return ok;
    }

    bool VerifyPostEffects()
    {
        const PostSettings saved = renderer.Effects();
        auto draw = [&]()
        {
            std::vector<unsigned char> pixels(static_cast<size_t>(width) * height * 3);
            if (!renderer.Begin(width, height))
            {
                return std::vector<unsigned char>{};
            }
            world->Draw(renderer);
            renderer.CompositeScene();
            // Opaque UI probe must survive exposure and edge effects unchanged.
            renderer.Rect(float(width - 26), float(height - 26), 16, 16, Color(80, 160, 220));
            renderer.Flush();
            glReadBuffer(GL_BACK);
            glPixelStorei(GL_PACK_ALIGNMENT, 1);
            glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
            glPixelStorei(GL_PACK_ALIGNMENT, 4);
            return pixels;
        };
        auto changed = [](const std::vector<unsigned char>& a, const std::vector<unsigned char>& b)
        {
            if (a.empty() || a.size() != b.size())
            {
                return false;
            }
            size_t count = 0;
            for (size_t i = 0; i < a.size(); ++i)
            {
                if (std::abs(int(a[i]) - int(b[i])) > 2)
                {
                    ++count;
                }
            }
            return count > 100;
        };
        bool ok = true;
        auto check = [&](bool pass, const char* name)
        {
            std::cout << (pass ? "PASS " : "FAIL ") << name << '\n';
            ok = ok && pass;
        };
        PostSettings neutral = saved;
        neutral.bloom = neutral.vignette = neutral.edgeBlur = false;
        renderer.Effects() = neutral;
        auto baseline = draw();
        const auto baselineCalls = FrameProfiler::Get().DrawCalls();
        renderer.Effects().bloom = true;
        auto bloom = draw();
        check(FrameProfiler::Get().DrawCalls() == baselineCalls + 4,
              "draw counter includes all four bloom GL submissions");
        check(changed(baseline, bloom), "bloom changes rendered highlights");
        renderer.Effects() = neutral;
        renderer.Effects().vignette = true;
        auto vignette = draw();
        check(changed(baseline, vignette), "vignette changes scene edges");
        renderer.Effects() = neutral;
        renderer.Effects().edgeBlur = true;
        auto blur = draw();
        check(FrameProfiler::Get().DrawCalls() == baselineCalls + 4,
              "draw counter resets per frame and includes edge blur");
        check(changed(baseline, blur), "edge blur changes scene edges");
        renderer.Effects() = saved;
        renderer.Effects().exposure = 2.5f;
        auto exposed = draw();
        check(changed(baseline, exposed), "exposure changes tone mapping");
        size_t probe = (static_cast<size_t>(20) * width + width - 20) * 3;
        bool ui = baseline.size() > probe + 2 && exposed.size() > probe + 2;
        for (int i = 0; ui && i < 3; ++i)
        {
            ui = baseline[probe + i] == exposed[probe + i];
        }
        check(ui, "UI is composited after post processing");
        renderer.Effects().enabled = false;
        auto bypassA = draw();
        renderer.Effects() = neutral;
        renderer.Effects().enabled = false;
        auto bypassB = draw();
        check(FrameProfiler::Get().DrawCalls() == baselineCalls,
              "draw counter excludes disabled post passes");
        check(!bypassA.empty() && bypassA == bypassB, "master bypass ignores all effect settings");
        renderer.Effects() = saved;
        return ok && glGetError() == GL_NO_ERROR;
    }
} // namespace

int main(int argc, char** argv)
{
    bool smoke = false, selfTest = false, rebuildCache = false, legacy = false;
    bool monitorTest = false;
    bool monitorEnabled = true;
    bool platformTest = false;
    bool playerMotionTest = false;
    bool yokaiArtTest = false;
    int swapInterval = 0;
    bool assetTest = false, assetSmoke = false, assetPreview = false;
    bool noSave = false;
    std::filesystem::path assetRoot;
    unsigned short monitorPort = 8765;
    for (int i = 1; i < argc; ++i)
    {
        smoke |= std::string(argv[i]) == "--smoke-test";
        selfTest |= std::string(argv[i]) == "--self-test";
        profileTest |= std::string(argv[i]) == "--profile-test";
        profileYokai |= std::string(argv[i]) == "--profile-yokai";
        rebuildCache |= std::string(argv[i]) == "--rebuild-mesh-cache";
        legacy |= std::string(argv[i]) == "--level-one";
        noSave |= std::string(argv[i]) == "--no-save";
        monitorEnabled &= std::string(argv[i]) != "--no-monitor";
        monitorTest |= std::string(argv[i]) == "--monitor-test";
        platformTest |= std::string(argv[i]) == "--platform-test";
        playerMotionTest |= std::string(argv[i]) == "--player-motion-test";
        yokaiArtTest |= std::string(argv[i]) == "--yokai-art-test";
        assetTest |= std::string(argv[i]) == "--asset-test";
        assetSmoke |= std::string(argv[i]) == "--asset-smoke-test";
        assetPreview |= std::string(argv[i]) == "--asset-preview";
        std::string argument = argv[i];
        if (argument.find("--fps-limit=") == 0)
        {
            try
            {
                size_t consumed = 0;
                auto value = argument.substr(12);
                fpsLimit = std::stoi(value, &consumed);
                if (consumed != value.size() || fpsLimit < 0 || fpsLimit > 1000)
                {
                    throw std::invalid_argument("fps limit");
                }
            }
            catch (...)
            {
                std::cerr << "--fps-limit must be between 0 and 1000 (0 = uncapped)\n";
                return 8;
            }
        }
        if (argument.find("--profile-frames=") == 0)
        {
            try
            {
                size_t consumed = 0;
                auto value = argument.substr(17);
                profileFrameLimit = std::stoi(value, &consumed);
                if (consumed != value.size() || profileFrameLimit < 1 || profileFrameLimit > 36000)
                {
                    throw std::invalid_argument("frame count");
                }
            }
            catch (...)
            {
                std::cerr << "--profile-frames must be between 1 and 36000\n";
                return 8;
            }
        }
        if (argument.find("--vsync=") == 0)
        {
            auto value = argument.substr(8);
            if (value != "0" && value != "1")
            {
                std::cerr << "--vsync must be 0 or 1\n";
                return 8;
            }
            swapInterval = value == "1" ? 1 : 0;
        }
        if (argument.find("--asset-root=") == 0)
        {
            assetRoot = std::filesystem::path(argument.substr(13));
        }
        if (argument.find("--monitor-port=") == 0)
        {
            try
            {
                size_t consumed = 0;
                auto port = std::stoul(argument.substr(15), &consumed);
                if (!port || port > 65535 || consumed != argument.size() - 15)
                {
                    throw std::invalid_argument("port");
                }
                monitorPort = static_cast<unsigned short>(port);
            }
            catch (...)
            {
                std::cerr << "Monitor port must be an integer from 1 to 65535\n";
                return 8;
            }
        }
    }
    if (assetTest || assetSmoke || assetPreview)
    {
        try
        {
            if (assetRoot.empty())
            {
                assetRoot = Art::AssetLibrary::Discover();
            }
            if ((assetTest || assetSmoke) && !Art::AssetLibrary::SelfTest(assetRoot))
            {
                return 10;
            }
        }
        catch (const std::exception& error)
        {
            std::cerr << "[ASSET] " << error.what() << '\n';
            return 10;
        }
        if (assetTest && !assetSmoke && !assetPreview)
        {
            return 0;
        }
    }
    if ((selfTest || smoke)
        && (!GlfwPlatform::SelfTest() || !PrototypeWorld::SelfTest() || !WorldMonitor::SelfTest()))
    {
        return 2;
    }
    if (selfTest && !smoke)
    {
        return 0;
    }
    if (monitorTest)
    {
        uint32_t monitorSeed = 2026;
        for (int i = 1; i < argc; ++i)
        {
            std::string argument = argv[i];
            if (argument.find("--seed=") == 0)
            {
                try
                {
                    monitorSeed = static_cast<uint32_t>(std::stoul(argument.substr(7)));
                }
                catch (...)
                {
                    std::cerr << "Invalid seed\n";
                    return 7;
                }
            }
        }
        RenderDiagnostics::Get().Open("monitor-test");
        world.reset(new PrototypeWorld(monitorSeed, !legacy));
        if (!monitorEnabled || !WorldMonitor::Get().Start(monitorPort))
        {
            return 9;
        }
        auto until = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        auto previous = std::chrono::steady_clock::now();
        while (std::chrono::steady_clock::now() < until)
        {
            auto now = std::chrono::steady_clock::now();
            world->Update(std::chrono::duration<float>(now - previous).count());
            previous = now;
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
        }
        WorldMonitor::Get().Stop();
        RenderDiagnostics::Get().Event("session_end", "headless monitor test shutdown");
        RenderDiagnostics::Get().Flush();
        std::cout << "[MONITOR] Headless integration test finished\n";
        return 0;
    }
    platform.framebuffer = Resize;
    platform.key = [](unsigned char key, bool down)
    {
        if (down)
        {
            KeyDown(key, 0, 0);
        }
        else
        {
            KeyUp(key, 0, 0);
        }
    };
    platform.mouse = [](int button, bool down, int x, int y)
    {
        MouseButton(button, down ? GLFW_PRESS : GLFW_RELEASE, x, y);
    };
    if (!platform.Open("2026 GSE | Nameless District - GLFW", width, height))
    {
        return 3;
    }
    platform.SetSwapInterval(swapInterval);
    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK || !GLEW_VERSION_3_3)
    {
        std::cerr << "OpenGL 3.3 required\n";
        platform.Close();
        return 4;
    }
    while (glGetError() != GL_NO_ERROR)
    {
    }
    RenderDiagnostics::Get().Open(smoke ? "smoke-test" : profileTest ? "profile-test" : "game");
    renderer.RebuildDiskCache(rebuildCache);
    RenderDiagnostics::Get().Event("cache_policy",
                                   rebuildCache ? "force_rebuild" : "load_valid_files");
    if (!renderer.Initialize())
    {
        std::cerr << "Renderer initialization failed\n";
        renderer.Shutdown();
        platform.Close();
        return 5;
    }
    initialized = true;
    uint32_t seed =
        smoke ? 2026
              : static_cast<uint32_t>(std::chrono::system_clock::now().time_since_epoch().count());
    bool explicitSeed = false;
    for (int i = 1; i < argc; ++i)
    {
        std::string argument = argv[i];
        if (argument.find("--seed=") == 0)
        {
            try
            {
                seed = static_cast<uint32_t>(std::stoul(argument.substr(7)));
                explicitSeed = true;
            }
            catch (...)
            {
                std::cerr << "Invalid seed\n";
                Close();
                platform.Close();
                return 7;
            }
        }
    }
    const bool playing = !smoke && !profileTest && !platformTest && !playerMotionTest
                         && !yokaiArtTest && !assetPreview && !assetSmoke;
    if (playing && !noSave)
    {
        wchar_t localAppData[32768]{};
        DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData, 32768);
        if (length > 0 && length < 32768)
        {
            std::wstring slot = legacy ? L"level-one" : L"world";
            if (explicitSeed)
            {
                slot += L"-" + std::to_wstring(seed);
            }
            savePath = std::filesystem::path(localAppData) / L"2026GSE01" / (slot + L".json");
            try
            {
                if (std::filesystem::exists(savePath))
                {
                    world = PrototypeWorld::LoadGame(savePath, !legacy);
                    seed = world->Seed();
                    std::cout << "[SAVE] Previous progress loaded.\n";
                }
            }
            catch (const std::exception& error)
            {
                std::cerr << "[SAVE] Could not load progress; existing file preserved. "
                          << error.what() << '\n';
                RenderDiagnostics::Get().Event("load_error", error.what());
                savePath.clear();
            }
        }
        else
        {
            std::cerr << "[SAVE] LOCALAPPDATA unavailable.\n";
        }
    }
    if (!world)
    {
        world = std::make_unique<PrototypeWorld>(seed, !legacy);
    }
    if (profileTest && profileYokai)
    {
        world->PreviewRuins();
        world->PreviewYokai(YokaiForm::Ranged, 9, true, false);
    }
    if (assetPreview || assetSmoke)
    {
        try
        {
            assetGallery = std::make_unique<Art::AssetPreview>(assetRoot);
            renderer.Effects().edgeBlur = false;
            renderer.Effects().vignette = false;
            renderer.Effects().bloom = false;
        }
        catch (const std::exception& error)
        {
            std::cerr << "[ASSET] " << error.what() << '\n';
            Close();
            platform.Close();
            return 10;
        }
    }
    if (monitorEnabled && !smoke && !profileTest && !assetGallery)
    {
        WorldMonitor::Get().Start(monitorPort);
    }
    if (!assetGallery)
    {
        world->PrepareArt();
    }
    RenderDiagnostics::Get().Event(
        "environment",
        std::string("seed=") + std::to_string(seed) + "; resolution=" + std::to_string(width) + "x"
            + std::to_string(height)
            + "; OpenGL=" + reinterpret_cast<const char*>(glGetString(GL_VERSION)) + "; GPU="
            + reinterpret_cast<const char*>(glGetString(GL_RENDERER)) + "; framework=GLFW "
            + glfwGetVersionString() + "; swap_interval=" + std::to_string(swapInterval)
            + "; fps_limit=" + std::to_string(fpsLimit));
    std::cout << "OpenGL " << glGetString(GL_VERSION) << " / " << glGetString(GL_RENDERER) << '\n';
    if (platformTest)
    {
        bool ok = GlfwPlatform::SelfTest();
        ok = ok && glfwGetCurrentContext() == platform.Window()
             && glfwGetWindowAttrib(platform.Window(), GLFW_OPENGL_PROFILE)
                    == GLFW_OPENGL_CORE_PROFILE;
        for (auto size : {std::pair<int, int>{960, 600}, {961, 601}, {1280, 800}})
        {
            platform.Resize(size.first, size.second);
            int fbWidth, fbHeight;
            glfwGetFramebufferSize(platform.Window(), &fbWidth, &fbHeight);
            ok = ok && width == fbWidth && height == fbHeight;
            ok = Snapshot(L"glfw-platform-resize.bmp") && ok;
        }
        platform.RequestClose();
        ok = platform.ShouldClose() && ok;
        Close();
        platform.Close();
        ok = platform.Window() == nullptr && platform.ShouldClose() && ok;
        std::cout << (ok ? "PASS " : "FAIL ")
                  << "GLFW core context, framebuffer resize and clean shutdown\n";
        return ok ? 0 : 12;
    }
    if (playerMotionTest)
    {
        bool ok = VerifyPlayerMotion();
        Close();
        platform.Close();
        return ok ? 0 : 13;
    }
    if (yokaiArtTest)
    {
        bool ok = VerifyYokaiArt();
        Close();
        platform.Close();
        return ok ? 0 : 14;
    }
    if (assetSmoke)
    {
        assetTime = .05f;
        bool ok = Snapshot(L"lowpoly-assets-pose-a.bmp") && assetError.empty();
        std::vector<unsigned char> before(static_cast<size_t>(width) * height * 3),
            after(before.size());
        glReadBuffer(GL_BACK);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, before.data());
        assetTime = .45f;
        ok = Snapshot(L"lowpoly-assets-pose-b.bmp") && assetError.empty() && ok;
        glReadBuffer(GL_BACK);
        glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, after.data());
        glPixelStorei(GL_PACK_ALIGNMENT, 4);
        ok = before != after && ok;
        std::cout << (ok ? "PASS " : "FAIL ")
                  << "real GLB skeletal animation changes rendered pixels\n";
        for (int action = 0; action < 9; ++action)
        {
            assetGallery->SetAction(action);
            assetTime = .4f;
            auto file = L"lowpoly-assets-action-" + std::to_wstring(action + 1) + L".bmp";
            ok = Snapshot(file.c_str()) && assetError.empty() && ok;
        }
        Close();
        platform.Close();
        return ok ? 0 : 11;
    }
    if (smoke)
    {
        for (int i = 0; i < 60; ++i)
        {
            world->Update(1.f / 60);
        }
        bool ok = renderer.VerifyMeshCache();
        ok = Art::WorldArt::Shared()->VerifyBuildingPlacement() && ok;
        ok = VerifyPostEffects() && ok;
        ok = Snapshot(L"prototype-companion.bmp") && ok;
        ok = Snapshot(L"living-world-town.bmp") && ok;
        world->Key('m', true);
        world->Key('m', false);
        ok = Snapshot(L"living-world-expanded-map.bmp") && ok;
        world->Key('m', true);
        world->Key('m', false);
        {
            auto savedWorld = std::move(world);
            world = std::make_unique<PrototypeWorld>(2026);
            world->PreviewYokai(YokaiForm::Sword, 9, false);
            ok = Snapshot(L"yokai-evolved-companion.bmp") && ok;
            const wchar_t* skills[] = {
                L"yokai-sword-skill.bmp", L"yokai-ranged-skill.bmp", L"yokai-claw-skill.bmp"};
            for (int form = 0; form < 3; ++form)
            {
                world->PreviewYokai(YokaiForm(form), 9, true);
                ok = Snapshot(skills[form]) && ok;
                for (int growth : {1, 3, 6, 9})
                {
                    for (bool equipped : {false, true})
                    {
                        world->PreviewYokai(YokaiForm(form), growth, equipped, false);
                        std::wstring file = L"yokai-evolution-" + std::to_wstring(form) + L"-lv"
                                            + std::to_wstring(growth)
                                            + (equipped ? L"-weapon.bmp" : L"-companion.bmp");
                        ok = Snapshot(file.c_str()) && ok;
                    }
                }
            }
            world = std::move(savedWorld);
        }
        {
            auto savedWorld = std::move(world);
            world = std::make_unique<PrototypeWorld>(2026);
            world->PreviewExpedition();
            ok = Snapshot(L"living-world-expedition.bmp") && ok;
            world = std::make_unique<PrototypeWorld>(2026);
            world->PreviewRoadJunction();
            ok = Snapshot(L"living-world-road-junction.bmp") && ok;
            world = std::make_unique<PrototypeWorld>(2026);
            world->PreviewRuins();
            ok = Snapshot(L"living-world-grounded-ruins.bmp") && ok;
            for (int region = 0; region < int(BossArenas.size()); ++region)
            {
                for (bool released : {false, true})
                {
                    world = std::make_unique<PrototypeWorld>(2026);
                    world->PrepareArt();
                    world->PreviewArena(region, released);
                    auto file = L"story-boss-arena-" + std::to_wstring(region)
                                + (released ? L"-release.bmp" : L".bmp");
                    ok = Snapshot(file.c_str()) && ok;
                }
            }
            world = std::move(savedWorld);
        }
        for (int form = 0; form < 3; ++form)
        {
            unsigned char key = static_cast<unsigned char>('1' + form);
            KeyDown(key, 0, 0);
            KeyUp(key, 0, 0);
            KeyDown('q', 0, 0);
            KeyUp('q', 0, 0);
            const wchar_t* images[] = {L"yokai-sword.bmp", L"yokai-ranged.bmp", L"yokai-claw.bmp"};
            ok = Snapshot(images[form]) && ok;
        }
        KeyDown('1', 0, 0);
        KeyUp('1', 0, 0);
        auto pixels = [&]()
        {
            std::vector<unsigned char> result(size_t(width) * height * 3);
            glReadBuffer(GL_BACK);
            glPixelStorei(GL_PACK_ALIGNMENT, 1);
            glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, result.data());
            glPixelStorei(GL_PACK_ALIGNMENT, 4);
            return result;
        };
        for (auto form : {YokaiForm::Sword, YokaiForm::Claw})
        {
            world->PreviewAttack(form, .2f);
            ok =
                Snapshot(form == YokaiForm::Sword ? L"sword-windup.bmp" : L"claw-windup.bmp") && ok;
            auto before = pixels();
            world->PreviewAttack(form, .55f);
            ok = Snapshot(form == YokaiForm::Sword ? L"sword-swing.bmp" : L"claw-swing.bmp") && ok;
            bool changed = before != pixels();
            std::cout << (changed ? "PASS " : "FAIL ")
                      << "melee animation changes rendered pixels\n";
            ok = changed && ok;
        }
        KeyDown('1', 0, 0);
        KeyUp('1', 0, 0);
        ok = renderer.VerifyHDR() && ok;
        KeyDown('h', 0, 0);
        KeyUp('h', 0, 0);
        ok = Snapshot(L"prototype-post-off.bmp") && ok;
        KeyDown('h', 0, 0);
        KeyUp('h', 0, 0);
        KeyDown('q', 0, 0);
        KeyUp('q', 0, 0);
        for (int i = 0; i < 60; ++i)
        {
            world->Update(1.f / 60);
        }
        for (int i = 0; i < 600; ++i)
        {
            world->Update(1.f / 60);
        }
        ok = Snapshot(L"level1-farming.bmp") && ok;
        ok = Snapshot(L"prototype-weapon.bmp") && ok;
        KeyDown('d', 0, 0);
        for (int i = 0; i < 35; ++i)
        {
            world->Update(1.f / 60);
        }
        KeyUp('d', 0, 0);
        KeyDown('e', 0, 0);
        KeyUp('e', 0, 0);
        for (int i = 0; i < 30; ++i)
        {
            world->Update(1.f / 60);
        }
        world->PreviewBoss();
        for (int i = 0; i < 125; ++i)
        {
            world->Update(1.f / 60);
        }
        ok = Snapshot(L"level1-boss.bmp") && ok;
        KeyDown('v', 0, 0);
        KeyUp('v', 0, 0);
        platform.Resize(960, 600);
        ok = Snapshot(L"prototype-resized.bmp") && ok;
        // Exercise odd half-resolution sizes, then restore the original targets.
        platform.Resize(961, 601);
        ok = Snapshot(L"prototype-resized-odd.bmp") && ok;
        platform.Resize(1280, 800);
        ok = Snapshot(L"prototype-restored.bmp") && ok;
        const auto& cache = renderer.MeshStats();
        std::cout << "Mesh cache: " << cache.hits << " hits, " << cache.uploads << " admissions, "
                  << cache.evictions << " evictions, " << cache.bytes << " resident bytes\n";
        Close();
        platform.Close();
        return ok ? 0 : 6;
    }
    gameSession = playing && !noSave;
    savedCheckpoint = world->ProgressCheckpoint();
    lastTick = lastSave = std::chrono::steady_clock::now();
    framePacer.Reset();
    FrameProfiler::Get().StartLogging();
    std::cout << "[PERF] Logging every 1s: FPS and actual GL draw calls (scene + post + UI).\n";
    while (!platform.ShouldClose())
    {
        platform.Poll();
        if (platform.ShouldClose())
        {
            break;
        }
        UpdateGame();
        if (platform.Minimized())
        {
            platform.Wait(1.0 / 60.0);
            continue;
        }
        Display();
    }
    if (initialized)
    {
        Close();
        platform.Close();
    }
    world.reset();
    return 0;
}
