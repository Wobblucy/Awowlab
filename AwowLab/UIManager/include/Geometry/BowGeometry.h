#pragma once
#include "SphereGeometry.h"
#include "GeometryHelpers.h"
#include <vector>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Bow geometry for Ranged DPS role - recurve bow with arrow
// Oriented to stand upright (Z-up), arrow pointing right (+X)
class BowGeometry {
public:
    // size: overall height of the bow
    // curveSegments: number of segments for the curve (more = smoother)
    static void generateBow(float size, uint32_t curveSegments, std::vector<Vertex3D>& vertices, std::vector<uint32_t>& indices) {
        vertices.clear();
        indices.clear();

        if (curveSegments < 8) curveSegments = 8;

        float bowHeight = size * 0.95f;
        float halfHeight = bowHeight * 0.5f;
        float limbThickness = size * 0.045f;
        float limbDepth = size * 0.055f;
        float halfDepth = limbDepth * 0.5f;

        // Tilt the bow about 20 degrees (positive = clockwise)
        const float tiltAngle = 0.35f;  // radians (~20 degrees)
        const float cosA = std::cos(tiltAngle);
        const float sinA = std::sin(tiltAngle);

        auto rotate = [cosA, sinA](float x, float z) -> std::pair<float, float> {
            return { x * cosA - z * sinA, x * sinA + z * cosA };
        };

        // Recurve bow profile - main curve bends back, tips curve forward
        auto addQuad = [&](float x0, float z0, float y0,
                          float x1, float z1, float y1,
                          float x2, float z2, float y2,
                          float x3, float z3, float y3,
                          float nx, float nz, float ny) {
            geometry::addQuad(vertices, indices, x0, z0, y0, x1, z1, y1, x2, z2, y2, x3, z3, y3, nx, nz, ny);
        };

        // The bow curves back (negative X) in the middle, but tips curve forward (positive X)
        std::vector<std::pair<float, float>> outerCurve;  // (x, z) pairs
        std::vector<std::pair<float, float>> innerCurve;

        for (uint32_t i = 0; i <= curveSegments; ++i) {
            float t = static_cast<float>(i) / static_cast<float>(curveSegments);
            float z = -halfHeight + t * bowHeight;  // Bottom to top

            // Normalized position from center (-1 at bottom, 0 at center, 1 at top)
            float normZ = z / halfHeight;
            float absNormZ = std::abs(normZ);

            // Main curve - deeper in the middle, less at tips
            // Bow curves away from the arrow (+X); the string side is -X
            float mainCurve;
            if (absNormZ < 0.7f) {
                // Main body curves forward (positive X = away from string)
                mainCurve = size * 0.22f * (1.0f - absNormZ * absNormZ * 1.5f);
            } else {
                // Tips curve back slightly - the "recurve"
                float tipT = (absNormZ - 0.7f) / 0.3f;
                float baseCurve = size * 0.22f * (1.0f - 0.7f * 0.7f * 1.5f);
                mainCurve = baseCurve - tipT * tipT * size * 0.08f;
            }

            // Limb gets thinner towards the tips
            float widthScale = 1.0f - absNormZ * 0.5f;
            float currentThickness = limbThickness * widthScale;

            float outerX = mainCurve - currentThickness * 0.5f;
            float innerX = mainCurve + currentThickness * 0.5f;

            auto [rotOuterX, rotOuterZ] = rotate(outerX, z);
            auto [rotInnerX, rotInnerZ] = rotate(innerX, z);

            outerCurve.push_back({rotOuterX, rotOuterZ});
            innerCurve.push_back({rotInnerX, rotInnerZ});
        }

        for (uint32_t i = 0; i < curveSegments; ++i) {
            float ox0 = outerCurve[i].first;
            float oz0 = outerCurve[i].second;
            float ox1 = outerCurve[i + 1].first;
            float oz1 = outerCurve[i + 1].second;

            float ix0 = innerCurve[i].first;
            float iz0 = innerCurve[i].second;
            float ix1 = innerCurve[i + 1].first;
            float iz1 = innerCurve[i + 1].second;

            float dx = ox1 - ox0;
            float dz = oz1 - oz0;
            float len = std::sqrt(dx * dx + dz * dz);
            if (len < 0.0001f) len = 1.0f;

            float outerNx = -dz / len;  // Perpendicular to curve, pointing outward
            float outerNz = dx / len;

            float innerNx = dz / len;   // Opposite direction for inner
            float innerNz = -dx / len;

            // Front face (+Y)
            addQuad(ox0, oz0, halfDepth,
                    ox1, oz1, halfDepth,
                    ix1, iz1, halfDepth,
                    ix0, iz0, halfDepth,
                    0, 0, 1);

            // Back face (-Y)
            addQuad(ox0, oz0, -halfDepth,
                    ix0, iz0, -halfDepth,
                    ix1, iz1, -halfDepth,
                    ox1, oz1, -halfDepth,
                    0, 0, -1);

            // Outer edge (back of bow)
            addQuad(ox0, oz0, halfDepth,
                    ox0, oz0, -halfDepth,
                    ox1, oz1, -halfDepth,
                    ox1, oz1, halfDepth,
                    outerNx, outerNz, 0);

            // Inner edge (belly of bow, facing string)
            addQuad(ix1, iz1, halfDepth,
                    ix1, iz1, -halfDepth,
                    ix0, iz0, -halfDepth,
                    ix0, iz0, halfDepth,
                    innerNx, innerNz, 0);
        }

        // Bottom tip
        {
            float ox = outerCurve[0].first;
            float oz = outerCurve[0].second;
            float ix = innerCurve[0].first;
            float iz = innerCurve[0].second;

            addQuad(ox, oz, halfDepth,
                    ix, iz, halfDepth,
                    ix, iz, -halfDepth,
                    ox, oz, -halfDepth,
                    0, -1, 0);
        }

        // Top tip
        {
            float ox = outerCurve[curveSegments].first;
            float oz = outerCurve[curveSegments].second;
            float ix = innerCurve[curveSegments].first;
            float iz = innerCurve[curveSegments].second;

            addQuad(ix, iz, halfDepth,
                    ox, oz, halfDepth,
                    ox, oz, -halfDepth,
                    ix, iz, -halfDepth,
                    0, 1, 0);
        }

        // Bowstring - connects the tips on the inner side
        float stringRadius = size * 0.012f;
        float topZ = innerCurve[curveSegments].second;
        float bottomZ = innerCurve[0].second;
        float topX = innerCurve[curveSegments].first;
        float bottomX = innerCurve[0].first;

        // String as a thin box
        addQuad(topX - stringRadius, topZ, stringRadius,
                bottomX - stringRadius, bottomZ, stringRadius,
                bottomX + stringRadius, bottomZ, stringRadius,
                topX + stringRadius, topZ, stringRadius,
                0, 0, 1);

        addQuad(topX + stringRadius, topZ, -stringRadius,
                bottomX + stringRadius, bottomZ, -stringRadius,
                bottomX - stringRadius, bottomZ, -stringRadius,
                topX - stringRadius, topZ, -stringRadius,
                0, 0, -1);

        // Arrow
        float arrowLength = size * 0.5f;
        float shaftRadius = size * 0.018f;
        float arrowheadLength = size * 0.10f;
        float arrowheadWidth = size * 0.05f;
        float fletchingLength = size * 0.12f;
        float fletchingHeight = size * 0.04f;

        // Arrow positioned at string, also rotated with the bow
        float localArrowStartX = -size * 0.08f;  // Start behind string
        float localArrowZ = 0.0f;
        float localArrowEndX = localArrowStartX + arrowLength;

        auto [arrowStartX, arrowStartZ] = rotate(localArrowStartX, localArrowZ);
        auto [arrowEndX, arrowEndZ] = rotate(localArrowEndX, localArrowZ);
        auto [arrowMidX, arrowMidZ] = rotate(localArrowEndX - arrowheadLength, localArrowZ);

        float arrowDirX = arrowEndX - arrowStartX;
        float arrowDirZ = arrowEndZ - arrowStartZ;
        float arrowLen = std::sqrt(arrowDirX * arrowDirX + arrowDirZ * arrowDirZ);
        arrowDirX /= arrowLen;
        arrowDirZ /= arrowLen;

        // Perpendicular vector for arrow thickness
        float arrowPerpX = -arrowDirZ;
        float arrowPerpZ = arrowDirX;

        // Arrow shaft as a simple box aligned with arrow direction
        // Four corners at start
        float s0x = arrowStartX + arrowPerpX * shaftRadius;
        float s0z = arrowStartZ + arrowPerpZ * shaftRadius;
        float s1x = arrowStartX - arrowPerpX * shaftRadius;
        float s1z = arrowStartZ - arrowPerpZ * shaftRadius;

        // Four corners at end (before arrowhead)
        float e0x = arrowMidX + arrowPerpX * shaftRadius;
        float e0z = arrowMidZ + arrowPerpZ * shaftRadius;
        float e1x = arrowMidX - arrowPerpX * shaftRadius;
        float e1z = arrowMidZ - arrowPerpZ * shaftRadius;

        // Shaft top face (+Y side)
        addQuad(s0x, s0z, shaftRadius,
                e0x, e0z, shaftRadius,
                e1x, e1z, shaftRadius,
                s1x, s1z, shaftRadius,
                0, 0, 1);

        // Shaft bottom face (-Y side)
        addQuad(s1x, s1z, -shaftRadius,
                e1x, e1z, -shaftRadius,
                e0x, e0z, -shaftRadius,
                s0x, s0z, -shaftRadius,
                0, 0, -1);

        // Shaft outer edge
        addQuad(s0x, s0z, shaftRadius,
                s0x, s0z, -shaftRadius,
                e0x, e0z, -shaftRadius,
                e0x, e0z, shaftRadius,
                arrowPerpX, arrowPerpZ, 0);

        // Shaft inner edge
        addQuad(e1x, e1z, shaftRadius,
                e1x, e1z, -shaftRadius,
                s1x, s1z, -shaftRadius,
                s1x, s1z, shaftRadius,
                -arrowPerpX, -arrowPerpZ, 0);

        // Arrowhead (triangular point)
        float headBaseX0 = arrowMidX + arrowPerpX * arrowheadWidth;
        float headBaseZ0 = arrowMidZ + arrowPerpZ * arrowheadWidth;
        float headBaseX1 = arrowMidX - arrowPerpX * arrowheadWidth;
        float headBaseZ1 = arrowMidZ - arrowPerpZ * arrowheadWidth;

        // Arrowhead - top face
        addQuad(headBaseX1, headBaseZ1, 0,
                headBaseX0, headBaseZ0, 0,
                arrowEndX, arrowEndZ, 0,
                arrowEndX, arrowEndZ, 0,
                0, 0, 1);

        // Arrowhead - bottom face
        addQuad(headBaseX0, headBaseZ0, 0,
                headBaseX1, headBaseZ1, 0,
                arrowEndX, arrowEndZ, 0,
                arrowEndX, arrowEndZ, 0,
                0, 0, -1);

        // Fletching - simplified, just one visible fin on top
        auto [fletchEndX, fletchEndZ] = rotate(localArrowStartX + fletchingLength, localArrowZ);
        auto [fletchTopX, fletchTopZ] = rotate(localArrowStartX + fletchingLength * 0.5f, fletchingHeight);

        addQuad(arrowStartX, arrowStartZ, shaftRadius,
                fletchEndX, fletchEndZ, shaftRadius,
                fletchTopX, fletchTopZ, shaftRadius * 0.3f,
                arrowStartX, arrowStartZ, shaftRadius,
                0, 0, 1);

        addQuad(arrowStartX, arrowStartZ, -shaftRadius,
                arrowStartX, arrowStartZ, -shaftRadius,
                fletchTopX, fletchTopZ, -shaftRadius * 0.3f,
                fletchEndX, fletchEndZ, -shaftRadius,
                0, 0, -1);
    }
};
