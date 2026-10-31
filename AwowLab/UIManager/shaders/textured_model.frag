#version 450

layout(location = 0) in vec3 fragNormal;
layout(location = 1) in vec2 fragUV;
layout(location = 2) in vec3 fragWorldPos;

layout(set = 0, binding = 1) uniform sampler2D modelTexture;

layout(location = 0) out vec4 outColor;

void main() {
    // Sample texture directly - pure texture colors, no blending with actor color
    vec4 texColor = texture(modelTexture, fragUV);

    // Apply subtle lighting for depth perception
    vec3 normal = normalize(fragNormal);
    vec3 viewDir = normalize(-fragWorldPos);

    // Key light (simple directional)
    vec3 lightDir = normalize(vec3(0.4, -0.8, 0.4));
    float diffuse = max(dot(normal, -lightDir), 0.0) * 0.3 + 0.7;  // Range: 0.7-1.0

    vec3 finalColor = texColor.rgb * diffuse;
    outColor = vec4(finalColor, texColor.a);
}
