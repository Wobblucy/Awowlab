#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec3 inColor;
layout(location = 3) in vec2 inUV;  // UV coords (used by floor, ignored by spheres)

layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 proj;
} camera;

layout(push_constant) uniform PushConstants {
    mat4 model;
    vec4 color;
} push;

layout(location = 0) out vec3 fragNormal;
layout(location = 1) out vec3 fragColor;
layout(location = 2) out vec3 fragWorldPos;
layout(location = 3) out float fragAlpha;

void main() {
    vec4 worldPos = push.model * vec4(inPosition, 1.0);
    fragWorldPos = worldPos.xyz;

    gl_Position = camera.proj * camera.view * worldPos;

    // Transform normal to world space (assuming uniform scaling)
    fragNormal = normalize(mat3(push.model) * inNormal);

    fragColor = push.color.rgb * inColor;
    fragAlpha = push.color.a;
}
