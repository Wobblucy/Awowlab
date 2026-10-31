#pragma once
#include "SphereGeometry.h"
#include "GeometryHelpers.h"
#include <vector>

// Sword geometry for Melee DPS role - simple blade silhouette
// Oriented to stand upright (Z-up)
class SwordGeometry {
public:
    // Generate a simplified sword shape (blade pointing up)
    // size: overall length of the sword
    static void generateSword(float size, std::vector<Vertex3D>& vertices, std::vector<uint32_t>& indices) {
        vertices.clear();
        indices.clear();

        float bladeLength = size * 0.65f;
        float bladeWidth = size * 0.12f;
        float handleLength = size * 0.25f;
        float handleWidth = size * 0.06f;
        float guardWidth = size * 0.25f;
        float guardHeight = size * 0.04f;
        float depth = size * 0.08f;
        float halfDepth = depth * 0.5f;

        // Z positions (vertical, Z-up)
        float tipZ = size * 0.5f;
        float bladeTopZ = tipZ - bladeLength * 0.1f;  // Start of full-width blade
        float guardTopZ = tipZ - bladeLength;
        float guardBottomZ = guardTopZ - guardHeight;
        float handleBottomZ = guardBottomZ - handleLength;

        auto addQuad = [&](float x0, float z0, float y0,
                          float x1, float z1, float y1,
                          float x2, float z2, float y2,
                          float x3, float z3, float y3,
                          float nx, float nz, float ny) {
            geometry::addQuad(vertices, indices, x0, z0, y0, x1, z1, y1, x2, z2, y2, x3, z3, y3, nx, nz, ny);
        };

        auto addTri = [&](float x0, float z0, float y0,
                         float x1, float z1, float y1,
                         float x2, float z2, float y2,
                         float nx, float nz, float ny) {
            geometry::addTri(vertices, indices, x0, z0, y0, x1, z1, y1, x2, z2, y2, nx, nz, ny);
        };

        float halfBlade = bladeWidth * 0.5f;
        float halfHandle = handleWidth * 0.5f;
        float halfGuard = guardWidth * 0.5f;

        // Front face (+Y)

        // Blade tip (triangle)
        addTri(0.0f, tipZ, halfDepth,
               halfBlade, bladeTopZ, halfDepth,
               -halfBlade, bladeTopZ, halfDepth,
               0, 0, 1);

        // Blade body (quad)
        addQuad(-halfBlade, bladeTopZ, halfDepth,
                halfBlade, bladeTopZ, halfDepth,
                halfBlade, guardTopZ, halfDepth,
                -halfBlade, guardTopZ, halfDepth,
                0, 0, 1);

        // Guard (quad)
        addQuad(-halfGuard, guardTopZ, halfDepth,
                halfGuard, guardTopZ, halfDepth,
                halfGuard, guardBottomZ, halfDepth,
                -halfGuard, guardBottomZ, halfDepth,
                0, 0, 1);

        // Handle (quad)
        addQuad(-halfHandle, guardBottomZ, halfDepth,
                halfHandle, guardBottomZ, halfDepth,
                halfHandle, handleBottomZ, halfDepth,
                -halfHandle, handleBottomZ, halfDepth,
                0, 0, 1);

        // Back face (-Y)

        // Blade tip (triangle, reversed)
        addTri(0.0f, tipZ, -halfDepth,
               -halfBlade, bladeTopZ, -halfDepth,
               halfBlade, bladeTopZ, -halfDepth,
               0, 0, -1);

        // Blade body (quad, reversed)
        addQuad(-halfBlade, guardTopZ, -halfDepth,
                halfBlade, guardTopZ, -halfDepth,
                halfBlade, bladeTopZ, -halfDepth,
                -halfBlade, bladeTopZ, -halfDepth,
                0, 0, -1);

        // Guard (quad, reversed)
        addQuad(-halfGuard, guardBottomZ, -halfDepth,
                halfGuard, guardBottomZ, -halfDepth,
                halfGuard, guardTopZ, -halfDepth,
                -halfGuard, guardTopZ, -halfDepth,
                0, 0, -1);

        // Handle (quad, reversed)
        addQuad(-halfHandle, handleBottomZ, -halfDepth,
                halfHandle, handleBottomZ, -halfDepth,
                halfHandle, guardBottomZ, -halfDepth,
                -halfHandle, guardBottomZ, -halfDepth,
                0, 0, -1);

        // Side faces

        // Blade tip - left edge
        addQuad(-halfBlade, bladeTopZ, halfDepth,
                0.0f, tipZ, halfDepth,
                0.0f, tipZ, -halfDepth,
                -halfBlade, bladeTopZ, -halfDepth,
                -0.8f, 0.6f, 0);

        // Blade tip - right edge
        addQuad(0.0f, tipZ, halfDepth,
                halfBlade, bladeTopZ, halfDepth,
                halfBlade, bladeTopZ, -halfDepth,
                0.0f, tipZ, -halfDepth,
                0.8f, 0.6f, 0);

        // Blade body - left edge
        addQuad(-halfBlade, guardTopZ, halfDepth,
                -halfBlade, bladeTopZ, halfDepth,
                -halfBlade, bladeTopZ, -halfDepth,
                -halfBlade, guardTopZ, -halfDepth,
                -1, 0, 0);

        // Blade body - right edge
        addQuad(halfBlade, bladeTopZ, halfDepth,
                halfBlade, guardTopZ, halfDepth,
                halfBlade, guardTopZ, -halfDepth,
                halfBlade, bladeTopZ, -halfDepth,
                1, 0, 0);

        // Guard - top edge
        addQuad(-halfGuard, guardTopZ, halfDepth,
                halfGuard, guardTopZ, halfDepth,
                halfGuard, guardTopZ, -halfDepth,
                -halfGuard, guardTopZ, -halfDepth,
                0, 1, 0);

        // Guard - bottom edge
        addQuad(-halfGuard, guardBottomZ, -halfDepth,
                halfGuard, guardBottomZ, -halfDepth,
                halfGuard, guardBottomZ, halfDepth,
                -halfGuard, guardBottomZ, halfDepth,
                0, -1, 0);

        // Guard - left edge
        addQuad(-halfGuard, guardBottomZ, halfDepth,
                -halfGuard, guardTopZ, halfDepth,
                -halfGuard, guardTopZ, -halfDepth,
                -halfGuard, guardBottomZ, -halfDepth,
                -1, 0, 0);

        // Guard - right edge
        addQuad(halfGuard, guardTopZ, halfDepth,
                halfGuard, guardBottomZ, halfDepth,
                halfGuard, guardBottomZ, -halfDepth,
                halfGuard, guardTopZ, -halfDepth,
                1, 0, 0);

        // Handle - left edge
        addQuad(-halfHandle, handleBottomZ, halfDepth,
                -halfHandle, guardBottomZ, halfDepth,
                -halfHandle, guardBottomZ, -halfDepth,
                -halfHandle, handleBottomZ, -halfDepth,
                -1, 0, 0);

        // Handle - right edge
        addQuad(halfHandle, guardBottomZ, halfDepth,
                halfHandle, handleBottomZ, halfDepth,
                halfHandle, handleBottomZ, -halfDepth,
                halfHandle, guardBottomZ, -halfDepth,
                1, 0, 0);

        // Handle - bottom edge (pommel)
        addQuad(-halfHandle, handleBottomZ, -halfDepth,
                halfHandle, handleBottomZ, -halfDepth,
                halfHandle, handleBottomZ, halfDepth,
                -halfHandle, handleBottomZ, halfDepth,
                0, -1, 0);
    }
};
