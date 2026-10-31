#version 450

layout(location = 0) in vec2 fragUV;
layout(location = 1) in vec2 fragWorldPos;

layout(push_constant) uniform PushConstants {
    mat4 model;
    vec4 color;  // w: 1.0 = texture, 0.5 = grid, 0.0 = solid color
} push;

layout(set = 0, binding = 1) uniform sampler2D floorTexture;

layout(location = 0) out vec4 outColor;

void main() {
    if (push.color.w > 0.75) {
        // Texture mode (w = 1.0)
        outColor = texture(floorTexture, fragUV);
    } else if (push.color.w > 0.25) {
        // Grid mode (w = 0.5) - subtle gridlines every 10 units
        vec2 grid = abs(fract(fragWorldPos / 10.0 - 0.5) - 0.5);
        float line = min(grid.x, grid.y);
        float gridAlpha = 1.0 - smoothstep(0.0, 0.02, line);

        // Subtle colors: dark background with faint lines
        vec3 bgColor = vec3(0.05, 0.05, 0.08);
        vec3 lineColor = vec3(0.15, 0.15, 0.2);
        outColor = vec4(mix(bgColor, lineColor, gridAlpha * 0.5), 1.0);
    } else {
        // Solid color mode (w = 0.0)
        outColor = vec4(push.color.rgb, 1.0);
    }
}
