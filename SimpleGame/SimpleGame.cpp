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
    int width = 1280, height = 800;
    bool initialized = false;
    bool effectKeys[256] = {};
    auto lastTick = std::chrono::steady_clock::now();

    void Close()
    {
        if (initialized)
        {
            renderer.Shutdown();
            initialized = false;
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
        world->Draw(renderer);
        renderer.CompositeScene();
        world->DrawUI(renderer);
        renderer.Flush();
        glutSwapBuffers();
    }

    void Resize(int w, int h)
    {
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
        if (!effectKeys[key])
        {
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
        if (key >= 'A' && key <= 'Z')
        {
            key += 32;
        }
        effectKeys[key] = false;
        world->Key(key, false);
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
        world->Update(dt);
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
        world->Draw(renderer);
        renderer.CompositeScene();
        world->DrawUI(renderer);
        renderer.Flush();
        glFinish();
        bool ok = renderer.Capture(ExecutableDirectory() + name);
        GLenum error = glGetError();
        std::cout << "Snapshot: " << ok << " GL error: " << error << '\n';
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
        renderer.Effects().bloom = true;
        auto bloom = draw();
        check(changed(baseline, bloom), "bloom changes rendered highlights");
        renderer.Effects() = neutral;
        renderer.Effects().vignette = true;
        auto vignette = draw();
        check(changed(baseline, vignette), "vignette changes scene edges");
        renderer.Effects() = neutral;
        renderer.Effects().edgeBlur = true;
        auto blur = draw();
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
        check(!bypassA.empty() && bypassA == bypassB, "master bypass ignores all effect settings");
        renderer.Effects() = saved;
        return ok && glGetError() == GL_NO_ERROR;
    }
} // namespace

int main(int argc, char** argv)
{
    bool smoke = false, selfTest = false;
    for (int i = 1; i < argc; ++i)
    {
        smoke |= std::string(argv[i]) == "--smoke-test";
        selfTest |= std::string(argv[i]) == "--self-test";
    }
    if ((selfTest || smoke) && !PrototypeWorld::SelfTest())
    {
        return 2;
    }
    if (selfTest && !smoke)
    {
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
    world.reset(new PrototypeWorld(seed));
    std::cout << "OpenGL " << glGetString(GL_VERSION) << " / " << glGetString(GL_RENDERER) << '\n';
    glutDisplayFunc(Display);
    glutReshapeFunc(Resize);
    glutKeyboardFunc(KeyDown);
    glutKeyboardUpFunc(KeyUp);
    glutCloseFunc(Close);
    glutIgnoreKeyRepeat(1);
    if (smoke)
    {
        for (int i = 0; i < 60; ++i)
        {
            world->Update(1.f / 60);
        }
        bool ok = renderer.VerifyMeshCache();
        ok = VerifyPostEffects() && ok;
        ok = Snapshot(L"prototype-companion.bmp") && ok;
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
        std::cout << "Mesh cache: " << cache.hits << " hits, " << cache.uploads << " uploads, "
                  << cache.evictions << " evictions, " << cache.bytes << " resident bytes\n";
        Close();
        glutDestroyWindow(window);
        return ok ? 0 : 6;
    }
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
