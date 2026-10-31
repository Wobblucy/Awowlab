#pragma once
#include "SphereGeometry.h"
#include <vector>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

class DiamondGeometry {
public:
    // Generate an octahedron (double pyramid / diamond shape)
    static void generateDiamond(float size, std::vector<Vertex3D>& vertices, std::vector<uint32_t>& indices) {
        vertices.clear();
        indices.clear();

        float half = size * 0.5f;

        // 6 vertices: top, bottom, and 4 equatorial points
        // But we need separate vertices per face for proper normals

        // Define the 8 triangular faces with their normals
        // Top pyramid (4 faces)
        float nv = 1.0f / std::sqrt(2.0f);  // Normal vertical component
        float nh = 1.0f / std::sqrt(2.0f);  // Normal horizontal component

        // Face 1: Top, +X+Z
        vertices.push_back({{0, half, 0}, {nh, nv, nh}, {1, 1, 1}});      // top
        vertices.push_back({{half, 0, 0}, {nh, nv, nh}, {1, 1, 1}});      // +X
        vertices.push_back({{0, 0, half}, {nh, nv, nh}, {1, 1, 1}});      // +Z

        // Face 2: Top, +Z-X
        vertices.push_back({{0, half, 0}, {-nh, nv, nh}, {1, 1, 1}});
        vertices.push_back({{0, 0, half}, {-nh, nv, nh}, {1, 1, 1}});
        vertices.push_back({{-half, 0, 0}, {-nh, nv, nh}, {1, 1, 1}});

        // Face 3: Top, -X-Z
        vertices.push_back({{0, half, 0}, {-nh, nv, -nh}, {1, 1, 1}});
        vertices.push_back({{-half, 0, 0}, {-nh, nv, -nh}, {1, 1, 1}});
        vertices.push_back({{0, 0, -half}, {-nh, nv, -nh}, {1, 1, 1}});

        // Face 4: Top, -Z+X
        vertices.push_back({{0, half, 0}, {nh, nv, -nh}, {1, 1, 1}});
        vertices.push_back({{0, 0, -half}, {nh, nv, -nh}, {1, 1, 1}});
        vertices.push_back({{half, 0, 0}, {nh, nv, -nh}, {1, 1, 1}});

        // Bottom pyramid (4 faces)
        // Face 5: Bottom, +X+Z
        vertices.push_back({{0, -half, 0}, {nh, -nv, nh}, {1, 1, 1}});
        vertices.push_back({{0, 0, half}, {nh, -nv, nh}, {1, 1, 1}});
        vertices.push_back({{half, 0, 0}, {nh, -nv, nh}, {1, 1, 1}});

        // Face 6: Bottom, +Z-X
        vertices.push_back({{0, -half, 0}, {-nh, -nv, nh}, {1, 1, 1}});
        vertices.push_back({{-half, 0, 0}, {-nh, -nv, nh}, {1, 1, 1}});
        vertices.push_back({{0, 0, half}, {-nh, -nv, nh}, {1, 1, 1}});

        // Face 7: Bottom, -X-Z
        vertices.push_back({{0, -half, 0}, {-nh, -nv, -nh}, {1, 1, 1}});
        vertices.push_back({{0, 0, -half}, {-nh, -nv, -nh}, {1, 1, 1}});
        vertices.push_back({{-half, 0, 0}, {-nh, -nv, -nh}, {1, 1, 1}});

        // Face 8: Bottom, -Z+X
        vertices.push_back({{0, -half, 0}, {nh, -nv, -nh}, {1, 1, 1}});
        vertices.push_back({{half, 0, 0}, {nh, -nv, -nh}, {1, 1, 1}});
        vertices.push_back({{0, 0, -half}, {nh, -nv, -nh}, {1, 1, 1}});

        for (uint32_t i = 0; i < 24; i++) {
            indices.push_back(i);
        }
    }
};
