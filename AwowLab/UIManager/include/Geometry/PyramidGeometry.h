#pragma once
#include "SphereGeometry.h"
#include <vector>
#include <cmath>

class PyramidGeometry {
public:
    // Generate a 4-sided pyramid with square base
    static void generatePyramid(float size, std::vector<Vertex3D>& vertices, std::vector<uint32_t>& indices) {
        vertices.clear();
        indices.clear();

        float half = size * 0.5f;
        float height = size * 0.8f;  // Slightly shorter than wide for classic pyramid look

        // Side faces are tilted inward
        float sideLen = std::sqrt(half * half + height * height);
        float ny = half / sideLen;
        float nxz = height / sideLen;

        // Front face (+Z)
        vertices.push_back({{0, height * 0.5f, 0}, {0, ny, nxz}, {1, 1, 1}});           // apex
        vertices.push_back({{-half, -height * 0.5f, half}, {0, ny, nxz}, {1, 1, 1}});   // front-left
        vertices.push_back({{half, -height * 0.5f, half}, {0, ny, nxz}, {1, 1, 1}});    // front-right

        // Right face (+X)
        vertices.push_back({{0, height * 0.5f, 0}, {nxz, ny, 0}, {1, 1, 1}});
        vertices.push_back({{half, -height * 0.5f, half}, {nxz, ny, 0}, {1, 1, 1}});
        vertices.push_back({{half, -height * 0.5f, -half}, {nxz, ny, 0}, {1, 1, 1}});

        // Back face (-Z)
        vertices.push_back({{0, height * 0.5f, 0}, {0, ny, -nxz}, {1, 1, 1}});
        vertices.push_back({{half, -height * 0.5f, -half}, {0, ny, -nxz}, {1, 1, 1}});
        vertices.push_back({{-half, -height * 0.5f, -half}, {0, ny, -nxz}, {1, 1, 1}});

        // Left face (-X)
        vertices.push_back({{0, height * 0.5f, 0}, {-nxz, ny, 0}, {1, 1, 1}});
        vertices.push_back({{-half, -height * 0.5f, -half}, {-nxz, ny, 0}, {1, 1, 1}});
        vertices.push_back({{-half, -height * 0.5f, half}, {-nxz, ny, 0}, {1, 1, 1}});

        // Base (two triangles, facing down)
        vertices.push_back({{-half, -height * 0.5f, half}, {0, -1, 0}, {1, 1, 1}});
        vertices.push_back({{-half, -height * 0.5f, -half}, {0, -1, 0}, {1, 1, 1}});
        vertices.push_back({{half, -height * 0.5f, -half}, {0, -1, 0}, {1, 1, 1}});
        vertices.push_back({{half, -height * 0.5f, half}, {0, -1, 0}, {1, 1, 1}});

        // Indices for 4 side triangles
        for (uint32_t i = 0; i < 12; i++) {
            indices.push_back(i);
        }

        // Base quad (2 triangles)
        indices.push_back(12);
        indices.push_back(13);
        indices.push_back(14);

        indices.push_back(12);
        indices.push_back(14);
        indices.push_back(15);
    }
};
