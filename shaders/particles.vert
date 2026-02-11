#version 450

layout(push_constant) uniform PushConstants {
    mat4 mvp;
    vec4 color;
} pc;

layout(location = 0) in vec2 inPosition;
layout(location = 1) in float inLife;      // 0.0 = dead, 1.0 = just born
layout(location = 2) in float inSize;

layout(location = 0) out float fragLife;

void main() {
    gl_Position = pc.mvp * vec4(inPosition, 0.0, 1.0);
    gl_PointSize = inSize * (0.5 + inLife * 0.5);
    fragLife = inLife;
}
