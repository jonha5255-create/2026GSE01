#pragma once
#include "Dependencies/glew.h"
#include "PostProcessor.h"
#include <string>
#include <vector>
#include <map>
#include <functional>
#include <cstdint>

struct Point
{
    float x, y;
};

struct Ink
{
    float r, g, b, a;
    float energy = 1;
};

inline Ink Color(int r, int g, int b, float a = 1.f)
{
    return {r / 255.f, g / 255.f, b / 255.f, a};
}

inline Ink Emissive(Ink color, float energy = 4.f)
{
    color.energy = energy;
    return color;
}

// Batched screen-space triangles. World projection belongs to PrototypeWorld.
class PrototypeRenderer
{
  public:
    bool Initialize();
    void Shutdown();
    bool Begin(int width, int height);
    void CompositeScene();

    PostSettings& Effects()
    {
        return post_.settings;
    }

    bool VerifyHDR() const
    {
        return post_.VerifyHDR();
    }

    void Flush();
    // Immutable local-space geometry; key must include every geometry/material input.
    // Dynamic transforms do not invalidate or upload the cached VBO.
    void CachedMesh(const std::string& key,
                    Point offset,
                    float scale,
                    float opacity,
                    const std::function<void()>& build);
    bool VerifyMeshCache();

    struct CacheStats
    {
        size_t hits = 0, uploads = 0, evictions = 0, bytes = 0;
    };

    const CacheStats& MeshStats() const
    {
        return cacheStats_;
    }

    void Triangle(Point a, Point b, Point c, Ink color);
    void Quad(Point a, Point b, Point c, Point d, Ink color);
    void Rect(float x, float y, float w, float h, Ink color);
    void Line(Point a, Point b, float width, Ink color);
    void Ellipse(Point p, float rx, float ry, Ink color);
    void Glow(Point p, float radius, Ink color);
    void Text(float x, float y, const std::string& text, Ink color, float scale = 1.f);
    void Text(float x, float y, const std::wstring& text, Ink color, float scale = 1.f);
    bool Capture(const std::wstring& path) const;

    int Width() const
    {
        return width_;
    }

    int Height() const
    {
        return height_;
    }

    void SetScale(float scale)
    {
        scale_ = scale;
    }

  private:
    struct Vertex
    {
        float x, y, r, g, b, a, u, v, energy;
    };

    struct Mesh
    {
        GLuint vao = 0, vbo = 0;
        GLsizei count = 0;
        uint64_t lastFrame = 0;
    };

    struct DrawCommand
    {
        const Mesh* mesh = nullptr;
        GLint first = 0;
        GLsizei count = 0;
        Point offset{0, 0};
        float scale = 1, opacity = 1;
    };

    static void ConfigureVertices();
    void QueueDynamic();
    void TrimCache();
    void ClearCache();
    static constexpr size_t MaxCachedMeshes = 256, MaxCachedBytes = 32 * 1024 * 1024;
    std::map<std::string, Mesh> meshes_;
    std::vector<DrawCommand> commands_;
    CacheStats cacheStats_;
    uint64_t frame_ = 0;
    size_t dynamicStart_ = 0;

    PostProcessor post_;
    bool linearScene_ = true;
    static constexpr int AtlasWidth = 3072, AtlasHeight = 2304, GlyphCell = 24;
    static constexpr int HangulStart = 96, GlyphCount = HangulStart + 11172;
    void Push(Point p, Ink c, float u = .5f / AtlasWidth, float v = .5f / AtlasHeight);
    std::vector<float> advances_;
    GLuint program_ = 0, vao_ = 0, vbo_ = 0, atlas_ = 0;
    GLint viewport_ = -1;
    GLint linearUniform_ = -1;
    GLint offsetUniform_ = -1, scaleUniform_ = -1, opacityUniform_ = -1;
    int width_ = 1280, height_ = 800;
    float scale_ = 1;
    std::vector<Vertex> vertices_;
};
