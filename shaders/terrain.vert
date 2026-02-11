#version 450

layout(push_constant) uniform PushConstants {
    mat4 mvp;
    vec4 color;
} pc;

layout(location = 0) in vec2 inPosition;
layout(location = 0) out vec2 fragWorldPos;

void main() {
    gl_Position = pc.mvp * vec4(inPosition, 0.0, 1.0);
    fragWorldPos = inPosition;
}
