#version 450

// Vertex attributes (same layout as sphere/classicon)
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec3 inColor;
layout(location = 3) in vec2 inUV;

layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 proj;
} camera;

layout(push_constant) uniform PushConstants {
    mat4 model;      // 64 bytes
    vec4 color;      // 16 bytes (reserved for selection highlight)
} push;

layout(location = 0) out vec3 fragNormal;
layout(location = 1) out vec2 fragUV;
layout(location = 2) out vec3 fragWorldPos;

void main() {
    vec4 worldPos = push.model * vec4(inPosition, 1.0);
    fragWorldPos = worldPos.xyz;

    gl_Position = camera.proj * camera.view * worldPos;

    // Transform normal to world space (assuming uniform scaling)
    fragNormal = normalize(mat3(push.model) * inNormal);

    fragUV = inUV;
}
