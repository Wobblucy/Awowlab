#version 450

// Vertex attributes (same layout as sphere.vert)
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec3 inColor;
layout(location = 3) in vec2 inUV;

layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 proj;
} camera;

layout(push_constant) uniform PushConstants {
    mat4 model;
    vec4 color;  // RGB + alpha
} push;

layout(location = 0) out vec2 fragUV;
layout(location = 1) out vec3 fragColor;
layout(location = 2) out float fragAlpha;

void main() {
    vec4 worldPos = push.model * vec4(inPosition, 1.0);

    gl_Position = camera.proj * camera.view * worldPos;

    // Pass UV for edge softening (U=0 at inner, U=1 at outer)
    fragUV = inUV;

    fragColor = push.color.rgb;
    fragAlpha = push.color.a;
}
