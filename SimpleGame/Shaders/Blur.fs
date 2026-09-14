#version 330 core
in vec2 uv;
out vec4 result;
uniform sampler2D source;
uniform vec2 stepUV;
uniform bool extractHighlights;

vec3 sampleAt(vec2 p)
{
    vec3 c = texture(source, p).rgb;
    if (extractHighlights)
    {
        // Brightness threshold in linear HDR; saturated neon must also
        // bloom.
        float brightness = max(c.r, max(c.g, c.b));
        float knee = clamp(brightness - .5, 0., 1.);
        float contribution = max(brightness - 1., knee * knee * .5);
        c *= contribution / max(brightness, .00001);
    }
    return c;
}

void main()
{
    vec3 c = sampleAt(uv) * .227027;
    c += (sampleAt(uv + stepUV) + sampleAt(uv - stepUV)) * .1945946;
    c += (sampleAt(uv + stepUV * 2.) + sampleAt(uv - stepUV * 2.)) * .1216216;
    c += (sampleAt(uv + stepUV * 3.) + sampleAt(uv - stepUV * 3.)) * .054054;
    c += (sampleAt(uv + stepUV * 4.) + sampleAt(uv - stepUV * 4.)) * .016216;
    result = vec4(c, 1.);
}
