#version 450

// Same vertex layout as sphere.vert
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec3 inColor;
layout(location = 3) in vec2 inUV;

// Camera UBO, same binding as sphere.vert
layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 proj;
} camera;

// 128 bytes, the minimum push constant size Vulkan guarantees
layout(push_constant) uniform PushConstants {
    mat4 model;               // 64 bytes
    vec4 color;               // 16 bytes - RGBA with alpha for transparency
    float elapsedTime_s;      // 4 bytes  - Time since encounter start (for animation)
    uint visualStyle;         // 4 bytes  - 0=solid, 1=striped, 2=hatched, 3=ripple_rings, 4=swirl
    float centerX;            // 4 bytes  - Annotation center X position
    float centerY;            // 4 bytes  - Annotation center Y position
    // Edge effect parameters (16 bytes)
    uint edgeFlags;           // 4 bytes  - bit0=fade, bit1=glow, bit2=outline, bit3=gradient, bit4=useSecondary, bit5=useInnerZone
    float edgeFadeWidth;      // 4 bytes  - Normalized fade width
    float outlineWidth;       // 4 bytes  - Normalized outline width
    float glowIntensity;      // 4 bytes  - Glow strength multiplier
    // Dual color support (8 bytes)
    uint secondaryColorPacked;  // 4 bytes - RGBA8 packed for gradients/outlines/patterns
    uint innerZoneColorPacked;  // 4 bytes - RGBA8 packed for center region (safe zones)
    // Packed parameters (8 bytes) - 4 floats packed into 2 uint32s
    uint packedRipple;        // 4 bytes - upper16: rippleSpeed*100, lower16: rippleCount*100
    uint packedPattern;       // 4 bytes - upper16: stripeSpacing*100, lower16: innerRadiusRatio*65535
} push;

layout(location = 0) out vec3 fragNormal;
layout(location = 1) out vec4 fragColor;  // Pass full RGBA
layout(location = 2) out vec3 fragWorldPos;
layout(location = 3) out vec2 fragUV;     // UV for edge effects

void main() {
    vec3 pos = inPosition;

    float innerRadiusRatio = float(push.packedPattern & 0xFFFFu) / 65535.0;

    // Donut: inner vertices (uv.y = 1) are built at radius 0.5 and moved
    // to the requested inner radius here
    if (innerRadiusRatio > 0.0 && inUV.y > 0.5) {
        float scaleFactor = innerRadiusRatio / 0.5;
        pos.x *= scaleFactor;
        pos.z *= scaleFactor;
    }

    vec4 worldPos = push.model * vec4(pos, 1.0);
    fragWorldPos = worldPos.xyz;

    gl_Position = camera.proj * camera.view * worldPos;

    // Assumes uniform scaling
    fragNormal = normalize(mat3(push.model) * inNormal);

    // Vertex colour tints the push constant colour (used for the skull's eyes)
    fragColor = vec4(inColor * push.color.rgb, push.color.a);

    fragUV = inUV;
}
