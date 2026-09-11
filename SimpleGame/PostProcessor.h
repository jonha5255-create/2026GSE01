#pragma once
#include "Dependencies/glew.h"

struct PostSettings {
    bool enabled=true, bloom=true, vignette=true, edgeBlur=true;
    float exposure=1.1f, bloomStrength=.24f, vignetteStrength=.48f, edgeBlurStrength=.95f;
};

// Linear HDR scene -> filtered scene / highlights -> SDR composite.
// UI is rendered separately after Composite(), directly to the back buffer.
class PostProcessor {
public:
    bool Initialize();
    bool Begin(int width,int height);
    void Composite();
    void Shutdown();
    PostSettings settings;
    bool VerifyHDR() const;
private:
    struct Target { GLuint fbo=0,texture=0; };
    bool Resize(int width,int height);
    static bool Allocate(Target& target,int width,int height);
    static void Release(Target& target);
    void Filter(GLuint source,const Target& destination,float dx,float dy,bool extract);
    GLuint blurProgram_=0,compositeProgram_=0,vao_=0;
    GLint blurSource_=-1,blurStep_=-1,blurExtract_=-1;
    GLint sceneUniform_=-1,softUniform_=-1,bloomUniform_=-1;
    GLint enabledUniform_=-1,exposureUniform_=-1,amountsUniform_=-1;
    int width_=0,height_=0,halfWidth_=0,halfHeight_=0;
    Target scene_,soft_[2],bloom_[2];
};
