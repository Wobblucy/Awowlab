#version 450

layout(location = 0) in vec3 fragNormal;
layout(location = 1) in vec3 fragColor;
layout(location = 2) in vec2 fragUV;
layout(location = 3) in float fragUseTexture;
layout(location = 4) in vec3 fragWorldPos;

layout(set = 0, binding = 1) uniform sampler2D specTexture;

layout(location = 0) out vec4 outColor;

void main() {
    vec3 finalColor;

    // Detect front face vs edge/back by UV coordinates
    // Front face has UVs around (0.5, 0.5), edge/back has UVs at (0, 0)
    bool isFrontFace = (fragUV.x > 0.01 || fragUV.y > 0.01);

    if (isFrontFace && fragUseTexture > 0.5) {
        // Front face: sample spec icon texture directly
        // No lighting - show icon as-is for clarity
        vec4 texColor = texture(specTexture, fragUV);
        finalColor = texColor.rgb;
    } else {
        // Back face and edge: solid color with simple lighting
        vec3 normal = normalize(fragNormal);
        vec3 viewDir = normalize(-fragWorldPos);

        // Key light
        vec3 keyLightDir = normalize(vec3(0.4, -0.8, 0.4));
        float keyDiffuse = max(dot(normal, -keyLightDir), 0.0);

        // Blinn-Phong specular
        vec3 halfwayKey = normalize(-keyLightDir + viewDir);
        float keySpecular = pow(max(dot(normal, halfwayKey), 0.0), 32.0) * 0.2;

        // Fill light
        vec3 fillLightDir = normalize(vec3(-0.6, -0.3, -0.5));
        float fillDiffuse = max(dot(normal, -fillLightDir), 0.0) * 0.3;

        float ambient = 0.15;

        float lighting = ambient + keyDiffuse * 0.6 + keySpecular + fillDiffuse;

        finalColor = fragColor * lighting;
    }

    outColor = vec4(finalColor, 1.0);
}
