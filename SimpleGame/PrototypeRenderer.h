#pragma once
#include "Dependencies/glew.h"
#include "PostProcessor.h"
#include <string>
#include <vector>

struct Point { float x, y; };
struct Ink { float r, g, b, a; float energy=1; };
inline Ink Color(int r, int g, int b, float a = 1.f) {
    return {r / 255.f, g / 255.f, b / 255.f, a};
}
inline Ink Emissive(Ink color,float energy=4.f) { color.energy=energy;return color; }

// Batched screen-space triangles. World projection belongs to PrototypeWorld.
class PrototypeRenderer {
public:
    bool Initialize();
    void Shutdown();
    bool Begin(int width, int height);
    void CompositeScene();
    PostSettings& Effects() { return post_.settings; }
    bool VerifyHDR() const { return post_.VerifyHDR(); }
    void Flush();
    void Triangle(Point a, Point b, Point c, Ink color);
    void Quad(Point a, Point b, Point c, Point d, Ink color);
    void Rect(float x, float y, float w, float h, Ink color);
    void Line(Point a, Point b, float width, Ink color);
    void Ellipse(Point p, float rx, float ry, Ink color);
    void Glow(Point p, float radius, Ink color);
    void Text(float x, float y, const std::string& text, Ink color, float scale = 1.f);
    void Text(float x, float y, const std::wstring& text, Ink color, float scale = 1.f);
    bool Capture(const std::wstring& path) const;
    int Width() const { return width_; }
    int Height() const { return height_; }
    void SetScale(float scale) { scale_ = scale; }
private:
    struct Vertex { float x, y, r, g, b, a, u, v, energy; };
    PostProcessor post_;
    bool linearScene_=true;
    static constexpr int AtlasWidth = 3072, AtlasHeight = 2304, GlyphCell = 24;
    static constexpr int HangulStart = 96, GlyphCount = HangulStart + 11172;
    void Push(Point p, Ink c, float u = .5f / AtlasWidth, float v = .5f / AtlasHeight);
    std::vector<float> advances_;
    GLuint program_ = 0, vao_ = 0, vbo_ = 0, atlas_ = 0;
    GLint viewport_ = -1;
    GLint linearUniform_ = -1;
    int width_ = 1280, height_ = 800;
    float scale_ = 1;
    std::vector<Vertex> vertices_;
};
