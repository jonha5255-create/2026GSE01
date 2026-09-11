#include "stdafx.h"
#include "PostProcessor.h"
#include <algorithm>
#include <iostream>
#include <vector>
#include <cmath>

namespace {
const char* Fullscreen=R"(#version 330 core
out vec2 uv;
void main() {
 vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);
 uv=p; gl_Position=vec4(p*2.-1.,0.,1.);
})";
GLuint Program(const char* fragment) {
    GLuint program=glCreateProgram();
    const GLenum types[]={GL_VERTEX_SHADER,GL_FRAGMENT_SHADER};
    const char* sources[]={Fullscreen,fragment};
    for(int i=0;i<2;++i) {
        GLuint shader=glCreateShader(types[i]);
        glShaderSource(shader,1,&sources[i],nullptr);glCompileShader(shader);
        GLint ok=0;glGetShaderiv(shader,GL_COMPILE_STATUS,&ok);
        if(!ok) {
            char log[2048]={};glGetShaderInfoLog(shader,sizeof(log),nullptr,log);
            std::cerr<<"Post shader: "<<log<<'\n';
            glDeleteShader(shader);glDeleteProgram(program);return 0;
        }
        glAttachShader(program,shader);glDeleteShader(shader);
    }
    glLinkProgram(program);GLint ok=0;glGetProgramiv(program,GL_LINK_STATUS,&ok);
    if(!ok) {
        char log[2048]={};glGetProgramInfoLog(program,sizeof(log),nullptr,log);
        std::cerr<<"Post link: "<<log<<'\n';glDeleteProgram(program);return 0;
    }
    return program;
}
}
bool PostProcessor::Initialize() {
    const char* blur=R"(#version 330 core
in vec2 uv; out vec4 result;
uniform sampler2D source;
uniform vec2 stepUV;
uniform bool extractHighlights;
vec3 sampleAt(vec2 p) {
 vec3 c=texture(source,p).rgb;
 if(extractHighlights) {
   // Brightness threshold in linear HDR; saturated neon must also bloom.
   float brightness=max(c.r,max(c.g,c.b));
   float knee=clamp(brightness-.5,0.,1.);
   float contribution=max(brightness-1.,knee*knee*.5);
   c*=contribution/max(brightness,.00001);
 }
 return c;
}
void main() {
 vec3 c=sampleAt(uv)*.227027;
 c+=(sampleAt(uv+stepUV)+sampleAt(uv-stepUV))*.1945946;
 c+=(sampleAt(uv+stepUV*2.)+sampleAt(uv-stepUV*2.))*.1216216;
 c+=(sampleAt(uv+stepUV*3.)+sampleAt(uv-stepUV*3.))*.054054;
 c+=(sampleAt(uv+stepUV*4.)+sampleAt(uv-stepUV*4.))*.016216;
 result=vec4(c,1.);
})";
    const char* composite=R"(#version 330 core
in vec2 uv; out vec4 result;
uniform sampler2D sceneImage,softImage,bloomImage;
uniform bool enabled;
uniform float exposure;
uniform vec3 amounts; // bloom, vignette, edge blur
vec3 toSRGB(vec3 c) {
 c=max(c,vec3(0.));
 return mix(12.92*c,1.055*pow(c,vec3(1./2.4))-.055,step(vec3(.0031308),c));
}
vec3 filmic(vec3 x) {
 return clamp((x*(2.51*x+.03))/(x*(2.43*x+.59)+.14),0.,1.);
}
void main() {
 vec3 color=texture(sceneImage,uv).rgb;
 if(enabled) {
   // Elliptical screen-space falloff: clear center, gradually softer edges.
   float radius=length((uv*2.-1.)*vec2(.88,1.));
   float edge=smoothstep(.40,1.12,radius);
   color=mix(color,texture(softImage,uv).rgb,edge*amounts.z);
   color+=texture(bloomImage,uv).rgb*amounts.x;
   color*=1.-smoothstep(.35,1.25,radius)*amounts.y;
   color=filmic(color*exposure);
 }
 result=vec4(toSRGB(clamp(color,0.,1.)),1.);
})";
    blurProgram_=Program(blur);compositeProgram_=Program(composite);
    if(!blurProgram_||!compositeProgram_){Shutdown();return false;}
    blurSource_=glGetUniformLocation(blurProgram_,"source");
    blurStep_=glGetUniformLocation(blurProgram_,"stepUV");
    blurExtract_=glGetUniformLocation(blurProgram_,"extractHighlights");
    sceneUniform_=glGetUniformLocation(compositeProgram_,"sceneImage");
    softUniform_=glGetUniformLocation(compositeProgram_,"softImage");
    bloomUniform_=glGetUniformLocation(compositeProgram_,"bloomImage");
    enabledUniform_=glGetUniformLocation(compositeProgram_,"enabled");
    exposureUniform_=glGetUniformLocation(compositeProgram_,"exposure");
    amountsUniform_=glGetUniformLocation(compositeProgram_,"amounts");
    glGenVertexArrays(1,&vao_);
    return true;
}
bool PostProcessor::Allocate(Target& target,int width,int height) {
    glGenTextures(1,&target.texture);glBindTexture(GL_TEXTURE_2D,target.texture);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA16F,width,height,0,GL_RGBA,GL_FLOAT,nullptr);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    glGenFramebuffers(1,&target.fbo);glBindFramebuffer(GL_FRAMEBUFFER,target.fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,target.texture,0);
    return glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE;
}
void PostProcessor::Release(Target& target) {
    if(target.fbo)glDeleteFramebuffers(1,&target.fbo);
    if(target.texture)glDeleteTextures(1,&target.texture);
    target={};
}
bool PostProcessor::Resize(int width,int height) {
    if(width==width_&&height==height_&&scene_.fbo)return true;
    Release(scene_);for(auto& t:soft_)Release(t);for(auto& t:bloom_)Release(t);
    width_=height_=0;halfWidth_=std::max(1,(width+1)/2);halfHeight_=std::max(1,(height+1)/2);
    bool ok=Allocate(scene_,width,height);
    for(auto& t:soft_)ok=Allocate(t,halfWidth_,halfHeight_)&&ok;
    for(auto& t:bloom_)ok=Allocate(t,halfWidth_,halfHeight_)&&ok;
    if(!ok) {
        std::cerr<<"HDR framebuffer allocation failed: "<<width<<"x"<<height<<'\n';
        Release(scene_);for(auto& t:soft_)Release(t);for(auto& t:bloom_)Release(t);
        glBindFramebuffer(GL_FRAMEBUFFER,0);return false;
    }
    width_=width;height_=height;return true;
}
bool PostProcessor::Begin(int width,int height) {
    // Prevent double gamma encoding; both scene and composite shaders are explicit.
    glDisable(GL_FRAMEBUFFER_SRGB);glActiveTexture(GL_TEXTURE0);
    if(!Resize(width,height))return false;
    glBindFramebuffer(GL_FRAMEBUFFER,scene_.fbo);glViewport(0,0,width,height);
    glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    return true;
}
void PostProcessor::Filter(GLuint source,const Target& target,float dx,float dy,bool extract) {
    glBindFramebuffer(GL_FRAMEBUFFER,target.fbo);
    glViewport(0,0,halfWidth_,halfHeight_);glUseProgram(blurProgram_);
    glUniform1i(blurSource_,0);glUniform2f(blurStep_,dx,dy);glUniform1i(blurExtract_,extract);
    glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,source);
    glDrawArrays(GL_TRIANGLES,0,3);
}
void PostProcessor::Composite() {
    glDisable(GL_BLEND);glBindVertexArray(vao_);
    if(settings.enabled&&settings.edgeBlur) {
        Filter(scene_.texture,soft_[0],1.f/halfWidth_,0,false);
        Filter(soft_[0].texture,soft_[1],0,1.f/halfHeight_,false);
        Filter(soft_[1].texture,soft_[0],1.f/halfWidth_,0,false);
        Filter(soft_[0].texture,soft_[1],0,1.f/halfHeight_,false);
    }
    if(settings.enabled&&settings.bloom) {
        Filter(scene_.texture,bloom_[0],1.f/halfWidth_,0,true);
        Filter(bloom_[0].texture,bloom_[1],0,1.f/halfHeight_,false);
        Filter(bloom_[1].texture,bloom_[0],2.f/halfWidth_,0,false);
        Filter(bloom_[0].texture,bloom_[1],0,2.f/halfHeight_,false);
    }
    glBindFramebuffer(GL_FRAMEBUFFER,0);glViewport(0,0,width_,height_);
    glUseProgram(compositeProgram_);
    const GLuint textures[]={scene_.texture,settings.enabled&&settings.edgeBlur?soft_[1].texture:scene_.texture,
        settings.enabled&&settings.bloom?bloom_[1].texture:scene_.texture};
    for(int i=0;i<3;++i){glActiveTexture(GL_TEXTURE0+i);glBindTexture(GL_TEXTURE_2D,textures[i]);}
    glUniform1i(sceneUniform_,0);glUniform1i(softUniform_,1);glUniform1i(bloomUniform_,2);
    glUniform1i(enabledUniform_,settings.enabled);
    glUniform1f(exposureUniform_,settings.exposure);
    glUniform3f(amountsUniform_,settings.bloom?settings.bloomStrength:0,
        settings.vignette?settings.vignetteStrength:0,settings.edgeBlur?settings.edgeBlurStrength:0);
    glDrawArrays(GL_TRIANGLES,0,3);
    glActiveTexture(GL_TEXTURE0);glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
}
bool PostProcessor::VerifyHDR() const {
    // Diagnostic readback of the real scene target, not a synthetic HDR label.
    GLint previous=0;glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&previous);
    glBindFramebuffer(GL_READ_FRAMEBUFFER,scene_.fbo);glReadBuffer(GL_COLOR_ATTACHMENT0);
    std::vector<float> pixels(static_cast<size_t>(width_)*height_*4);
    glReadPixels(0,0,width_,height_,GL_RGBA,GL_FLOAT,pixels.data());
    glBindFramebuffer(GL_READ_FRAMEBUFFER,previous);
    float peak=0;bool finite=true;
    for(size_t i=0;i<pixels.size();i+=4)for(int c=0;c<3;++c) {
        finite=finite&&std::isfinite(pixels[i+c]);peak=std::max(peak,pixels[i+c]);
    }
    bool ok=finite&&peak>1.f;
    std::cout<<(ok?"PASS ":"FAIL ")<<"HDR scene radiance > 1 (peak "<<peak<<")\n";
    return ok;
}
void PostProcessor::Shutdown() {
    Release(scene_);for(auto& t:soft_)Release(t);for(auto& t:bloom_)Release(t);
    if(blurProgram_)glDeleteProgram(blurProgram_);
    if(compositeProgram_)glDeleteProgram(compositeProgram_);
    if(vao_)glDeleteVertexArrays(1,&vao_);
    blurProgram_=compositeProgram_=vao_=0;width_=height_=0;
}
