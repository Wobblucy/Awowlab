#version 450

layout(location = 0) in vec2 fragUV;
layout(location = 1) in vec3 fragColor;
layout(location = 2) in float fragAlpha;

layout(location = 0) out vec4 outColor;

void main() {
    // UV.x represents radial position: 0 = inner edge, 1 = outer edge
    // Create soft edges that fade at both inner and outer boundaries
    float innerFade = smoothstep(0.0, 0.3, fragUV.x);   // Fade in from inner edge
    float outerFade = smoothstep(1.0, 0.7, fragUV.x);   // Fade out at outer edge
    float edgeFade = innerFade * outerFade;

    // Brighter toward the middle of the ring
    float centerBoost = 1.0 + 0.3 * sin(fragUV.x * 3.14159);

    float finalAlpha = fragAlpha * edgeFade * centerBoost;

    // Slight brightness boost for visibility
    vec3 finalColor = fragColor * 1.2;

    outColor = vec4(finalColor, finalAlpha);
}
