#version 450

layout(push_constant) uniform PushConstants {
    mat4 mvp;
    vec4 color;
} pc;

layout(location = 0) in vec2 fragWorldPos;
layout(location = 0) out vec4 outColor;

float hash(vec2 p) {
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}

float noise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float a = hash(i);
    float b = hash(i + vec2(1.0, 0.0));
    float c = hash(i + vec2(0.0, 1.0));
    float d = hash(i + vec2(1.0, 1.0));
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

void main() {
    vec3 baseColor = pc.color.rgb;
    float n1 = noise(fragWorldPos * 8.0) * 0.15;
    float n2 = noise(fragWorldPos * 32.0) * 0.08;
    float n3 = noise(fragWorldPos * 64.0) * 0.04;
    float detail = n1 + n2 + n3;

    float crater = noise(fragWorldPos * 3.0);
    crater = smoothstep(0.55, 0.6, crater) * 0.2;

    vec3 finalColor = baseColor + detail - crater;
    float gradient = smoothstep(-0.5, 0.5, fragWorldPos.y) * 0.1;
    finalColor -= gradient;
    outColor = vec4(clamp(finalColor, 0.0, 1.0), 1.0);
}
