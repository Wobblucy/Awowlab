#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec3 inColor;
layout(location = 3) in vec2 inUV;

layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 proj;
} camera;

// useTexture separates the textured front face from the solid back and edge
layout(push_constant) uniform PushConstants {
    mat4 model;      // 64 bytes
    vec4 color;      // 16 bytes (RGB + alpha for selection highlight)
    float useTexture; // 4 bytes (1.0 = sample texture, 0.0 = solid color)
    float padding1;   // 4 bytes
    float padding2;   // 4 bytes
    float padding3;   // 4 bytes
} push;

layout(location = 0) out vec3 fragNormal;
layout(location = 1) out vec3 fragColor;
layout(location = 2) out vec2 fragUV;
layout(location = 3) out float fragUseTexture;
layout(location = 4) out vec3 fragWorldPos;

void main() {
    vec4 worldPos = push.model * vec4(inPosition, 1.0);
    fragWorldPos = worldPos.xyz;

    gl_Position = camera.proj * camera.view * worldPos;

    fragNormal = normalize(mat3(push.model) * inNormal);

    fragColor = push.color.rgb;

    fragUV = inUV;

    fragUseTexture = push.useTexture;
}
