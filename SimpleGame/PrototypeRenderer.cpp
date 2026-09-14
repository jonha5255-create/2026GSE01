#include "stdafx.h"
#include "PrototypeRenderer.h"
#include "ShaderProgram.h"
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <array>

namespace
{
    template <size_t Segments> const std::array<Point, Segments + 1>& UnitCircle()
    {
        static const auto points = []()
        {
            std::array<Point, Segments + 1> result{};
            for (size_t i = 0; i <= Segments; ++i)
            {
                float angle = float(i) * 6.2831853f / Segments;
                result[i] = {std::cos(angle), std::sin(angle)};
            }
            return result;
        }();
        return points;
    }
} // namespace

bool PrototypeRenderer::Initialize()
{
    program_ = LoadShaderProgram(L"Scene.vs", L"Scene.fs");
    if (!program_)
    {
        return false;
    }
    viewport_ = glGetUniformLocation(program_, "viewport");
    linearUniform_ = glGetUniformLocation(program_, "linearScene");
    offsetUniform_ = glGetUniformLocation(program_, "meshOffset");
    scaleUniform_ = glGetUniformLocation(program_, "meshScale");
    opacityUniform_ = glGetUniformLocation(program_, "meshOpacity");
    glGenVertexArrays(1, &vao_);
    glBindVertexArray(vao_);
    glGenBuffers(1, &vbo_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    ConfigureVertices();

    // Preload ASCII and all 11,172 modern Hangul syllables. Text remains local;
    // UTF-8 dialogue may change without editing a hand-picked glyph list.
    HDC dc = CreateCompatibleDC(nullptr);
    BITMAPINFO info = {};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = AtlasWidth;
    info.bmiHeader.biHeight = -AtlasHeight;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    void* bits = nullptr;
    HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    HFONT font = CreateFontW(-20,
                             0,
                             0,
                             0,
                             FW_NORMAL,
                             FALSE,
                             FALSE,
                             FALSE,
                             DEFAULT_CHARSET,
                             OUT_DEFAULT_PRECIS,
                             CLIP_DEFAULT_PRECIS,
                             ANTIALIASED_QUALITY,
                             DEFAULT_PITCH,
                             L"Malgun Gothic");
    if (!dc || !bitmap || !font)
    {
        if (bitmap)
        {
            DeleteObject(bitmap);
        }
        if (font)
        {
            DeleteObject(font);
        }
        if (dc)
        {
            DeleteDC(dc);
        }
        Shutdown();
        return false;
    }
    HGDIOBJ oldBitmap = SelectObject(dc, bitmap), oldFont = SelectObject(dc, font);
    PatBlt(dc, 0, 0, AtlasWidth, AtlasHeight, BLACKNESS);
    SetTextColor(dc, RGB(255, 255, 255));
    SetBkMode(dc, TRANSPARENT);
    advances_.assign(GlyphCount, 20.f);
    for (int i = 1; i < GlyphCount; ++i)
    {
        wchar_t c = static_cast<wchar_t>(i < HangulStart ? i + 31 : 0xAC00 + i - HangulStart);
        int x = (i % (AtlasWidth / GlyphCell)) * GlyphCell;
        int y = (i / (AtlasWidth / GlyphCell)) * GlyphCell;
        TextOutW(dc, x, y, &c, 1);
        SIZE extent = {};
        GetTextExtentPoint32W(dc, &c, 1, &extent);
        advances_[i] = static_cast<float>(extent.cx);
    }
    GdiFlush();
    std::vector<unsigned char> pixels(AtlasWidth * AtlasHeight);
    auto src = static_cast<unsigned char*>(bits);
    for (size_t i = 0; i < pixels.size(); ++i)
    {
        pixels[i] = src[i * 4];
    }
    pixels[0] = 255;
    SelectObject(dc, oldBitmap);
    SelectObject(dc, oldFont);
    DeleteObject(bitmap);
    DeleteObject(font);
    DeleteDC(dc);
    glGenTextures(1, &atlas_);
    glBindTexture(GL_TEXTURE_2D, atlas_);
    glTexImage2D(GL_TEXTURE_2D,
                 0,
                 GL_R8,
                 AtlasWidth,
                 AtlasHeight,
                 0,
                 GL_RED,
                 GL_UNSIGNED_BYTE,
                 pixels.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);
    vertices_.reserve(250000);
    return post_.Initialize() && glGetError() == GL_NO_ERROR;
}

void PrototypeRenderer::Shutdown()
{
    commands_.clear();
    ClearCache();
    post_.Shutdown();
    if (atlas_)
    {
        glDeleteTextures(1, &atlas_);
    }
    if (vbo_)
    {
        glDeleteBuffers(1, &vbo_);
    }
    if (vao_)
    {
        glDeleteVertexArrays(1, &vao_);
    }
    if (program_)
    {
        glDeleteProgram(program_);
    }
    atlas_ = vbo_ = vao_ = program_ = 0;
}

bool PrototypeRenderer::Begin(int width, int height)
{
    width_ = std::max(width, 1);
    height_ = std::max(height, 1);
    vertices_.clear();
    commands_.clear();
    dynamicStart_ = 0;
    ++frame_;
    TrimCache();
    scale_ = 1;
    glViewport(0, 0, width_, height_);
    linearScene_ = true;
    if (!post_.Begin(width_, height_))
    {
        return false;
    }
    glClearColor(.002f, .0033f, .0047f, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    return true;
}

void PrototypeRenderer::CompositeScene()
{
    Flush();
    post_.Composite();
    linearScene_ = false;
}

void PrototypeRenderer::Push(Point p, Ink c, float u, float v)
{
    vertices_.push_back({p.x * scale_, p.y * scale_, c.r, c.g, c.b, c.a, u, v, c.energy});
}

void PrototypeRenderer::Triangle(Point a, Point b, Point c, Ink color)
{
    Push(a, color);
    Push(b, color);
    Push(c, color);
}

void PrototypeRenderer::Quad(Point a, Point b, Point c, Point d, Ink color)
{
    Triangle(a, b, c, color);
    Triangle(a, c, d, color);
}

void PrototypeRenderer::Rect(float x, float y, float w, float h, Ink c)
{
    Quad({x, y}, {x + w, y}, {x + w, y + h}, {x, y + h}, c);
}

void PrototypeRenderer::Line(Point a, Point b, float width, Ink c)
{
    float dx = b.x - a.x, dy = b.y - a.y, len = std::sqrt(dx * dx + dy * dy);
    if (len < .001f)
    {
        return;
    }
    float x = -dy / len * width * .5f, y = dx / len * width * .5f;
    Quad({a.x + x, a.y + y}, {b.x + x, b.y + y}, {b.x - x, b.y - y}, {a.x - x, a.y - y}, c);
}

void PrototypeRenderer::Ellipse(Point p, float rx, float ry, Ink c)
{
    const auto& circle = UnitCircle<32>();
    for (int i = 0; i < 32; ++i)
    {
        Triangle(p,
                 {p.x + circle[i].x * rx, p.y + circle[i].y * ry},
                 {p.x + circle[i + 1].x * rx, p.y + circle[i + 1].y * ry},
                 c);
    }
}

void PrototypeRenderer::Glow(Point p, float radius, Ink c)
{
    // Subtle local haze; actual light spreading comes from HDR bloom.
    // Interpolated alpha avoids visible concentric discs after tone mapping.
    c.a *= .12f;
    c.energy = 1;
    Ink edge = c;
    edge.a = 0;
    const auto& circle = UnitCircle<48>();
    for (int i = 0; i < 48; ++i)
    {
        Push(p, c);
        Push({p.x + circle[i].x * radius, p.y + circle[i].y * radius}, edge);
        Push({p.x + circle[i + 1].x * radius, p.y + circle[i + 1].y * radius}, edge);
    }
}

void PrototypeRenderer::Text(float x, float y, const std::string& text, Ink c, float scale)
{
    if (text.empty())
    {
        return;
    }
    int count =
        MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (count <= 0)
    {
        return;
    }
    std::wstring wide(count, L' ');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), &wide[0], count);
    Text(x, y, wide, c, scale);
}

void PrototypeRenderer::Text(float x, float y, const std::wstring& text, Ink c, float scale)
{
    float start = x;
    for (wchar_t ch : text)
    {
        if (ch == '\n')
        {
            y += 24 * scale;
            x = start;
            continue;
        }
        int i = ch >= 32 && ch <= 126          ? ch - 31
                : ch >= 0xAC00 && ch <= 0xD7A3 ? HangulStart + ch - 0xAC00
                                               : '?' - 31;
        float u = float((i % (AtlasWidth / GlyphCell)) * GlyphCell) / AtlasWidth;
        float v = float((i / (AtlasWidth / GlyphCell)) * GlyphCell) / AtlasHeight;
        float u2 = u + float(GlyphCell) / AtlasWidth, v2 = v + float(GlyphCell) / AtlasHeight;
        Point a{x, y}, b{x + GlyphCell * scale, y}, d{x, y + GlyphCell * scale}, e{b.x, d.y};
        Push(a, c, u, v);
        Push(b, c, u2, v);
        Push(e, c, u2, v2);
        Push(a, c, u, v);
        Push(e, c, u2, v2);
        Push(d, c, u, v2);
        x += advances_[i] * scale;
    }
}

void PrototypeRenderer::Flush()
{
    QueueDynamic();
    glUseProgram(program_);
    glUniform2f(viewport_, float(width_), float(height_));
    glUniform1i(linearUniform_, linearScene_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, atlas_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(
        GL_ARRAY_BUFFER, vertices_.size() * sizeof(Vertex), vertices_.data(), GL_STREAM_DRAW);
    for (const auto& command : commands_)
    {
        glBindVertexArray(command.mesh ? command.mesh->vao : vao_);
        glUniform2f(offsetUniform_, command.offset.x, command.offset.y);
        glUniform1f(scaleUniform_, command.scale);
        glUniform1f(opacityUniform_, command.opacity);
        glDrawArrays(GL_TRIANGLES, command.first, command.count);
    }
    vertices_.clear();
    commands_.clear();
    dynamicStart_ = 0;
}

void PrototypeRenderer::ConfigureVertices()
{
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glEnableVertexAttribArray(2);
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), nullptr);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)(2 * sizeof(float)));
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)(6 * sizeof(float)));
    glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)(8 * sizeof(float)));
}

void PrototypeRenderer::QueueDynamic()
{
    if (vertices_.size() > dynamicStart_)
    {
        commands_.push_back(
            {nullptr, GLint(dynamicStart_), GLsizei(vertices_.size() - dynamicStart_)});
        dynamicStart_ = vertices_.size();
    }
}

void PrototypeRenderer::CachedMesh(const std::string& key,
                                   Point offset,
                                   float scale,
                                   float opacity,
                                   const std::function<void()>& build)
{
    QueueDynamic();
    auto it = meshes_.find(key);
    if (it == meshes_.end())
    {
        // The builder only emits local geometry, never flushes or nests cache calls.
        std::vector<Vertex> pending;
        pending.swap(vertices_);
        float previousScale = scale_;
        scale_ = 1;
        build();
        scale_ = previousScale;
        Mesh mesh;
        mesh.count = GLsizei(vertices_.size());
        glGenVertexArrays(1, &mesh.vao);
        glBindVertexArray(mesh.vao);
        glGenBuffers(1, &mesh.vbo);
        glBindBuffer(GL_ARRAY_BUFFER, mesh.vbo);
        glBufferData(
            GL_ARRAY_BUFFER, vertices_.size() * sizeof(Vertex), vertices_.data(), GL_STATIC_DRAW);
        ConfigureVertices();
        cacheStats_.bytes += vertices_.size() * sizeof(Vertex);
        ++cacheStats_.uploads;
        pending.swap(vertices_);
        it = meshes_.emplace(key, mesh).first;
    }
    else
    {
        ++cacheStats_.hits;
    }
    it->second.lastFrame = frame_;
    commands_.push_back({&it->second, 0, it->second.count, offset, scale, opacity});
}

void PrototypeRenderer::TrimCache()
{
    // Evict only at frame boundaries: queued draws may still reference a mesh.
    while (meshes_.size() > MaxCachedMeshes || cacheStats_.bytes > MaxCachedBytes)
    {
        auto oldest = std::min_element(meshes_.begin(),
                                       meshes_.end(),
                                       [](const auto& a, const auto& b)
                                       {
                                           return a.second.lastFrame < b.second.lastFrame;
                                       });
        if (oldest == meshes_.end())
        {
            break;
        }
        auto& mesh = oldest->second;
        cacheStats_.bytes -= size_t(mesh.count) * sizeof(Vertex);
        glDeleteBuffers(1, &mesh.vbo);
        glDeleteVertexArrays(1, &mesh.vao);
        meshes_.erase(oldest);
        ++cacheStats_.evictions;
    }
}

void PrototypeRenderer::ClearCache()
{
    for (auto& entry : meshes_)
    {
        glDeleteBuffers(1, &entry.second.vbo);
        glDeleteVertexArrays(1, &entry.second.vao);
    }
    meshes_.clear();
    cacheStats_ = {};
}

bool PrototypeRenderer::VerifyMeshCache()
{
    const PostSettings saved = post_.settings;
    post_.settings.enabled = false;
    bool ok = true;
    auto check = [&](bool pass, const char* name)
    {
        std::cout << (pass ? "PASS " : "FAIL ") << name << '\n';
        ok = ok && pass;
    };
    int builds = 0;
    auto geometry = [&]()
    {
        ++builds;
        Rect(0, 0, 70, 45, Color(200, 100, 55, .7f));
        Rect(20, 10, 35, 22, Emissive(Color(30, 170, 230), 2));
    };
    auto render = [&](bool cached, Point offset, float scale, float opacity)
    {
        if (!Begin(width_, height_))
        {
            return std::vector<unsigned char>{};
        }
        Rect(offset.x - 10, offset.y - 10, 170, 120, Color(50, 80, 110));
        if (cached)
        {
            CachedMesh("test:shape", offset, scale, opacity, geometry);
        }
        else
        {
            Rect(offset.x, offset.y, 70 * scale, 45 * scale, Color(200, 100, 55, .7f * opacity));
            Rect(offset.x + 20 * scale,
                 offset.y + 10 * scale,
                 35 * scale,
                 22 * scale,
                 Emissive(Color(30, 170, 230, opacity), 2));
        }
        // A dynamic overlap after a cached draw tests painter's order too.
        Rect(offset.x + 30, offset.y + 20, 12, 18, Color(170, 20, 130, .4f));
        CompositeScene();
        Rect(8, 8, 20, 20, Color(80, 160, 220));
        Flush();
        std::vector<unsigned char> pixels(size_t(width_) * height_ * 3);
        glReadBuffer(GL_BACK);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(0, 0, width_, height_, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
        glPixelStorei(GL_PACK_ALIGNMENT, 4);
        return pixels;
    };
    for (int i = 0; i < 3; ++i)
    {
        Point offset{100.f + i * 90, 140.f + i * 30};
        float scale = i == 0 ? 1.f : 2.f, opacity = i == 2 ? .3f : 1.f;
        auto direct = render(false, offset, scale, opacity);
        auto cached = render(true, offset, scale, opacity);
        check(!direct.empty() && direct == cached,
              "cached mesh matches dynamic geometry, transform, opacity and draw order");
    }
    check(builds == 1, "same mesh built and uploaded only once across frames");
    check(Begin(width_, height_), "cache eviction test framebuffer");
    auto before = cacheStats_.uploads;
    for (size_t i = 0; i < MaxCachedMeshes + 2; ++i)
    {
        CachedMesh("test:eviction:" + std::to_string(i),
                   {0, 0},
                   1,
                   1,
                   [&]()
                   {
                       Rect(0, 0, 1, 1, Color(30, 40, 50));
                   });
    }
    Flush();
    check(cacheStats_.uploads == before + MaxCachedMeshes + 2,
          "different mesh keys create independent buffers");
    check(Begin(width_, height_), "cache eviction next frame");
    check(meshes_.size() <= MaxCachedMeshes && cacheStats_.bytes <= MaxCachedBytes
              && cacheStats_.evictions > 0,
          "LRU eviction bounds GPU mesh cache after travel");
    check(glGetError() == GL_NO_ERROR, "mesh cache has no OpenGL errors");
    post_.settings = saved;
    return ok;
}

bool PrototypeRenderer::Capture(const std::wstring& path) const
{
    // BMP rows are bottom-up, matching glReadPixels. Include row padding.
    int stride = (width_ * 3 + 3) & ~3;
    std::vector<unsigned char> pixels(stride * height_);
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    glReadBuffer(GL_BACK);
    glReadPixels(0, 0, width_, height_, GL_BGR, GL_UNSIGNED_BYTE, pixels.data());
    BITMAPFILEHEADER file = {};
    file.bfType = 0x4d42;
    file.bfOffBits = sizeof(file) + sizeof(BITMAPINFOHEADER);
    file.bfSize = file.bfOffBits + static_cast<DWORD>(pixels.size());
    BITMAPINFOHEADER info = {};
    info.biSize = sizeof(info);
    info.biWidth = width_;
    info.biHeight = height_;
    info.biPlanes = 1;
    info.biBitCount = 24;
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<char*>(&file), sizeof(file));
    out.write(reinterpret_cast<char*>(&info), sizeof(info));
    out.write(reinterpret_cast<char*>(pixels.data()), pixels.size());
    return out.good();
}
