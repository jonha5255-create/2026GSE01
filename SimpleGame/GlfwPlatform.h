#pragma once
#define GLFW_INCLUDE_NONE
#include "Dependencies/GLFW/glfw-3.4/include/GLFW/glfw3.h"
#include <array>
#include <functional>

// Owns the window/context only; simulation and renderer stay framework-independent.
class GlfwPlatform
{
  public:
    GlfwPlatform() = default;
    ~GlfwPlatform();
    bool Open(const char* title, int width, int height);
    void Close();
    void RequestClose();
    bool ShouldClose() const;
    bool Minimized() const;
    void Swap();
    void Poll();
    void Wait(double seconds);
    void Resize(int width, int height);
    void SyncFramebuffer();
    void SetSwapInterval(int interval);
    void ReleaseKeys();
    static int TranslateKey(int key);
    static bool SelfTest();
    static void ToFramebuffer(double x,
                              double y,
                              int windowWidth,
                              int windowHeight,
                              int framebufferWidth,
                              int framebufferHeight,
                              int& outX,
                              int& outY);

    GLFWwindow* Window() const
    {
        return window_;
    }

    std::function<void(unsigned char, bool)> key;
    std::function<void(int, bool, int, int)> mouse;
    std::function<void(int, int)> framebuffer;
    void DispatchKey(int code, int action);

  private:
    GlfwPlatform(const GlfwPlatform&) = delete;
    GlfwPlatform& operator=(const GlfwPlatform&) = delete;
    void ReconcileMovementKeys(bool (*isHeld)(int));

  private:
    GLFWwindow* window_ = nullptr;
    bool started_ = false;
    std::array<bool, 256> pressed_{};
};
