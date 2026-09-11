#include "stdafx.h"
#include "PrototypeRenderer.h"
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>

namespace {
GLuint Shader(GLenum type, const char* source) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[2048] = {};
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        std::cerr << log << '\n';
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}
}

bool PrototypeRenderer::Initialize() {
    const char* vs = R"(#version 330 core
layout(location=0) in vec2 position;
layout(location=1) in vec4 color;
layout(location=2) in vec2 uv;
layout(location=3) in float emission;
uniform vec2 viewport;
out vec4 tint; out vec2 texcoord;
out float energy;
void main() {
 gl_Position=vec4(position.x/viewport.x*2.-1.,1.-position.y/viewport.y*2.,0.,1.);
 tint=color; texcoord=uv; energy=emission;
})";
    const char* fs = R"(#version 330 core
in vec4 tint; in vec2 texcoord;
in float energy;
uniform sampler2D atlas;
uniform bool linearScene;
out vec4 result;
void main(){
 vec3 rgb=tint.rgb;
 if(linearScene) {
   rgb=mix(rgb/12.92,pow((rgb+.055)/1.055,vec3(2.4)),step(vec3(.04045),rgb))*energy;
 }
 result=vec4(rgb,tint.a*texture(atlas,texcoord).r);
}
)";
    GLuint vertex = Shader(GL_VERTEX_SHADER, vs), fragment = Shader(GL_FRAGMENT_SHADER, fs);
    if (!vertex || !fragment) {
        if (vertex) glDeleteShader(vertex);
        if (fragment) glDeleteShader(fragment);
        return false;
    }
    program_ = glCreateProgram();
    glAttachShader(program_, vertex); glAttachShader(program_, fragment);
    glLinkProgram(program_);
    glDeleteShader(vertex); glDeleteShader(fragment);
    GLint ok = 0; glGetProgramiv(program_, GL_LINK_STATUS, &ok);
    if (!ok) { Shutdown(); return false; }
    viewport_ = glGetUniformLocation(program_, "viewport");
    linearUniform_ = glGetUniformLocation(program_, "linearScene");
    glGenVertexArrays(1, &vao_); glBindVertexArray(vao_);
    glGenBuffers(1, &vbo_); glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glEnableVertexAttribArray(0); glEnableVertexAttribArray(1); glEnableVertexAttribArray(2);
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), nullptr);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)(2 * sizeof(float)));
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)(6 * sizeof(float)));
    glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)(8 * sizeof(float)));

    // Preload ASCII and all 11,172 modern Hangul syllables. Text remains local;
    // UTF-8 dialogue may change without editing a hand-picked glyph list.
    HDC dc = CreateCompatibleDC(nullptr);
    BITMAPINFO info = {}; info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = AtlasWidth; info.bmiHeader.biHeight = -AtlasHeight;
    info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32;
    void* bits = nullptr;
    HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    HFONT font = CreateFontW(-20, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
        DEFAULT_PITCH, L"Malgun Gothic");
    if (!dc || !bitmap || !font) {
        if (bitmap) DeleteObject(bitmap);
        if (font) DeleteObject(font);
        if (dc) DeleteDC(dc);
        Shutdown(); return false;
    }
    HGDIOBJ oldBitmap = SelectObject(dc, bitmap), oldFont = SelectObject(dc, font);
    PatBlt(dc, 0, 0, AtlasWidth, AtlasHeight, BLACKNESS);
    SetTextColor(dc, RGB(255,255,255)); SetBkMode(dc, TRANSPARENT);
    advances_.assign(GlyphCount, 20.f);
    for (int i = 1; i < GlyphCount; ++i) {
        wchar_t c = static_cast<wchar_t>(i < HangulStart ? i + 31 : 0xAC00 + i - HangulStart);
        int x=(i % (AtlasWidth/GlyphCell))*GlyphCell;
        int y=(i / (AtlasWidth/GlyphCell))*GlyphCell;
        TextOutW(dc, x, y, &c, 1);
        SIZE extent={}; GetTextExtentPoint32W(dc,&c,1,&extent);
        advances_[i]=static_cast<float>(extent.cx);
    }
    GdiFlush();
    std::vector<unsigned char> pixels(AtlasWidth * AtlasHeight);
    auto src = static_cast<unsigned char*>(bits);
    for (size_t i = 0; i < pixels.size(); ++i) pixels[i] = src[i * 4];
    pixels[0] = 255;
    SelectObject(dc, oldBitmap); SelectObject(dc, oldFont);
    DeleteObject(bitmap); DeleteObject(font); DeleteDC(dc);
    glGenTextures(1, &atlas_); glBindTexture(GL_TEXTURE_2D, atlas_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, AtlasWidth, AtlasHeight, 0, GL_RED, GL_UNSIGNED_BYTE, pixels.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);
    vertices_.reserve(250000);
    return post_.Initialize() && glGetError() == GL_NO_ERROR;
}

void PrototypeRenderer::Shutdown() {
    post_.Shutdown();
    if (atlas_) glDeleteTextures(1, &atlas_);
    if (vbo_) glDeleteBuffers(1, &vbo_);
    if (vao_) glDeleteVertexArrays(1, &vao_);
    if (program_) glDeleteProgram(program_);
    atlas_ = vbo_ = vao_ = program_ = 0;
}
bool PrototypeRenderer::Begin(int width, int height) {
    width_ = std::max(width, 1); height_ = std::max(height, 1);
    vertices_.clear(); scale_=1; glViewport(0, 0, width_, height_);
    linearScene_=true;
    if(!post_.Begin(width_,height_)) return false;
    glClearColor(.002f, .0033f, .0047f, 1); glClear(GL_COLOR_BUFFER_BIT);
    return true;
}
void PrototypeRenderer::CompositeScene() {
    Flush();post_.Composite();linearScene_=false;
}
void PrototypeRenderer::Push(Point p, Ink c, float u, float v) {
    vertices_.push_back({p.x*scale_,p.y*scale_,c.r,c.g,c.b,c.a,u,v,c.energy});
}
void PrototypeRenderer::Triangle(Point a, Point b, Point c, Ink color) {
    Push(a,color); Push(b,color); Push(c,color);
}
void PrototypeRenderer::Quad(Point a, Point b, Point c, Point d, Ink color) {
    Triangle(a,b,c,color); Triangle(a,c,d,color);
}
void PrototypeRenderer::Rect(float x,float y,float w,float h,Ink c) {
    Quad({x,y},{x+w,y},{x+w,y+h},{x,y+h},c);
}
void PrototypeRenderer::Line(Point a,Point b,float width,Ink c) {
    float dx=b.x-a.x,dy=b.y-a.y,len=std::sqrt(dx*dx+dy*dy);
    if(len<.001f) return;
    float x=-dy/len*width*.5f,y=dx/len*width*.5f;
    Quad({a.x+x,a.y+y},{b.x+x,b.y+y},{b.x-x,b.y-y},{a.x-x,a.y-y},c);
}
void PrototypeRenderer::Ellipse(Point p,float rx,float ry,Ink c) {
    for(int i=0;i<32;++i) {
        float a=i*6.2831853f/32,b=(i+1)*6.2831853f/32;
        Triangle(p,{p.x+std::cos(a)*rx,p.y+std::sin(a)*ry},
            {p.x+std::cos(b)*rx,p.y+std::sin(b)*ry},c);
    }
}
void PrototypeRenderer::Glow(Point p,float radius,Ink c) {
    // Subtle local haze; actual light spreading comes from HDR bloom.
    // Interpolated alpha avoids visible concentric discs after tone mapping.
    c.a*=.12f;c.energy=1;
    Ink edge=c;edge.a=0;
    for(int i=0;i<48;++i) {
        float a=i*6.2831853f/48,b=(i+1)*6.2831853f/48;
        Push(p,c);
        Push({p.x+std::cos(a)*radius,p.y+std::sin(a)*radius},edge);
        Push({p.x+std::cos(b)*radius,p.y+std::sin(b)*radius},edge);
    }
}
void PrototypeRenderer::Text(float x,float y,const std::string& text,Ink c,float scale) {
    if(text.empty()) return;
    int count=MultiByteToWideChar(CP_UTF8,0,text.data(),static_cast<int>(text.size()),nullptr,0);
    if(count<=0) return;
    std::wstring wide(count,L' ');
    MultiByteToWideChar(CP_UTF8,0,text.data(),static_cast<int>(text.size()),&wide[0],count);
    Text(x,y,wide,c,scale);
}
void PrototypeRenderer::Text(float x,float y,const std::wstring& text,Ink c,float scale) {
    float start=x;
    for(wchar_t ch:text) {
        if(ch=='\n'){y+=24*scale;x=start;continue;}
        int i=ch>=32&&ch<=126?ch-31:
            ch>=0xAC00&&ch<=0xD7A3?HangulStart+ch-0xAC00:'?'-31;
        float u=float((i%(AtlasWidth/GlyphCell))*GlyphCell)/AtlasWidth;
        float v=float((i/(AtlasWidth/GlyphCell))*GlyphCell)/AtlasHeight;
        float u2=u+float(GlyphCell)/AtlasWidth,v2=v+float(GlyphCell)/AtlasHeight;
        Point a{x,y},b{x+GlyphCell*scale,y},d{x,y+GlyphCell*scale},e{b.x,d.y};
        Push(a,c,u,v);Push(b,c,u2,v);Push(e,c,u2,v2);
        Push(a,c,u,v);Push(e,c,u2,v2);Push(d,c,u,v2);
        x+=advances_[i]*scale;
    }
}
void PrototypeRenderer::Flush() {
    glUseProgram(program_); glUniform2f(viewport_,float(width_),float(height_));
    glUniform1i(linearUniform_,linearScene_);
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D,atlas_);
    glBindVertexArray(vao_); glBindBuffer(GL_ARRAY_BUFFER,vbo_);
    glBufferData(GL_ARRAY_BUFFER,vertices_.size()*sizeof(Vertex),vertices_.data(),GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES,0,static_cast<GLsizei>(vertices_.size()));
    vertices_.clear();
}
bool PrototypeRenderer::Capture(const std::wstring& path) const {
    // BMP rows are bottom-up, matching glReadPixels. Include row padding.
    int stride=(width_*3+3)&~3;
    std::vector<unsigned char> pixels(stride*height_);
    glPixelStorei(GL_PACK_ALIGNMENT,4); glReadBuffer(GL_BACK);
    glReadPixels(0,0,width_,height_,GL_BGR,GL_UNSIGNED_BYTE,pixels.data());
    BITMAPFILEHEADER file={}; file.bfType=0x4d42;
    file.bfOffBits=sizeof(file)+sizeof(BITMAPINFOHEADER);
    file.bfSize=file.bfOffBits+static_cast<DWORD>(pixels.size());
    BITMAPINFOHEADER info={}; info.biSize=sizeof(info);
    info.biWidth=width_;info.biHeight=height_;info.biPlanes=1;info.biBitCount=24;
    std::ofstream out(path,std::ios::binary);
    out.write(reinterpret_cast<char*>(&file),sizeof(file));
    out.write(reinterpret_cast<char*>(&info),sizeof(info));
    out.write(reinterpret_cast<char*>(pixels.data()),pixels.size());
    return out.good();
}
