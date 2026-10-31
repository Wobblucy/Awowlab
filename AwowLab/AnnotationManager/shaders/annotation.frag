#version 450

layout(location = 0) in vec3 fragNormal;
layout(location = 1) in vec4 fragColor;  // Full RGBA with alpha
layout(location = 2) in vec3 fragWorldPos;
layout(location = 3) in vec2 fragUV;     // UV for edge effects (uv.x = edge distance, uv.y = radial/inner marker)

// Must match annotation.vert
layout(push_constant) uniform PushConstants {
    mat4 model;               // 64 bytes
    vec4 color;               // 16 bytes
    float elapsedTime_s;      // 4 bytes  - Time since encounter start
    uint visualStyle;         // 4 bytes  - 0=solid, 1=striped, 2=hatched, 3=ripple_rings, 4=swirl
    float centerX;            // 4 bytes  - Annotation center X
    float centerY;            // 4 bytes  - Annotation center Y
    // Edge effect parameters (16 bytes)
    uint edgeFlags;           // 4 bytes  - bit0=fade, bit1=glow, bit2=outline, bit3=gradient, bit4=useSecondary, bit5=useInnerZone
    float edgeFadeWidth;      // 4 bytes  - Normalized width of edge fade (0.0-0.3)
    float outlineWidth;       // 4 bytes  - Normalized width of outline (0.0-0.15)
    float glowIntensity;      // 4 bytes  - Glow strength multiplier (0.0-2.0)
    // Dual color support (8 bytes)
    uint secondaryColorPacked;  // 4 bytes - RGBA8 packed for gradients/outlines/patterns
    uint innerZoneColorPacked;  // 4 bytes - RGBA8 packed for center region (safe zones)
    // Packed parameters (8 bytes); 128 bytes in total
    uint packedRipple;        // 4 bytes - upper16: rippleSpeed*100, lower16: rippleCount*100
    uint packedPattern;       // 4 bytes - upper16: stripeSpacing*100, lower16: innerRadiusRatio*65535
} push;

bool hasEdgeFade()       { return (push.edgeFlags & 0x1u) != 0u; }
bool hasGlow()           { return (push.edgeFlags & 0x2u) != 0u; }
bool hasOutline()        { return (push.edgeFlags & 0x4u) != 0u; }
bool hasGradient()       { return (push.edgeFlags & 0x8u) != 0u; }
bool useSecondaryColor() { return (push.edgeFlags & 0x10u) != 0u; }
bool useInnerZoneColor() { return (push.edgeFlags & 0x20u) != 0u; }

// Unpack RGBA8 color from uint32
vec4 unpackColor(uint packed) {
    return vec4(
        float((packed >> 0) & 0xFFu) / 255.0,
        float((packed >> 8) & 0xFFu) / 255.0,
        float((packed >> 16) & 0xFFu) / 255.0,
        float((packed >> 24) & 0xFFu) / 255.0
    );
}

// Unpack ripple/pattern parameters from packed uint32s
float unpackRippleSpeed()    { return float(push.packedRipple >> 16) / 100.0; }
float unpackRippleCount()    { return float(push.packedRipple & 0xFFFFu) / 100.0; }
float unpackStripeSpacing()  { return float(push.packedPattern >> 16) / 100.0; }
float unpackInnerRadiusRatio() { return float(push.packedPattern & 0xFFFFu) / 65535.0; }

// Derive secondary colors from primary
vec3 deriveOutlineColor(vec3 baseColor) {
    // Darken by 30% for outline
    return baseColor * 0.7;
}

vec3 deriveGradientEndColor(vec3 baseColor) {
    // Shift toward white by 20%
    return mix(baseColor, vec3(1.0), 0.2);
}

layout(location = 0) out vec4 outColor;

const uint STYLE_SOLID = 0u;
const uint STYLE_STRIPED = 1u;
const uint STYLE_HATCHED = 2u;
const uint STYLE_RIPPLE_RINGS = 3u;
const uint STYLE_SWIRL = 4u;

// Striped: diagonal lines
// Returns 1.0 for visible stripes, 0.0 for transparent gaps
float stripePattern(vec2 worldXY, float spacing) {
    // Diagonal stripes at 45 degrees
    float diag = worldXY.x + worldXY.y;
    float stripePhase = mod(diag, spacing) / spacing;
    // Visible stripe in first 40% of period, transparent gap in remaining 60%
    return 1.0 - step(0.4, stripePhase);
}

// Hatched: two crossing diagonals
// Returns 1.0 for visible hatches, 0.0 for transparent gaps
float hatchPattern(vec2 worldXY, float spacing) {
    // First diagonal (x + y)
    float diag1 = worldXY.x + worldXY.y;
    float stripe1 = mod(diag1, spacing) / spacing;
    // Visible in first 35% of period
    float hatch1 = 1.0 - step(0.35, stripe1);

    // Second diagonal (x - y)
    float diag2 = worldXY.x - worldXY.y;
    float stripe2 = mod(diag2, spacing) / spacing;
    float hatch2 = 1.0 - step(0.35, stripe2);

    // Combine: visible where either stripe is visible
    return max(hatch1, hatch2);
}

// Ripple rings: concentric rings moving outward
// Returns 1.0 for visible rings, 0.0 for transparent gaps
float ripplePattern(vec2 worldXY, vec2 center, float spacing, float speed, float ringCount, float time) {
    float dist = length(worldXY - center);

    // Phase grows with distance and falls with time, so the rings move outward
    float ripplePhase = dist - time * speed;

    // Higher ringCount packs more rings into the same distance
    float ringPeriod = spacing / ringCount;
    float phase = mod(ripplePhase, ringPeriod) / ringPeriod;

    // Sharp rings: 40% visible, 60% transparent gap (same as stripe/hatch)
    return 1.0 - step(0.4, phase);
}

// Swirl: Archimedean spiral stripes from the centre
// Returns alpha: 1.0 for spiral arms, 0.1 for gaps (nearly transparent)
float swirlPattern(vec2 worldXY, vec2 center, float spacing) {
    vec2 offset = worldXY - center;
    float r = length(offset);
    float theta = atan(offset.y, offset.x);  // -PI to PI

    // Archimedean spiral: arms appear where theta - k*r is constant
    // k controls how tightly wound the spiral is
    float k = 6.2832 / spacing;  // 2*PI / spacing = one full turn per 'spacing' yards
    float spiralPhase = theta - k * r;

    float phase = fract(spiralPhase / 6.2832);  // 2*PI

    // Spiral arm = full alpha (1.0), gap = low alpha (0.1)
    return mix(1.0, 0.1, step(0.4, phase));
}

void main() {
    // Directional light, same as sphere.frag
    vec3 lightDir = normalize(vec3(0.5, -1.0, 0.3));
    vec3 normal = normalize(fragNormal);

    // Ambient is a little higher than for actors so annotations stay visible
    float ambient = 0.2;

    float diffuse = max(dot(normal, -lightDir), 0.0);

    float lighting = ambient + diffuse * 0.8;

    vec4 secondaryColor = unpackColor(push.secondaryColorPacked);
    vec4 innerZoneColor = unpackColor(push.innerZoneColorPacked);

    vec3 baseColor = fragColor.rgb;

    // Secondary colour on its own (no gradient or outline): blend centre to edge
    if (useSecondaryColor() && !hasGradient()) {
        baseColor = mix(baseColor, secondaryColor.rgb, fragUV.y * 0.5);
    }

    // Inner zone colour for the centre region
    // fragUV.y encodes radial position: 0.0 at center, 1.0 at outer edge
    if (useInnerZoneColor()) {
        float innerThreshold = 0.5;  // Inner 50% of shape
        if (fragUV.y < innerThreshold) {
            // Smooth transition from inner zone to primary color
            float t = fragUV.y / innerThreshold;  // 0 at center, 1 at threshold
            baseColor = mix(innerZoneColor.rgb, baseColor, t);
        }
    }

    // Radial gradient along uv.y
    if (hasGradient()) {
        // Use secondary color if enabled, otherwise derive from primary
        vec3 gradientEnd = useSecondaryColor()
            ? secondaryColor.rgb
            : deriveGradientEndColor(push.color.rgb);
        vec3 gradientStart = useInnerZoneColor() ? baseColor : push.color.rgb;
        baseColor = mix(gradientStart, gradientEnd, fragUV.y);
    }

    vec3 finalColor = baseColor * lighting;

    float patternAlpha = 1.0;
    vec2 worldXY = fragWorldPos.xy;  // Ground plane coordinates
    vec2 center = vec2(push.centerX, push.centerY);

    float stripeSpacing = unpackStripeSpacing();
    float rippleSpeed = unpackRippleSpeed();
    float rippleCount = unpackRippleCount();

    if (push.visualStyle == STYLE_STRIPED) {
        patternAlpha = stripePattern(worldXY, stripeSpacing);
    }
    else if (push.visualStyle == STYLE_HATCHED) {
        patternAlpha = hatchPattern(worldXY, stripeSpacing);
    }
    else if (push.visualStyle == STYLE_RIPPLE_RINGS) {
        // Ripple rings: hatched + animated expanding rings
        patternAlpha = ripplePattern(worldXY, center, stripeSpacing,
                                     rippleSpeed, rippleCount, push.elapsedTime_s);
    }
    else if (push.visualStyle == STYLE_SWIRL) {
        // Swirl pattern: spiral arms full alpha, gaps nearly transparent
        patternAlpha = swirlPattern(worldXY, center, stripeSpacing);
    }
    // STYLE_SOLID leaves patternAlpha at 1.0

    // Edge fade: soft alpha falloff at the edges
    float edgeFade = 1.0;
    if (hasEdgeFade()) {
        edgeFade = smoothstep(0.0, push.edgeFadeWidth, fragUV.x);
    }

    float finalAlpha = fragColor.a * patternAlpha * edgeFade;

    // Outline: overrides the colour in a narrow band at the edge
    if (hasOutline() && fragUV.x < push.outlineWidth) {
        // Use secondary color for outline if enabled, otherwise derive from primary
        vec3 outlineColor = useSecondaryColor()
            ? secondaryColor.rgb
            : deriveOutlineColor(push.color.rgb);
        finalColor = outlineColor * lighting;
        // Outline ignores pattern alpha but respects edge fade
        finalAlpha = fragColor.a * edgeFade;
    }

    // Glow: additive brightness at the edges
    if (hasGlow()) {
        float glowWidth = push.edgeFadeWidth * 2.0;
        float glowFactor = 1.0 - smoothstep(0.0, glowWidth, fragUV.x);
        finalColor += push.color.rgb * glowFactor * push.glowIntensity * 0.5;
    }

    // Skip fully transparent fragments
    if (finalAlpha < 0.01) {
        discard;
    }

    // Alpha combines the push constant colour, the pattern and the edge effects
    outColor = vec4(finalColor, finalAlpha);
}
