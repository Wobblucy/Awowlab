#version 450

layout(location = 0) in vec3 fragNormal;
layout(location = 1) in vec3 fragColor;
layout(location = 2) in vec3 fragWorldPos;
layout(location = 3) in float fragAlpha;

layout(location = 0) out vec4 outColor;

void main() {
    vec3 normal = normalize(fragNormal);

    // Camera/view direction for specular calculation
    vec3 viewDir = normalize(-fragWorldPos);

    // Key light (main directional)
    vec3 keyLightDir = normalize(vec3(0.4, -0.8, 0.4));
    vec3 keyLightColor = vec3(1.0, 0.98, 0.95);  // Slightly warm
    float keyDiffuse = max(dot(normal, -keyLightDir), 0.0);

    // Blinn-Phong specular for key light
    vec3 halfwayKey = normalize(-keyLightDir + viewDir);
    float keySpecular = pow(max(dot(normal, halfwayKey), 0.0), 32.0) * 0.3;

    // Fill light (softer, from the opposite side)
    vec3 fillLightDir = normalize(vec3(-0.6, -0.3, -0.5));
    vec3 fillLightColor = vec3(0.6, 0.7, 0.9);  // Cool blue tint
    float fillDiffuse = max(dot(normal, -fillLightDir), 0.0) * 0.4;

    // Rim light, for edge definition
    float rimDot = 1.0 - max(dot(viewDir, normal), 0.0);
    float rim = pow(rimDot, 3.0) * 0.25;
    vec3 rimColor = vec3(0.9, 0.95, 1.0);

    float ambient = 0.12;

    vec3 lighting = vec3(ambient);
    lighting += keyLightColor * (keyDiffuse * 0.7 + keySpecular);
    lighting += fillLightColor * fillDiffuse;
    lighting += rimColor * rim;

    vec3 finalColor = fragColor * lighting;

    // Per-actor opacity (Play Along draws friendly players translucent)
    outColor = vec4(finalColor, fragAlpha);
}
