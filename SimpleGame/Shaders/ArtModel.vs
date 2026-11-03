#version 330 core
layout(location=0) in vec3 position;
layout(location=1) in vec3 normal;
layout(location=2) in vec4 color;
layout(location=3) in vec3 emission;
layout(location=4) in uvec4 joints;
layout(location=5) in vec4 weights;
uniform mat4 bones[56];
uniform vec2 viewport, origin;
uniform float scale, yaw, opacity;
uniform vec3 stretch, offset;
out vec4 tint;
out vec3 glow;
out vec3 worldNormal;
void main()
{
    mat4 skin = bones[joints.x]*weights.x;
    if (weights.y > 0) skin += bones[joints.y]*weights.y;
    if (weights.z > 0) skin += bones[joints.z]*weights.z;
    if (weights.w > 0) skin += bones[joints.w]*weights.w;
    vec3 p = (skin * vec4(position,1)).xyz + offset;
    float c = cos(yaw), s = sin(yaw);
    mat3 turn = mat3(c,0,-s, 0,1,0, s,0,c);
    p = (turn*p)*stretch;
    worldNormal = (turn * mat3(skin) * normal) / max(stretch,vec3(.0001));
    vec2 screen = origin + vec2((p.x-p.z)*.85, (p.x+p.z)*.425-p.y)*scale;
    gl_Position = vec4(screen.x/viewport.x*2-1, 1-screen.y/viewport.y*2,
                       clamp(-(p.x+p.z+1.6*p.y)/10000,-.999,.999),1);
    tint = vec4(color.rgb, color.a*opacity);
    glow = emission;
}
