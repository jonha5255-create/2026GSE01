#version 330 core
layout(location = 0) in vec2 position;
layout(location = 1) in vec4 color;
layout(location = 2) in vec2 uv;
layout(location = 3) in float emission;
uniform vec2 viewport;
uniform vec2 meshOffset;
uniform float meshScale;
uniform float meshOpacity;
out vec4 tint;
out vec2 texcoord;
out float energy;

void main()
{
    vec2 screen = position * meshScale + meshOffset;
    gl_Position = vec4(screen.x / viewport.x * 2. - 1., 1. - screen.y / viewport.y * 2., 0., 1.);
    tint = vec4(color.rgb, color.a * meshOpacity);
    texcoord = uv;
    energy = emission;
}
