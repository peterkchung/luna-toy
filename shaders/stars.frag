#version 450

layout(push_constant) uniform PushConstants {
    mat4 mvp;
    vec4 color;
} pc;

layout(location = 0) in float fragBrightness;
layout(location = 0) out vec4 outColor;

void main() {
    vec2 coord = gl_PointCoord * 2.0 - 1.0;
    float dist = dot(coord, coord);
    if (dist > 1.0) discard;

    float alpha = (1.0 - dist) * fragBrightness * pc.color.a;

    // Slight color variation - some stars warmer, some cooler
    vec3 starColor = mix(vec3(0.8, 0.85, 1.0), vec3(1.0, 0.95, 0.8), fragBrightness);
    outColor = vec4(starColor, alpha);
}
