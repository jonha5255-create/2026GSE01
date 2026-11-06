#include "stdafx.h"
#include "PrototypeRenderer.h"
#include "FrameProfiler.h"
#include "ShaderProgram.h"
#include "RenderDiagnostics.h"
#include <stdexcept>
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
    GLint maxTexels = 0;
    glGetIntegerv(GL_MAX_TEXTURE_BUFFER_SIZE, &maxTexels);
    // Static cache (32 MiB) and the bounded animated stream share a page so painter
    // ordering does not force a texture/page switch for every character.
    atlasVertexLimit_ =
        std::min(size_t(maxTexels / 3), size_t(128 * 1024 * 1024 / (12 * sizeof(float))));
    atlasVertexLimit_ -= atlasVertexLimit_ % 3;
    descriptorLimit_ = std::min(size_t(maxTexels / 5), MaxTriangleInstances);
    if (atlasVertexLimit_ < 144 || !descriptorLimit_)
    {
        return false;
    }
    glGenVertexArrays(1, &vao_);
    glBindVertexArray(vao_);
    glGenBuffers(1, &vbo_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glEnableVertexAttribArray(0);
    glVertexAttribIPointer(0, 2, GL_UNSIGNED_INT, sizeof(TriangleInstance), nullptr);
    glVertexAttribDivisor(0, 1);
    glGenBuffers(1, &descriptorBuffer_);
    glGenTextures(1, &descriptorTexture_);
    glUseProgram(program_);
    glUniform1i(glGetUniformLocation(program_, "meshVertices"), 1);
    glUniform1i(glGetUniformLocation(program_, "instances"), 2);
    glUniform1i(glGetUniformLocation(program_, "atlas"), 0);

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
    vertices_.reserve(4096);
    commands_.reserve(4096);
    descriptors_.reserve(4096);
    triangles_.reserve(MaxTriangleInstances);
    cacheNamespace_ =
        "geometry-v2-layout12-font-"
        + std::to_string(MeshDiskCache::Hash(advances_.data(), advances_.size() * sizeof(float)))
        + ":";
    disk_.Initialize();
    RenderDiagnostics::Get().Event("renderer_limits",
                                   "GL_MAX_TEXTURE_BUFFER_SIZE=" + std::to_string(maxTexels)
                                       + "; max_triangle_instances="
                                       + std::to_string(MaxTriangleInstances));
    modelProgram_ = LoadShaderProgram(L"ArtModel.vs", L"ArtModel.fs");
    return modelProgram_ && post_.Initialize() && glGetError() == GL_NO_ERROR;
}

void PrototypeRenderer::Shutdown()
{
    for (auto& entry : gpuModels_)
    {
        glDeleteBuffers(1, &entry.second.buffer);
        glDeleteVertexArrays(1, &entry.second.vao);
    }
    gpuModels_.clear();
    modelBytes_ = 0;
    glDeleteProgram(modelProgram_);
    glDeleteRenderbuffers(1, &modelDepth_);
    modelProgram_ = modelDepth_ = 0;
    depthWidth_ = depthHeight_ = 0;
    commands_.clear();
    ClearCache();
    glDeleteBuffers(1, &descriptorBuffer_);
    glDeleteTextures(1, &descriptorTexture_);
    descriptorBuffer_ = descriptorTexture_ = 0;
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
    FrameProfiler::Get().BeginFrame();
    FrameProfiler::Get().Counters().gpuModelBytes = modelBytes_;
    FrameProfiler::Get().Counters().gpuModelEntries = gpuModels_.size();
    width_ = std::max(width, 1);
    height_ = std::max(height, 1);
    vertices_.clear();
    commands_.clear();
    ++frame_;
    TrimCache();
    scale_ = 1;
    glViewport(0, 0, width_, height_);
    linearScene_ = true;
    if (!post_.Begin(width_, height_))
    {
        return false;
    }
    GLint framebuffer = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &framebuffer);
    if (framebuffer)
    {
        if (!modelDepth_)
        {
            glGenRenderbuffers(1, &modelDepth_);
        }
        glBindRenderbuffer(GL_RENDERBUFFER, modelDepth_);
        if (depthWidth_ != width_ || depthHeight_ != height_)
        {
            glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, width_, height_);
            depthWidth_ = width_;
            depthHeight_ = height_;
        }
        glFramebufferRenderbuffer(
            GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, modelDepth_);
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
    if (!recording_)
    {
        Submit(Primitive(0),
               a,
               {b.x - a.x, b.y - a.y},
               {c.x - a.x, c.y - a.y},
               color,
               .5f / AtlasWidth,
               .5f / AtlasHeight,
               0,
               0,
               true);
        return;
    }
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
    if (!recording_)
    {
        Submit(Primitive(1),
               {x, y},
               {w, 0},
               {0, h},
               c,
               .5f / AtlasWidth,
               .5f / AtlasHeight,
               0,
               0,
               true);
        return;
    }
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
    if (!recording_)
    {
        Submit(Primitive(1),
               {a.x + x, a.y + y},
               {dx, dy},
               {-2 * x, -2 * y},
               c,
               .5f / AtlasWidth,
               .5f / AtlasHeight,
               0,
               0,
               true);
        return;
    }
    Quad({a.x + x, a.y + y}, {b.x + x, b.y + y}, {b.x - x, b.y - y}, {a.x - x, a.y - y}, c);
}

void PrototypeRenderer::Ellipse(Point p, float rx, float ry, Ink c)
{
    if (!recording_)
    {
        Submit(
            Primitive(2), p, {rx, 0}, {0, ry}, c, .5f / AtlasWidth, .5f / AtlasHeight, 0, 0, true);
        return;
    }
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
    if (!recording_)
    {
        Submit(Primitive(3),
               p,
               {radius, 0},
               {0, radius},
               c,
               .5f / AtlasWidth,
               .5f / AtlasHeight,
               0,
               0,
               true);
        return;
    }
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
        if (!recording_)
        {
            Submit(Primitive(1),
                   a,
                   {GlyphCell * scale, 0},
                   {0, GlyphCell * scale},
                   c,
                   u,
                   v,
                   u2 - u,
                   v2 - v,
                   true);
        }
        else
        {
            Push(a, c, u, v);
            Push(b, c, u2, v);
            Push(e, c, u2, v2);
            Push(a, c, u, v);
            Push(e, c, u2, v2);
            Push(d, c, u, v2);
        }
        x += advances_[i] * scale;
    }
}

PrototypeRenderer::Mesh& PrototypeRenderer::Primitive(int kind)
{
    return GetMesh("primitive:" + std::to_string(kind),
                   [&]()
                   {
                       Ink white = Color(255, 255, 255);
                       if (kind == 0)
                       {
                           Triangle({0, 0}, {1, 0}, {0, 1}, white);
                       }
                       else if (kind == 1)
                       {
                           Push({0, 0}, white, 0, 0);
                           Push({1, 0}, white, 1, 0);
                           Push({1, 1}, white, 1, 1);
                           Push({0, 0}, white, 0, 0);
                           Push({1, 1}, white, 1, 1);
                           Push({0, 1}, white, 0, 1);
                       }
                       else if (kind == 2)
                       {
                           Ellipse({0, 0}, 1, 1, white);
                       }
                       else
                       {
                           Ink edge = white;
                           edge.a = 0;
                           const auto& circle = UnitCircle<48>();
                           for (int i = 0; i < 48; ++i)
                           {
                               Push({0, 0}, white);
                               Push(circle[i], edge);
                               Push(circle[i + 1], edge);
                           }
                       }
                   });
}

PrototypeRenderer::Mesh& PrototypeRenderer::GetMesh(const std::string& key,
                                                    const std::function<void()>& build)
{
    auto found = meshes_.find(key);
    auto& stats = FrameProfiler::Get().Counters();
    if (found != meshes_.end())
    {
        ++cacheStats_.hits;
        ++stats.hits;
        found->second.lastFrame = frame_;
        return found->second;
    }
    ++stats.misses;
    Mesh mesh;
    bool persistent = key.find("test:") != 0;
    std::string diskKey = cacheNamespace_ + key;
    MeshLoad loaded = persistent ? disk_.Load(diskKey, mesh.data) : MeshLoad::Missing;
    if (loaded == MeshLoad::Loaded && mesh.data.size() / 12 > atlasVertexLimit_)
    {
        mesh.data.clear();
        loaded = MeshLoad::Rejected;
    }
    if (loaded == MeshLoad::Loaded)
    {
        ++cacheStats_.diskLoads;
        ++stats.loads;
        RenderDiagnostics::Get().Event("mesh_loaded", diskKey);
    }
    else
    {
        if (loaded == MeshLoad::Rejected)
        {
            ++cacheStats_.diskRejects;
            ++stats.rejects;
            RenderDiagnostics::Get().Event("mesh_rejected", diskKey);
        }
        float previousScale = scale_;
        vertices_.clear();
        recording_ = true;
        scale_ = 1;
        try
        {
            build();
        }
        catch (...)
        {
            recording_ = false;
            scale_ = previousScale;
            vertices_.clear();
            throw;
        }
        recording_ = false;
        scale_ = previousScale;
        if (vertices_.size() > atlasVertexLimit_)
        {
            throw std::runtime_error("Mesh exceeds GPU atlas page limit");
        }
        mesh.data.reserve(vertices_.size() * 12);
        for (const auto& v : vertices_)
        {
            const float packed[] = {v.x, v.y, v.u, v.v, v.r, v.g, v.b, v.a, v.energy, 0, 0, 0};
            mesh.data.insert(mesh.data.end(), std::begin(packed), std::end(packed));
        }
        vertices_.clear();
        if (!mesh.data.empty() && !MeshDiskCache::Validate(mesh.data))
        {
            throw std::runtime_error("Invalid generated mesh");
        }
        ++cacheStats_.generations;
        ++stats.generations;
        RenderDiagnostics::Get().Event("mesh_generated", diskKey);
        if (persistent && !mesh.data.empty())
        {
            if (disk_.Save(diskKey, mesh.data))
            {
                ++cacheStats_.diskWrites;
                ++stats.writes;
                RenderDiagnostics::Get().Event("mesh_saved", diskKey);
            }
            else
            {
                RenderDiagnostics::Get().Event("mesh_write_failed", diskKey);
            }
        }
    }
    mesh.count = GLsizei(mesh.data.size() / 12);
    mesh.lastFrame = frame_;
    cacheStats_.bytes += mesh.data.size() * sizeof(float);
    ++cacheStats_.uploads;
    atlasDirty_ = true;
    return meshes_.emplace(key, std::move(mesh)).first->second;
}

void PrototypeRenderer::Submit(const Mesh& mesh,
                               Point origin,
                               Point axisX,
                               Point axisY,
                               Ink tint,
                               float u,
                               float v,
                               float du,
                               float dv,
                               bool overrideUV)
{
    InstanceData instance{{axisX.x * scale_, axisY.x * scale_, origin.x * scale_, 0},
                          {axisX.y * scale_, axisY.y * scale_, origin.y * scale_, 0},
                          {tint.r, tint.g, tint.b, tint.a},
                          {u, v, du, dv},
                          {overrideUV ? 1.f : 0.f, tint.energy, 0, 0}};
    commands_.push_back({&mesh, instance});
    ++FrameProfiler::Get().Counters().objects;
}

void PrototypeRenderer::CachedMesh(const std::string& key,
                                   Point offset,
                                   float scale,
                                   float opacity,
                                   const std::function<void()>& build)
{
    if (recording_)
    {
        throw std::logic_error("Nested cached mesh builder");
    }
    auto& mesh = GetMesh(key, build);
    Submit(mesh, offset, {scale, 0}, {0, scale}, Color(255, 255, 255, opacity));
}

void PrototypeRenderer::ReleasePages()
{
    for (auto& page : pages_)
    {
        glDeleteTextures(1, &page.texture);
        glDeleteBuffers(1, &page.buffer);
    }
    pages_.clear();
}

void PrototypeRenderer::PackedMesh(const std::vector<float>& data,
                                   Point origin,
                                   float scale,
                                   float opacity)
{
    if (data.size() % 36 != 0)
    {
        throw std::invalid_argument("Incomplete packed triangles");
    }
    if (data.empty())
    {
        return;
    }
    if (recording_)
    {
        for (size_t i = 0; i < data.size(); i += 12)
        {
            Push({origin.x + data[i] * scale, origin.y + data[i + 1] * scale},
                 {data[i + 4], data[i + 5], data[i + 6], data[i + 7] * opacity, data[i + 8]},
                 data[i + 2],
                 data[i + 3]);
        }
        return;
    }
    streamedMeshes_.emplace_back();
    auto& mesh = streamedMeshes_.back();
    mesh.data = data;
    mesh.count = GLsizei(data.size() / 12);
    Submit(mesh, origin, {scale, 0}, {0, scale}, Color(255, 255, 255, opacity));
}

void PrototypeRenderer::ModelMesh(const std::string& key,
                                  const std::vector<ModelVertex>& vertices,
                                  const std::vector<std::array<float, 16>>& bones,
                                  Point origin,
                                  float scale,
                                  float yaw,
                                  std::array<float, 3> stretch,
                                  std::array<float, 3> offset,
                                  float opacity,
                                  bool sharedDepth,
                                  bool additive,
                                  bool linearMaterial)
{
    if (vertices.empty())
    {
        return;
    }
    if (bones.empty() || bones.size() > 56 || vertices.size() % 3)
    {
        throw std::invalid_argument("Invalid GPU model/palette");
    }
    Flush();
    auto found = gpuModels_.find(key);
    if (found == gpuModels_.end())
    {
        const size_t bytes = vertices.size() * sizeof(ModelVertex);
        if (bytes > 64 * 1024 * 1024)
        {
            throw std::runtime_error("GPU model exceeds cache budget");
        }
        while (!gpuModels_.empty()
               && (gpuModels_.size() >= 128 || modelBytes_ + bytes > 64 * 1024 * 1024))
        {
            auto oldest = std::min_element(gpuModels_.begin(),
                                           gpuModels_.end(),
                                           [](const auto& a, const auto& b)
                                           {
                                               return a.second.used < b.second.used;
                                           });
            modelBytes_ -= size_t(oldest->second.count) * sizeof(ModelVertex);
            glDeleteBuffers(1, &oldest->second.buffer);
            glDeleteVertexArrays(1, &oldest->second.vao);
            gpuModels_.erase(oldest);
            ++FrameProfiler::Get().Counters().evictions;
        }
        GpuModel mesh;
        mesh.count = GLsizei(vertices.size());
        glGenVertexArrays(1, &mesh.vao);
        glBindVertexArray(mesh.vao);
        glGenBuffers(1, &mesh.buffer);
        glBindBuffer(GL_ARRAY_BUFFER, mesh.buffer);
        glBufferData(GL_ARRAY_BUFFER, bytes, vertices.data(), GL_STATIC_DRAW);
        const int components[] = {3, 3, 4, 3, 4, 4};
        const size_t offsets[] = {offsetof(ModelVertex, position),
                                  offsetof(ModelVertex, normal),
                                  offsetof(ModelVertex, color),
                                  offsetof(ModelVertex, emission),
                                  offsetof(ModelVertex, joints),
                                  offsetof(ModelVertex, weights)};
        for (GLuint i = 0; i < 6; ++i)
        {
            glEnableVertexAttribArray(i);
            if (i == 4)
            {
                glVertexAttribIPointer(i,
                                       components[i],
                                       GL_UNSIGNED_INT,
                                       sizeof(ModelVertex),
                                       reinterpret_cast<void*>(offsets[i]));
            }
            else
            {
                glVertexAttribPointer(i,
                                      components[i],
                                      GL_FLOAT,
                                      GL_FALSE,
                                      sizeof(ModelVertex),
                                      reinterpret_cast<void*>(offsets[i]));
            }
        }
        found = gpuModels_.emplace(key, mesh).first;
        modelBytes_ += bytes;
        ++FrameProfiler::Get().Counters().gpuModelUploads;
        FrameProfiler::Get().Counters().meshBytes += bytes;
        RenderDiagnostics::Get().Event("gpu_model_upload",
                                       key + "; bytes=" + std::to_string(bytes)
                                           + "; resident=" + std::to_string(modelBytes_));
    }
    auto& mesh = found->second;
    mesh.used = frame_;
    glUseProgram(modelProgram_);
    auto uniform = [&](const char* name)
    {
        return glGetUniformLocation(modelProgram_, name);
    };
    glUniformMatrix4fv(uniform("bones"), GLsizei(bones.size()), GL_FALSE, bones[0].data());
    glUniform2f(uniform("viewport"), float(width_), float(height_));
    glUniform2f(uniform("origin"), origin.x, origin.y);
    glUniform1f(uniform("scale"), scale);
    glUniform1f(uniform("yaw"), yaw);
    glUniform1f(uniform("opacity"), opacity);
    glUniform3fv(uniform("stretch"), 1, stretch.data());
    glUniform3fv(uniform("offset"), 1, offset.data());
    glUniform1i(uniform("linearScene"), linearScene_);
    glUniform1i(uniform("linearMaterial"),linearMaterial);
    // The scene graph controls inter-actor painter order; depth resolves only
    // surfaces within this model, removing per-frame CPU triangle sorting.
    if (additive)
    {
        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    }
    else
    {
        glEnable(GL_DEPTH_TEST);
        glDepthMask(GL_TRUE);
        glDepthFunc(GL_LEQUAL);
        if (!sharedDepth)
        {
            glClear(GL_DEPTH_BUFFER_BIT);
        }
    }
    glBindVertexArray(mesh.vao);
    glDrawArrays(GL_TRIANGLES, 0, mesh.count);
    if (additive)
    {
        glDepthMask(GL_TRUE);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }
    glDisable(GL_DEPTH_TEST);
    FrameProfiler::Get().RecordDrawCall(linearScene_ ? DrawStage::Scene : DrawStage::UI);
    auto& counters = FrameProfiler::Get().Counters();
    counters.triangles += mesh.count / 3;
    counters.instanceBytes += bones.size() * sizeof(bones[0]);
    counters.gpuModelBytes = modelBytes_;
    counters.gpuModelEntries = gpuModels_.size();
}

void PrototypeRenderer::RebuildAtlas()
{
    if (!atlasDirty_)
    {
        return;
    }
    ReleasePages();
    std::vector<float> data;
    auto upload = [&]()
    {
        if (data.empty())
        {
            return;
        }
        AtlasPage page;
        glGenBuffers(1, &page.buffer);
        glBindBuffer(GL_TEXTURE_BUFFER, page.buffer);
        page.usedVertices = data.size() / 12;
        const size_t reserve = streamedMeshes_.empty() ? 0 : 1572864;
        page.capacity =
            page.usedVertices + std::min<size_t>(reserve, atlasVertexLimit_ - page.usedVertices);
        glBufferData(
            GL_TEXTURE_BUFFER, page.capacity * 12 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
        glBufferSubData(GL_TEXTURE_BUFFER, 0, data.size() * sizeof(float), data.data());
        glGenTextures(1, &page.texture);
        glBindTexture(GL_TEXTURE_BUFFER, page.texture);
        glTexBuffer(GL_TEXTURE_BUFFER, GL_RGBA32F, page.buffer);
        FrameProfiler::Get().Counters().meshBytes += data.size() * sizeof(float);
        pages_.push_back(page);
        data.clear();
    };
    for (auto& entry : meshes_)
    {
        auto& mesh = entry.second;
        if (data.size() / 12 + mesh.count > atlasVertexLimit_)
        {
            upload();
        }
        mesh.page = pages_.size();
        mesh.first = GLint(data.size() / 12);
        data.insert(data.end(), mesh.data.begin(), mesh.data.end());
    }
    upload();
    atlasDirty_ = false;
    RenderDiagnostics::Get().Event("atlas_rebuilt",
                                   "pages=" + std::to_string(pages_.size())
                                       + "; resident_bytes=" + std::to_string(cacheStats_.bytes));
}

void PrototypeRenderer::Flush()
{
    if (recording_)
    {
        throw std::logic_error("Flush during cached mesh generation");
    }
    if (commands_.empty())
    {
        return;
    }
    RebuildAtlas();
    // Reuse spare tails in the static atlas. Animated poses never rebuild the atlas
    // or create a GL buffer per actor/frame. Painter ordering is preserved.
    std::vector<std::vector<float>> streams(pages_.size());
    for (auto& mesh : streamedMeshes_)
    {
        if (size_t(mesh.count) > atlasVertexLimit_)
        {
            throw std::runtime_error("Stream mesh exceeds atlas limit");
        }
        size_t page = 0;
        while (page < pages_.size()
               && pages_[page].usedVertices + streams[page].size() / 12 + size_t(mesh.count)
                      > pages_[page].capacity)
        {
            ++page;
        }
        if (page == pages_.size())
        {
            AtlasPage extra;
            extra.capacity = std::min(atlasVertexLimit_, std::max<size_t>(262144, mesh.count));
            glGenBuffers(1, &extra.buffer);
            glBindBuffer(GL_TEXTURE_BUFFER, extra.buffer);
            glBufferData(
                GL_TEXTURE_BUFFER, extra.capacity * 12 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
            glGenTextures(1, &extra.texture);
            glBindTexture(GL_TEXTURE_BUFFER, extra.texture);
            glTexBuffer(GL_TEXTURE_BUFFER, GL_RGBA32F, extra.buffer);
            pages_.push_back(extra);
            streams.emplace_back();
        }
        mesh.page = page;
        mesh.first = GLint(pages_[page].usedVertices + streams[page].size() / 12);
        streams[page].insert(streams[page].end(), mesh.data.begin(), mesh.data.end());
    }
    for (size_t i = 0; i < streams.size(); ++i)
    {
        if (streams[i].empty())
        {
            continue;
        }
        glBindBuffer(GL_TEXTURE_BUFFER, pages_[i].buffer);
        glBufferSubData(GL_TEXTURE_BUFFER,
                        pages_[i].usedVertices * 12 * sizeof(float),
                        streams[i].size() * sizeof(float),
                        streams[i].data());
        FrameProfiler::Get().Counters().meshBytes += streams[i].size() * sizeof(float);
    }
    glUseProgram(program_);
    glUniform2f(viewport_, float(width_), float(height_));
    glUniform1i(linearUniform_, linearScene_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, atlas_);
    glBindVertexArray(vao_);
    size_t page = 0;
    triangles_.clear();
    descriptors_.clear();
    auto& counters = FrameProfiler::Get().Counters();
    auto draw = [&]()
    {
        if (triangles_.empty())
        {
            return;
        }
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_BUFFER, pages_[page].texture);
        glBindBuffer(GL_TEXTURE_BUFFER, descriptorBuffer_);
        glBufferData(GL_TEXTURE_BUFFER,
                     descriptors_.size() * sizeof(InstanceData),
                     descriptors_.data(),
                     GL_STREAM_DRAW);
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_BUFFER, descriptorTexture_);
        glTexBuffer(GL_TEXTURE_BUFFER, GL_RGBA32F, descriptorBuffer_);
        glBindBuffer(GL_ARRAY_BUFFER, vbo_);
        glBufferData(GL_ARRAY_BUFFER,
                     triangles_.size() * sizeof(TriangleInstance),
                     triangles_.data(),
                     GL_STREAM_DRAW);
        glDrawArraysInstanced(GL_TRIANGLES, 0, 3, GLsizei(triangles_.size()));
        FrameProfiler::Get().RecordDrawCall(linearScene_ ? DrawStage::Scene : DrawStage::UI);
        counters.triangles += triangles_.size();
        counters.instanceBytes += descriptors_.size() * sizeof(InstanceData)
                                  + triangles_.size() * sizeof(TriangleInstance);
        triangles_.clear();
        descriptors_.clear();
    };
    for (const auto& command : commands_)
    {
        const Mesh& mesh = *command.mesh;
        if (!mesh.count)
        {
            continue;
        }
        if (!triangles_.empty() && (page != mesh.page || descriptors_.size() >= descriptorLimit_))
        {
            ++counters.splits;
            draw();
        }
        page = mesh.page;
        GLuint descriptor = GLuint(descriptors_.size());
        descriptors_.push_back(command.instance);
        for (GLint vertex = 0; vertex < mesh.count; vertex += 3)
        {
            if (triangles_.size() == MaxTriangleInstances)
            {
                ++counters.splits;
                draw();
                descriptor = 0;
                descriptors_.push_back(command.instance);
            }
            triangles_.push_back({GLuint(mesh.first + vertex), descriptor});
        }
        if (!batchEnabled_)
        {
            draw();
        }
    }
    draw();
    glActiveTexture(GL_TEXTURE0);
    commands_.clear();
    streamedMeshes_.clear();
}

void PrototypeRenderer::TrimCache()
{
    while (meshes_.size() > MaxCachedMeshes || cacheStats_.bytes > MaxCachedBytes)
    {
        auto oldest = meshes_.end();
        for (auto it = meshes_.begin(); it != meshes_.end(); ++it)
        {
            if (it->first.find("primitive:") == 0)
            {
                continue;
            }
            if (oldest == meshes_.end() || it->second.lastFrame < oldest->second.lastFrame)
            {
                oldest = it;
            }
        }
        if (oldest == meshes_.end())
        {
            break;
        }
        cacheStats_.bytes -= oldest->second.data.size() * sizeof(float);
        RenderDiagnostics::Get().Event("mesh_evicted", oldest->first);
        meshes_.erase(oldest);
        atlasDirty_ = true;
        ++cacheStats_.evictions;
        ++FrameProfiler::Get().Counters().evictions;
    }
}

void PrototypeRenderer::ClearCache()
{
    ReleasePages();
    meshes_.clear();
    cacheStats_ = {};
    atlasDirty_ = true;
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
    check(builds == 1, "same cached geometry generated once across frames");
    batchEnabled_ = false;
    auto reference = render(true, {100, 140}, 2, .3f);
    auto referenceCalls = FrameProfiler::Get().DrawCalls();
    batchEnabled_ = true;
    auto batched = render(true, {100, 140}, 2, .3f);
    check(reference == batched && !batched.empty(),
          "ordered instancing matches separate GL submissions pixel for pixel");
    check(FrameProfiler::Get().DrawCalls() == 3 && referenceCalls > 3,
          "heterogeneous transparent meshes merge into one scene and one UI draw");
    check(disk_.SelfTest(), "persistent mesh cache validation");
    std::vector<ModelVertex> model(3);
    const float positions[3][3] = {{0, 0, 0}, {40, 0, 0}, {0, 40, 0}};
    for (size_t i = 0; i < model.size(); ++i)
    {
        std::copy(std::begin(positions[i]), std::end(positions[i]), model[i].position);
        model[i].normal[1] = 1;
        const float color[] = {.8f, .4f, .2f, 1};
        std::copy(std::begin(color), std::end(color), model[i].color);
        model[i].weights[0] = 1;
    }
    auto gpuReference = [&](int mode, float translation, float opacity)
    {
        Begin(width_, height_);
        Rect(200, 150, 200, 200, Color(50, 80, 110));
        auto project = [&](float x, float y)
        {
            return Point{250 + (x + translation) * .85f * 2,
                         250 + ((x + translation) * .425f - y) * 2};
        };
        if (mode == 1)
        {
            std::array<float, 16> bone{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, translation, 0, 0, 1};
            ModelMesh("test:gpu-skin", model, {bone}, {250, 250}, 2, 0, {1, 1, 1}, {}, opacity);
        }
        else if (mode == 2)
        {
            std::vector<float> packed;
            float light = .48f + .52f * .85f;
            for (auto p : {project(0, 0), project(40, 0), project(0, 40)})
            {
                const float vertex[] = {p.x,
                                        p.y,
                                        .5f / 3072,
                                        .5f / 2304,
                                        .8f * light,
                                        .4f * light,
                                        .2f * light,
                                        opacity,
                                        1,
                                        0,
                                        0,
                                        0};
                packed.insert(packed.end(), std::begin(vertex), std::end(vertex));
            }
            PackedMesh(packed, {0, 0}, 1);
        }
        else
        {
            float light = .48f + .52f * .85f;
            Triangle(project(0, 0),
                     project(40, 0),
                     project(0, 40),
                     {.8f * light, .4f * light, .2f * light, opacity});
        }
        Rect(270, 230, 12, 18, Color(170, 20, 130, .4f));
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
        auto cpu = gpuReference(false, float(i) * 17, i == 2 ? .4f : 1);
        auto gpu = gpuReference(true, float(i) * 17, i == 2 ? .4f : 1);
        auto packed = gpuReference(2, float(i) * 17, i == 2 ? .4f : 1);
        check(cpu == packed,
              "packed model stream preserves pixels and cached/static painter order");
        size_t differences = 0;
        for (size_t k = 0; k < cpu.size(); ++k)
        {
            differences += std::abs(int(cpu[k]) - int(gpu[k])) > 2;
        }
        check(differences < 100,
              "GPU skinning matches CPU projection, bone translation, opacity and painter order");
    }
    check(gpuModels_.count("test:gpu-skin") == 1, "bone animation reuses immutable GPU geometry");
    Begin(width_, height_);
    // Force multiple descriptor batches without changing draw order or positions.
    size_t savedLimit = descriptorLimit_;
    descriptorLimit_ = 2;
    auto split = render(true, {100, 140}, 2, .3f);
    check(split == batched && FrameProfiler::Get().Counters().splits > 0,
          "descriptor-capacity batch splits preserve pixels");
    descriptorLimit_ = savedLimit;
    size_t savedPageLimit = atlasVertexLimit_;
    atlasVertexLimit_ = 12;
    atlasDirty_ = true;
    auto paged = render(true, {100, 140}, 2, .3f);
    check(paged == batched && FrameProfiler::Get().Counters().splits > 0,
          "atlas-page batch splits preserve pixels");
    atlasVertexLimit_ = savedPageLimit;
    atlasDirty_ = true;
    // Disk-backed cached builder must not run after memory eviction.
    std::string roundTrip = "roundtrip-v1:" + std::to_string(GetCurrentProcessId()) + ":"
                            + std::to_string(GetTickCount64());
    int diskBuilds = 0;
    auto diskBuilder = [&]()
    {
        ++diskBuilds;
        Rect(0, 0, 2, 2, Color(255, 255, 255));
    };
    CachedMesh(roundTrip, {0, 0}, 1, 1, diskBuilder);
    Flush();
    auto found = meshes_.find(roundTrip);
    cacheStats_.bytes -= found->second.data.size() * sizeof(float);
    meshes_.erase(found);
    atlasDirty_ = true;
    bool savedRead = disk_.readEnabled;
    disk_.readEnabled = true;
    CachedMesh(roundTrip, {0, 0}, 1, 1, diskBuilder);
    Flush();
    disk_.readEnabled = savedRead;
    check(diskBuilds == 1, "evicted mesh reloads from disk without running builder");
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
          "different mesh keys create independent atlas entries");
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
