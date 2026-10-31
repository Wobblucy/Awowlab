#pragma once
#include "SphereGeometry.h"
#include <vector>

class CubeGeometry {
public:
    static void generateCube(float size, std::vector<Vertex3D>& vertices, std::vector<uint32_t>& indices) {
        vertices.clear();
        indices.clear();

        float halfSize = size * 0.5f;

        // Cube has 8 vertices, but we need 24 (4 per face) for proper normals
        // Define the 6 faces with their normals

        // Front face (+Z)
        vertices.push_back({{-halfSize, -halfSize,  halfSize}, {0, 0, 1}, {1, 1, 1}});
        vertices.push_back({{ halfSize, -halfSize,  halfSize}, {0, 0, 1}, {1, 1, 1}});
        vertices.push_back({{ halfSize,  halfSize,  halfSize}, {0, 0, 1}, {1, 1, 1}});
        vertices.push_back({{-halfSize,  halfSize,  halfSize}, {0, 0, 1}, {1, 1, 1}});

        // Back face (-Z)
        vertices.push_back({{ halfSize, -halfSize, -halfSize}, {0, 0, -1}, {1, 1, 1}});
        vertices.push_back({{-halfSize, -halfSize, -halfSize}, {0, 0, -1}, {1, 1, 1}});
        vertices.push_back({{-halfSize,  halfSize, -halfSize}, {0, 0, -1}, {1, 1, 1}});
        vertices.push_back({{ halfSize,  halfSize, -halfSize}, {0, 0, -1}, {1, 1, 1}});

        // Right face (+X)
        vertices.push_back({{ halfSize, -halfSize,  halfSize}, {1, 0, 0}, {1, 1, 1}});
        vertices.push_back({{ halfSize, -halfSize, -halfSize}, {1, 0, 0}, {1, 1, 1}});
        vertices.push_back({{ halfSize,  halfSize, -halfSize}, {1, 0, 0}, {1, 1, 1}});
        vertices.push_back({{ halfSize,  halfSize,  halfSize}, {1, 0, 0}, {1, 1, 1}});

        // Left face (-X)
        vertices.push_back({{-halfSize, -halfSize, -halfSize}, {-1, 0, 0}, {1, 1, 1}});
        vertices.push_back({{-halfSize, -halfSize,  halfSize}, {-1, 0, 0}, {1, 1, 1}});
        vertices.push_back({{-halfSize,  halfSize,  halfSize}, {-1, 0, 0}, {1, 1, 1}});
        vertices.push_back({{-halfSize,  halfSize, -halfSize}, {-1, 0, 0}, {1, 1, 1}});

        // Top face (+Y)
        vertices.push_back({{-halfSize,  halfSize,  halfSize}, {0, 1, 0}, {1, 1, 1}});
        vertices.push_back({{ halfSize,  halfSize,  halfSize}, {0, 1, 0}, {1, 1, 1}});
        vertices.push_back({{ halfSize,  halfSize, -halfSize}, {0, 1, 0}, {1, 1, 1}});
        vertices.push_back({{-halfSize,  halfSize, -halfSize}, {0, 1, 0}, {1, 1, 1}});

        // Bottom face (-Y)
        vertices.push_back({{-halfSize, -halfSize, -halfSize}, {0, -1, 0}, {1, 1, 1}});
        vertices.push_back({{ halfSize, -halfSize, -halfSize}, {0, -1, 0}, {1, 1, 1}});
        vertices.push_back({{ halfSize, -halfSize,  halfSize}, {0, -1, 0}, {1, 1, 1}});
        vertices.push_back({{-halfSize, -halfSize,  halfSize}, {0, -1, 0}, {1, 1, 1}});

        // Generate indices (6 faces * 2 triangles * 3 indices = 36 indices)
        for (uint32_t face = 0; face < 6; face++) {
            uint32_t baseVertex = face * 4;

            indices.push_back(baseVertex + 0);
            indices.push_back(baseVertex + 1);
            indices.push_back(baseVertex + 2);

            indices.push_back(baseVertex + 0);
            indices.push_back(baseVertex + 2);
            indices.push_back(baseVertex + 3);
        }
    }
};
