#version 450

layout(push_constant) uniform PushConstants {
    mat4 mvp;
    vec4 color;
} pc;

layout(location = 0) in float fragLife;
layout(location = 0) out vec4 outColor;

void main() {
    // Circular point sprite
    vec2 coord = gl_PointCoord * 2.0 - 1.0;
    float dist = dot(coord, coord);
    if (dist > 1.0) discard;

    // Color transitions: white -> yellow -> orange -> red -> transparent
    vec3 hotColor = vec3(1.0, 1.0, 0.9);
    vec3 warmColor = vec3(1.0, 0.6, 0.1);
    vec3 coolColor = vec3(0.8, 0.2, 0.0);

    vec3 color;
    if (fragLife > 0.6) {
        color = mix(warmColor, hotColor, (fragLife - 0.6) / 0.4);
    } else if (fragLife > 0.3) {
        color = mix(coolColor, warmColor, (fragLife - 0.3) / 0.3);
    } else {
        color = coolColor * (fragLife / 0.3);
    }

    float alpha = fragLife * (1.0 - dist) * pc.color.a;
    outColor = vec4(color, alpha);
}
