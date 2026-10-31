#pragma once
#include "SphereGeometry.h"
#include <vector>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

class StarGeometry {
public:
    // Generate a 3D star (6 pointed - one point on each axis)
    static void generateStar(float size, std::vector<Vertex3D>& vertices, std::vector<uint32_t>& indices) {
        vertices.clear();
        indices.clear();

        float pointLen = size * 0.6f;   // Length of each point
        float coreSize = size * 0.25f;  // Size of central core

        // 6 point tips: +X, -X, +Y, -Y, +Z, -Z
        // 8 core vertices (cube corners)
        // Each point connects to 4 adjacent core vertices

        // Core vertices (small cube at center)
        float c = coreSize;
        // Indexed 0-7
        std::vector<std::array<float, 3>> coreVerts = {
            {-c, -c, -c},  // 0
            { c, -c, -c},  // 1
            { c,  c, -c},  // 2
            {-c,  c, -c},  // 3
            {-c, -c,  c},  // 4
            { c, -c,  c},  // 5
            { c,  c,  c},  // 6
            {-c,  c,  c}   // 7
        };

        // Point tips
        std::array<float, 3> tips[6] = {
            { pointLen, 0, 0},  // +X
            {-pointLen, 0, 0},  // -X
            {0,  pointLen, 0},  // +Y
            {0, -pointLen, 0},  // -Y
            {0, 0,  pointLen},  // +Z
            {0, 0, -pointLen}   // -Z
        };

        // Each point has 4 triangular faces
        // +X point connects to core vertices 1,2,5,6 (the +X face of core)
        int pointCoreVerts[6][4] = {
            {1, 5, 6, 2},  // +X
            {0, 3, 7, 4},  // -X
            {3, 2, 6, 7},  // +Y
            {0, 4, 5, 1},  // -Y
            {4, 7, 6, 5},  // +Z
            {0, 1, 2, 3}   // -Z
        };

        auto addPointFaces = [&](int pointIdx) {
            auto& tip = tips[pointIdx];
            auto& coreIdxs = pointCoreVerts[pointIdx];

            // 4 triangular faces per point
            for (int i = 0; i < 4; i++) {
                int c1 = coreIdxs[i];
                int c2 = coreIdxs[(i + 1) % 4];

                auto& v1 = coreVerts[c1];
                auto& v2 = coreVerts[c2];

                float e1x = v1[0] - tip[0], e1y = v1[1] - tip[1], e1z = v1[2] - tip[2];
                float e2x = v2[0] - tip[0], e2y = v2[1] - tip[1], e2z = v2[2] - tip[2];
                float nx = e1y * e2z - e1z * e2y;
                float ny = e1z * e2x - e1x * e2z;
                float nz = e1x * e2y - e1y * e2x;
                float len = std::sqrt(nx*nx + ny*ny + nz*nz);
                if (len > 0.0001f) { nx /= len; ny /= len; nz /= len; }

                uint32_t baseIdx = static_cast<uint32_t>(vertices.size());

                vertices.push_back({{tip[0], tip[1], tip[2]}, {nx, ny, nz}, {1, 1, 1}});
                vertices.push_back({{v1[0], v1[1], v1[2]}, {nx, ny, nz}, {1, 1, 1}});
                vertices.push_back({{v2[0], v2[1], v2[2]}, {nx, ny, nz}, {1, 1, 1}});

                indices.push_back(baseIdx);
                indices.push_back(baseIdx + 1);
                indices.push_back(baseIdx + 2);
            }
        };

        // Generate all 6 points (24 triangles total)
        for (int i = 0; i < 6; i++) {
            addPointFaces(i);
        }
    }
};
