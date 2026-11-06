#include "stdafx.h"
#include <windows.h>
#include "GlfwPlatform.h"
#include <algorithm>
#include <cmath>
#include <iostream>

namespace
{
    constexpr std::array<int, 4> MovementKeys{GLFW_KEY_W, GLFW_KEY_A, GLFW_KEY_S, GLFW_KEY_D};
}

GlfwPlatform::~GlfwPlatform()
{
    Close();
}

bool GlfwPlatform::Open(const char* title, int width, int height)
{
    glfwSetErrorCallback(
        [](int error, const char* description)
        {
            std::cerr << "[GLFW] " << error << ": " << description << '\n';
        });
    if (!glfwInit())
    {
        return false;
    }
    started_ = true;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_DOUBLEBUFFER, GLFW_TRUE);
    glfwWindowHint(GLFW_POSITION_X, 80);
    glfwWindowHint(GLFW_POSITION_Y, 60);
    window_ = glfwCreateWindow(width, height, title, nullptr, nullptr);
    if (!window_)
    {
        Close();
        return false;
    }
    glfwMakeContextCurrent(window_);
    glfwSetWindowUserPointer(window_, this);
    glfwSetKeyCallback(window_,
                       [](GLFWwindow* window, int code, int, int action, int)
                       {
                           auto* self =
                               static_cast<GlfwPlatform*>(glfwGetWindowUserPointer(window));
                           if (self)
                           {
                               self->DispatchKey(code, action);
                           }
                       });
    glfwSetWindowFocusCallback(window_,
                               [](GLFWwindow* window, int focused)
                               {
                                   auto* self =
                                       static_cast<GlfwPlatform*>(glfwGetWindowUserPointer(window));
                                   if (self && !focused)
                                   {
                                       self->ReleaseKeys();
                                   }
                               });
    glfwSetFramebufferSizeCallback(window_,
                                   [](GLFWwindow* window, int width, int height)
                                   {
                                       auto* self = static_cast<GlfwPlatform*>(
                                           glfwGetWindowUserPointer(window));
                                       if (self && self->framebuffer)
                                       {
                                           self->framebuffer(width, height);
                                       }
                                   });
    glfwSetMouseButtonCallback(
        window_,
        [](GLFWwindow* window, int button, int action, int)
        {
            auto* self = static_cast<GlfwPlatform*>(glfwGetWindowUserPointer(window));
            if (!self || !self->mouse
                || (button != GLFW_MOUSE_BUTTON_LEFT && button != GLFW_MOUSE_BUTTON_RIGHT))
            {
                return;
            }
            double x, y;
            int width, height, fbWidth, fbHeight, px, py;
            glfwGetCursorPos(window, &x, &y);
            glfwGetWindowSize(window, &width, &height);
            glfwGetFramebufferSize(window, &fbWidth, &fbHeight);
            ToFramebuffer(x, y, width, height, fbWidth, fbHeight, px, py);
            self->mouse(button, action == GLFW_PRESS, px, py);
        });
    SyncFramebuffer();
    return true;
}

void GlfwPlatform::Close()
{
    if (window_)
    {
        glfwSetWindowUserPointer(window_, nullptr);
        glfwDestroyWindow(window_);
        window_ = nullptr;
    }
    if (started_)
    {
        glfwTerminate();
        started_ = false;
    }
}

void GlfwPlatform::RequestClose()
{
    if (window_)
    {
        glfwSetWindowShouldClose(window_, GLFW_TRUE);
    }
}

bool GlfwPlatform::ShouldClose() const
{
    return !window_ || glfwWindowShouldClose(window_);
}

bool GlfwPlatform::Minimized() const
{
    return window_ && glfwGetWindowAttrib(window_, GLFW_ICONIFIED);
}

void GlfwPlatform::Swap()
{
    glfwSwapBuffers(window_);
}

void GlfwPlatform::Poll()
{
    glfwPollEvents();
    if (window_ && glfwGetWindowAttrib(window_, GLFW_FOCUSED))
    {
        ReconcileMovementKeys(
            [](int code)
            {
                // GLFW key names identify physical keys. Translate through the active layout.
                int scan = glfwGetKeyScancode(code);
                UINT vk = scan >= 0 ? MapVirtualKeyW(UINT(scan), MAPVK_VSC_TO_VK_EX) : 0;
                return !vk || (GetAsyncKeyState(int(vk)) & 0x8000) != 0;
            });
    }
}

void GlfwPlatform::Wait(double seconds)
{
    glfwWaitEventsTimeout(seconds);
}

void GlfwPlatform::SetSwapInterval(int interval)
{
    glfwSwapInterval(interval);
}

void GlfwPlatform::SyncFramebuffer()
{
    int width, height;
    glfwGetFramebufferSize(window_, &width, &height);
    if (framebuffer)
    {
        framebuffer(width, height);
    }
}

void GlfwPlatform::Resize(int width, int height)
{
    glfwSetWindowSize(window_, width, height);
    glfwPollEvents();
    SyncFramebuffer();
}

int GlfwPlatform::TranslateKey(int code)
{
    if (code >= GLFW_KEY_A && code <= GLFW_KEY_Z)
    {
        return 'a' + code - GLFW_KEY_A;
    }
    if (code >= GLFW_KEY_0 && code <= GLFW_KEY_9)
    {
        return '0' + code - GLFW_KEY_0;
    }
    switch (code)
    {
    case GLFW_KEY_ESCAPE:
        return 27;
    case GLFW_KEY_TAB:
        return '\t';
    case GLFW_KEY_SPACE:
        return ' ';
    case GLFW_KEY_ENTER:
        return '\r';
    case GLFW_KEY_LEFT_BRACKET:
        return '[';
    case GLFW_KEY_RIGHT_BRACKET:
        return ']';
    default:
        return -1;
    }
}

void GlfwPlatform::DispatchKey(int code, int action)
{
    int translated = TranslateKey(code);
    if (translated < 0)
    {
        return;
    }
    if (action == GLFW_REPEAT
        && std::find(MovementKeys.begin(), MovementKeys.end(), code) == MovementKeys.end())
    {
        return;
    }
    // A recovered key can still be held in GLFW's cache, so its next press can be a repeat.
    bool down = action != GLFW_RELEASE;
    if (pressed_[translated] == down)
    {
        return;
    }
    pressed_[translated] = down;
    if (key)
    {
        key(static_cast<unsigned char>(translated), down);
    }
}

void GlfwPlatform::ReconcileMovementKeys(bool (*isHeld)(int))
{
    for (int code : MovementKeys)
    {
        if (pressed_[TranslateKey(code)] && !isHeld(code))
        {
            DispatchKey(code, GLFW_RELEASE);
        }
    }
}

void GlfwPlatform::ReleaseKeys()
{
    for (size_t i = 0; i < pressed_.size(); ++i)
    {
        if (pressed_[i])
        {
            pressed_[i] = false;
            if (key)
            {
                key(static_cast<unsigned char>(i), false);
            }
        }
    }
}

void GlfwPlatform::ToFramebuffer(
    double x, double y, int width, int height, int fbWidth, int fbHeight, int& px, int& py)
{
    px = int(std::floor(x * fbWidth / std::max(1, width)));
    py = int(std::floor(y * fbHeight / std::max(1, height)));
}

bool GlfwPlatform::SelfTest()
{
    bool ok = true;
    auto check = [&](bool pass, const char* label)
    {
        std::cout << (pass ? "PASS " : "FAIL ") << label << '\n';
        ok &= pass;
    };
    GlfwPlatform platform;
    int downs = 0, ups = 0;
    platform.key = [&](unsigned char key, bool down)
    {
        if (key == 'w')
        {
            down ? ++downs : ++ups;
        }
    };
    platform.DispatchKey(GLFW_KEY_W, GLFW_PRESS);
    platform.DispatchKey(GLFW_KEY_W, GLFW_REPEAT);
    platform.DispatchKey(GLFW_KEY_W, GLFW_PRESS);
    check(downs == 1, "GLFW repeats do not retrigger gameplay actions");
    platform.ReleaseKeys();
    platform.DispatchKey(GLFW_KEY_W, GLFW_RELEASE);
    check(ups == 1, "focus loss releases held movement keys exactly once");
    std::array<bool, 256> gameKeys{};
    int releases = 0;
    platform.key = [&](unsigned char key, bool down)
    {
        gameKeys[key] = down;
        if (!down)
        {
            ++releases;
        }
    };
    platform.DispatchKey(GLFW_KEY_W, GLFW_PRESS);
    platform.DispatchKey(GLFW_KEY_D, GLFW_PRESS);
    platform.ReconcileMovementKeys([](int code) { return code == GLFW_KEY_D; });
    check(!gameKeys['w'] && gameKeys['d'], "releasing W keeps D held when moving diagonally");
    platform.ReconcileMovementKeys([](int) { return false; });
    platform.DispatchKey(GLFW_KEY_D, GLFW_RELEASE);
    check(!gameKeys['w'] && !gameKeys['d'] && releases == 2,
          "missed diagonal key releases clear movement without duplicate callbacks");
    platform.DispatchKey(GLFW_KEY_D, GLFW_REPEAT);
    platform.DispatchKey(GLFW_KEY_Q, GLFW_REPEAT);
    check(gameKeys['d'] && !gameKeys['q'], "movement can resume after a missed release");
    check(TranslateKey(GLFW_KEY_TAB) == '\t' && TranslateKey(GLFW_KEY_LEFT_BRACKET) == '['
              && TranslateKey(GLFW_KEY_3) == '3' && TranslateKey(GLFW_KEY_ESCAPE) == 27
              && TranslateKey(GLFW_KEY_UNKNOWN) == -1,
          "GLFW key mapping preserves existing controls");
    int x, y;
    ToFramebuffer(480.5, 300.5, 961, 601, 1922, 1202, x, y);
    check(x == 961 && y == 601, "high-DPI mouse coordinates match framebuffer picking pixels");
    return ok;
}
