#version 330 core
in vec4 tint;
in vec3 glow;
in vec3 worldNormal;
uniform bool linearScene;
out vec4 result;
void main()
{
    float light = .48 + .52*max(0,dot(normalize(worldNormal),vec3(.3,.85,.4)));
    vec3 rgb = tint.rgb*light + glow;
    if (linearScene)
        rgb = mix(rgb/12.92,pow((rgb+.055)/1.055,vec3(2.4)),step(vec3(.04045),rgb));
    result = vec4(rgb,tint.a);
}
