#version 330 core
in vec2 uv;
out vec4 result;
uniform sampler2D sceneImage, softImage, bloomImage;
uniform bool enabled;
uniform float exposure;
uniform vec3 amounts; // bloom, vignette, edge blur

vec3 toSRGB(vec3 c)
{
    c = max(c, vec3(0.));
    return mix(12.92 * c, 1.055 * pow(c, vec3(1. / 2.4)) - .055, step(vec3(.0031308), c));
}

vec3 filmic(vec3 x)
{
    return clamp((x * (2.51 * x + .03)) / (x * (2.43 * x + .59) + .14), 0., 1.);
}

void main()
{
    vec3 color = texture(sceneImage, uv).rgb;
    if (enabled)
    {
        // Elliptical screen-space falloff: clear center, gradually
        // softer edges.
        float radius = length((uv * 2. - 1.) * vec2(.88, 1.));
        float edge = smoothstep(.40, 1.12, radius);
        color = mix(color, texture(softImage, uv).rgb, edge * amounts.z);
        color += texture(bloomImage, uv).rgb * amounts.x;
        color *= 1. - smoothstep(.35, 1.25, radius) * amounts.y;
        color = filmic(color * exposure);
    }
    result = vec4(toSRGB(clamp(color, 0., 1.)), 1.);
}
