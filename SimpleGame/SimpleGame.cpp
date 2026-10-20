/*
Copyright 2022 Lee Taek Hee (Tech University of Korea)
This program is free software: you can redistribute it and/or modify
it under the terms of the What The Hell License. Do it plz.
This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY.
Prototype scene extensions, 2026.
*/
#include "stdafx.h"
#include "PrototypeRenderer.h"
#include "PrototypeWorld.h"
#include "FrameProfiler.h"
#include "RenderDiagnostics.h"
#include "WorldMonitor.h"
#include "AssetPreview.h"
#include "Dependencies/freeglut.h"
#include <windows.h>
#include <algorithm>
#include <chrono>
#include <iostream>
#include <memory>
#include <string>

namespace
{
    PrototypeRenderer renderer;
    std::unique_ptr<PrototypeWorld> world;
    std::unique_ptr<Art::AssetPreview> assetGallery;
    float assetTime = 0;
    bool assetPaused = false;
    std::string assetError;
    int width = 1280, height = 800;
    bool initialized = false;
    bool profileTest = false;
    int profileFrames = 0;
    bool effectKeys[256] = {};
    auto lastTick = std::chrono::steady_clock::now();

    void Close()
    {
        WorldMonitor::Get().Stop();
        if (initialized)
        {
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
            glutLeaveMainLoop();
            return;
        }
        DrawContents();
        renderer.Flush();
        FrameProfiler::Get().EndRender();
        glutSwapBuffers();
        FrameProfiler::Get().EndFrame();
        if (profileTest && ++profileFrames >= 180)
        {
            glutLeaveMainLoop();
        }
    }

    void Resize(int w, int h)
    {
        RenderDiagnostics::Get().Event("resize", std::to_string(w) + "x" + std::to_string(h));
        width = std::max(w, 1);
        height = std::max(h, 1);
        glutPostRedisplay();
    }

    void KeyDown(unsigned char key, int, int)
    {
        if (key == 27)
        {
            glutLeaveMainLoop();
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
        if (initialized && world && button == GLUT_LEFT_BUTTON && state == GLUT_DOWN)
        {
            RenderDiagnostics::Get().Event("mouse_attack",
                                           std::to_string(x) + "," + std::to_string(y));
            world->MouseAttack(x, y, width, height);
            glutPostRedisplay();
        }
        else if (initialized && world && button == GLUT_RIGHT_BUTTON && state == GLUT_DOWN)
        {
            RenderDiagnostics::Get().Event("mouse_skill", "right_button_down");
            world->MouseSkill();
            glutPostRedisplay();
        }
    }

    void Timer(int)
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
        }
        FrameProfiler::Get().RecordUpdate(std::chrono::duration<double, std::milli>(
                                              std::chrono::steady_clock::now() - updateStart)
                                              .count());
        glutPostRedisplay();
        glutTimerFunc(16, Timer, 0);
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
    bool assetTest = false, assetSmoke = false, assetPreview = false;
    std::filesystem::path assetRoot;
    unsigned short monitorPort = 8765;
    for (int i = 1; i < argc; ++i)
    {
        smoke |= std::string(argv[i]) == "--smoke-test";
        selfTest |= std::string(argv[i]) == "--self-test";
        profileTest |= std::string(argv[i]) == "--profile-test";
        rebuildCache |= std::string(argv[i]) == "--rebuild-mesh-cache";
        legacy |= std::string(argv[i]) == "--level-one";
        monitorEnabled &= std::string(argv[i]) != "--no-monitor";
        monitorTest |= std::string(argv[i]) == "--monitor-test";
        assetTest |= std::string(argv[i]) == "--asset-test";
        assetSmoke |= std::string(argv[i]) == "--asset-smoke-test";
        assetPreview |= std::string(argv[i]) == "--asset-preview";
        std::string argument = argv[i];
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
    if ((selfTest || smoke) && (!PrototypeWorld::SelfTest() || !WorldMonitor::SelfTest()))
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
    glutInit(&argc, argv);
    glutInitContextVersion(3, 3);
    glutInitContextProfile(GLUT_CORE_PROFILE);
    glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGBA);
    glutInitWindowPosition(80, 60);
    glutInitWindowSize(width, height);
    int window = glutCreateWindow("2026 GSE | Nameless District - Rendering Prototype");
    if (!window)
    {
        return 3;
    }
    glutSetOption(GLUT_ACTION_ON_WINDOW_CLOSE, GLUT_ACTION_GLUTMAINLOOP_RETURNS);
    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK || !GLEW_VERSION_3_3)
    {
        std::cerr << "OpenGL 3.3 required\n";
        glutDestroyWindow(window);
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
        glutDestroyWindow(window);
        return 5;
    }
    initialized = true;
    uint32_t seed =
        smoke ? 2026
              : static_cast<uint32_t>(std::chrono::system_clock::now().time_since_epoch().count());
    for (int i = 1; i < argc; ++i)
    {
        std::string argument = argv[i];
        if (argument.find("--seed=") == 0)
        {
            try
            {
                seed = static_cast<uint32_t>(std::stoul(argument.substr(7)));
            }
            catch (...)
            {
                std::cerr << "Invalid seed\n";
                Close();
                glutDestroyWindow(window);
                return 7;
            }
        }
    }
    world.reset(new PrototypeWorld(seed, !legacy));
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
            glutDestroyWindow(window);
            return 10;
        }
    }
    if (monitorEnabled && !smoke && !profileTest && !assetGallery)
    {
        WorldMonitor::Get().Start(monitorPort);
    }
    RenderDiagnostics::Get().Event(
        "environment",
        std::string("seed=") + std::to_string(seed) + "; resolution=" + std::to_string(width) + "x"
            + std::to_string(height)
            + "; OpenGL=" + reinterpret_cast<const char*>(glGetString(GL_VERSION))
            + "; GPU=" + reinterpret_cast<const char*>(glGetString(GL_RENDERER))
            + "; timer_interval_ms=16; swap_interval=driver_default");
    std::cout << "OpenGL " << glGetString(GL_VERSION) << " / " << glGetString(GL_RENDERER) << '\n';
    glutDisplayFunc(Display);
    glutReshapeFunc(Resize);
    glutKeyboardFunc(KeyDown);
    glutKeyboardUpFunc(KeyUp);
    glutMouseFunc(MouseButton);
    glutCloseFunc(Close);
    glutIgnoreKeyRepeat(1);
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
        glutDestroyWindow(window);
        return ok ? 0 : 11;
    }
    if (smoke)
    {
        for (int i = 0; i < 60; ++i)
        {
            world->Update(1.f / 60);
        }
        bool ok = renderer.VerifyMeshCache();
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
        glutReshapeWindow(960, 600);
        glutMainLoopEvent();
        Resize(960, 600);
        ok = Snapshot(L"prototype-resized.bmp") && ok;
        // Exercise odd half-resolution sizes, then restore the original targets.
        glutReshapeWindow(961, 601);
        glutMainLoopEvent();
        Resize(961, 601);
        ok = Snapshot(L"prototype-resized-odd.bmp") && ok;
        glutReshapeWindow(1280, 800);
        glutMainLoopEvent();
        Resize(1280, 800);
        ok = Snapshot(L"prototype-restored.bmp") && ok;
        const auto& cache = renderer.MeshStats();
        std::cout << "Mesh cache: " << cache.hits << " hits, " << cache.uploads << " admissions, "
                  << cache.evictions << " evictions, " << cache.bytes << " resident bytes\n";
        Close();
        glutDestroyWindow(window);
        return ok ? 0 : 6;
    }
    lastTick = std::chrono::steady_clock::now();
    FrameProfiler::Get().StartLogging();
    std::cout << "[PERF] Logging every 1s: FPS and actual GL draw calls (scene + post + UI).\n";
    glutTimerFunc(16, Timer, 0);
    glutMainLoop();
    if (initialized)
    {
        Close();
        glutDestroyWindow(window);
    }
    world.reset();
    return 0;
}
