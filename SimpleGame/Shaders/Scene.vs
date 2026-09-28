#version 330 core
// One instance is one triangle of an immutable mesh. Instance order is painter order.
layout(location = 0) in uvec2 triangleReference;
uniform samplerBuffer meshVertices;
uniform samplerBuffer instances;
uniform vec2 viewport;
out vec4 tint;
out vec2 texcoord;
out float energy;

void main()
{
    int vertex = (int(triangleReference.x) + gl_VertexID) * 3;
    vec4 geometry = texelFetch(meshVertices, vertex);
    vec4 color = texelFetch(meshVertices, vertex + 1);
    float emission = texelFetch(meshVertices, vertex + 2).x;
    int instance = int(triangleReference.y) * 5;
    vec4 row0 = texelFetch(instances, instance);
    vec4 row1 = texelFetch(instances, instance + 1);
    vec4 colorScale = texelFetch(instances, instance + 2);
    vec4 uvRect = texelFetch(instances, instance + 3);
    vec4 options = texelFetch(instances, instance + 4);
    vec3 local = vec3(geometry.xy, 1.);
    vec2 screen = vec2(dot(row0.xyz, local), dot(row1.xyz, local));
    gl_Position = vec4(screen.x / viewport.x * 2. - 1., 1. - screen.y / viewport.y * 2., 0., 1.);
    tint = color * colorScale;
    texcoord = options.x > 0.5 ? uvRect.xy + geometry.zw * uvRect.zw : geometry.zw;
    energy = emission * options.y;
}
