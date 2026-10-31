#pragma once
#include "SphereGeometry.h"
#include "GeometryHelpers.h"
#include <vector>

// Medical cross geometry for Healer role - 3D plus sign
// Oriented to stand upright (Z-up)
class MedicCrossGeometry {
public:
    // Generate a 3D medical cross (plus sign with depth)
    // size: overall width/height of the cross
    static void generateMedicCross(float size, std::vector<Vertex3D>& vertices, std::vector<uint32_t>& indices) {
        vertices.clear();
        indices.clear();

        // The cross lies in the X-Z plane; Y is depth
        float armLength = size * 0.5f;    // Half the total size
        float armWidth = size * 0.2f;     // Width of each arm
        float depth = size * 0.15f;       // Thickness (Y direction)

        float halfArm = armWidth * 0.5f;
        float halfDepth = depth * 0.5f;

        auto addQuad = [&](float x0, float z0, float y0,
                          float x1, float z1, float y1,
                          float x2, float z2, float y2,
                          float x3, float z3, float y3,
                          float nx, float nz, float ny) {
            geometry::addQuad(vertices, indices, x0, z0, y0, x1, z1, y1, x2, z2, y2, x3, z3, y3, nx, nz, ny);
        };

        // Front face (+Y) - full cross shape
        // Top arm of cross (going up in Z)
        addQuad(-halfArm, halfArm, halfDepth,
                halfArm, halfArm, halfDepth,
                halfArm, armLength, halfDepth,
                -halfArm, armLength, halfDepth,
                0, 0, 1);

        // Bottom arm of cross (going down in Z)
        addQuad(-halfArm, -armLength, halfDepth,
                halfArm, -armLength, halfDepth,
                halfArm, -halfArm, halfDepth,
                -halfArm, -halfArm, halfDepth,
                0, 0, 1);

        // Left arm of cross (-X)
        addQuad(-armLength, -halfArm, halfDepth,
                -halfArm, -halfArm, halfDepth,
                -halfArm, halfArm, halfDepth,
                -armLength, halfArm, halfDepth,
                0, 0, 1);

        // Right arm of cross (+X)
        addQuad(halfArm, -halfArm, halfDepth,
                armLength, -halfArm, halfDepth,
                armLength, halfArm, halfDepth,
                halfArm, halfArm, halfDepth,
                0, 0, 1);

        // Center of cross
        addQuad(-halfArm, -halfArm, halfDepth,
                halfArm, -halfArm, halfDepth,
                halfArm, halfArm, halfDepth,
                -halfArm, halfArm, halfDepth,
                0, 0, 1);

        // Back face (-Y) - full cross shape (reversed winding)
        // Top arm
        addQuad(-halfArm, armLength, -halfDepth,
                halfArm, armLength, -halfDepth,
                halfArm, halfArm, -halfDepth,
                -halfArm, halfArm, -halfDepth,
                0, 0, -1);

        // Bottom arm
        addQuad(-halfArm, -halfArm, -halfDepth,
                halfArm, -halfArm, -halfDepth,
                halfArm, -armLength, -halfDepth,
                -halfArm, -armLength, -halfDepth,
                0, 0, -1);

        // Left arm
        addQuad(-armLength, halfArm, -halfDepth,
                -halfArm, halfArm, -halfDepth,
                -halfArm, -halfArm, -halfDepth,
                -armLength, -halfArm, -halfDepth,
                0, 0, -1);

        // Right arm
        addQuad(halfArm, halfArm, -halfDepth,
                armLength, halfArm, -halfDepth,
                armLength, -halfArm, -halfDepth,
                halfArm, -halfArm, -halfDepth,
                0, 0, -1);

        // Center
        addQuad(-halfArm, halfArm, -halfDepth,
                halfArm, halfArm, -halfDepth,
                halfArm, -halfArm, -halfDepth,
                -halfArm, -halfArm, -halfDepth,
                0, 0, -1);

        // Side faces - Top arm (+Z direction)
        // Top edge
        addQuad(-halfArm, armLength, halfDepth,
                halfArm, armLength, halfDepth,
                halfArm, armLength, -halfDepth,
                -halfArm, armLength, -halfDepth,
                0, 1, 0);

        // Left edge of top arm
        addQuad(-halfArm, halfArm, halfDepth,
                -halfArm, armLength, halfDepth,
                -halfArm, armLength, -halfDepth,
                -halfArm, halfArm, -halfDepth,
                -1, 0, 0);

        // Right edge of top arm
        addQuad(halfArm, armLength, halfDepth,
                halfArm, halfArm, halfDepth,
                halfArm, halfArm, -halfDepth,
                halfArm, armLength, -halfDepth,
                1, 0, 0);

        // Side faces - Bottom arm (-Z direction)
        // Bottom edge
        addQuad(-halfArm, -armLength, -halfDepth,
                halfArm, -armLength, -halfDepth,
                halfArm, -armLength, halfDepth,
                -halfArm, -armLength, halfDepth,
                0, -1, 0);

        // Left edge of bottom arm
        addQuad(-halfArm, -armLength, halfDepth,
                -halfArm, -halfArm, halfDepth,
                -halfArm, -halfArm, -halfDepth,
                -halfArm, -armLength, -halfDepth,
                -1, 0, 0);

        // Right edge of bottom arm
        addQuad(halfArm, -halfArm, halfDepth,
                halfArm, -armLength, halfDepth,
                halfArm, -armLength, -halfDepth,
                halfArm, -halfArm, -halfDepth,
                1, 0, 0);

        // Side faces - Left arm (-X direction)
        // Left edge
        addQuad(-armLength, halfArm, halfDepth,
                -armLength, -halfArm, halfDepth,
                -armLength, -halfArm, -halfDepth,
                -armLength, halfArm, -halfDepth,
                -1, 0, 0);

        // Top edge of left arm
        addQuad(-armLength, halfArm, halfDepth,
                -armLength, halfArm, -halfDepth,
                -halfArm, halfArm, -halfDepth,
                -halfArm, halfArm, halfDepth,
                0, 1, 0);

        // Bottom edge of left arm
        addQuad(-halfArm, -halfArm, halfDepth,
                -halfArm, -halfArm, -halfDepth,
                -armLength, -halfArm, -halfDepth,
                -armLength, -halfArm, halfDepth,
                0, -1, 0);

        // Side faces - Right arm (+X direction)
        // Right edge
        addQuad(armLength, -halfArm, halfDepth,
                armLength, halfArm, halfDepth,
                armLength, halfArm, -halfDepth,
                armLength, -halfArm, -halfDepth,
                1, 0, 0);

        // Top edge of right arm
        addQuad(halfArm, halfArm, halfDepth,
                halfArm, halfArm, -halfDepth,
                armLength, halfArm, -halfDepth,
                armLength, halfArm, halfDepth,
                0, 1, 0);

        // Bottom edge of right arm
        addQuad(armLength, -halfArm, halfDepth,
                armLength, -halfArm, -halfDepth,
                halfArm, -halfArm, -halfDepth,
                halfArm, -halfArm, halfDepth,
                0, -1, 0);
    }
};
