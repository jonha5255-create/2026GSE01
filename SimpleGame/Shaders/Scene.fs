#version 330 core
in vec4 tint;
in vec2 texcoord;
in float energy;
uniform sampler2D atlas;
uniform bool linearScene;
out vec4 result;

void main()
{
    vec3 rgb = tint.rgb;
    if (linearScene)
    {
        rgb = mix(rgb / 12.92, pow((rgb + .055) / 1.055, vec3(2.4)), step(vec3(.04045), rgb))
              * energy;
    }
    result = vec4(rgb, tint.a * texture(atlas, texcoord).r);
}
