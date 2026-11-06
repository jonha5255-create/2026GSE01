#version 330 core
in vec4 tint;
in vec3 glow;
in vec3 worldNormal;
uniform bool linearScene;
uniform bool linearMaterial;
out vec4 result;
void main()
{
    float lengthN = length(worldNormal);
    vec3 normal = lengthN > .000001 ? worldNormal/lengthN : vec3(0,1,0);
    float light = .48 + .52*max(0,dot(normal,vec3(.3,.85,.4)));
    vec3 rgb = tint.rgb*light;
    if (linearScene && !linearMaterial)
        rgb = mix(rgb/12.92,pow((rgb+.055)/1.055,vec3(2.4)),step(vec3(.04045),rgb));
    result = vec4(rgb+glow,tint.a);
}
