#pragma once
#include <vector>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Vertex structure matching shader layout
struct Vertex3D {
    float position[3] = {0.0f, 0.0f, 0.0f};  // xyz
    float normal[3] = {0.0f, 0.0f, 1.0f};    // nx ny nz (default facing +Z)
    float color[3] = {1.0f, 1.0f, 1.0f};     // rgb (default white)
    float uv[2] = {0.0f, 0.0f};              // texture coordinates (used by floor plane)
};

class SphereGeometry {
public:
    static void generateUVSphere(float radius, uint32_t segments, uint32_t rings,
                                 std::vector<Vertex3D>& vertices,
                                 std::vector<uint32_t>& indices) {
        vertices.clear();
        indices.clear();

        for (uint32_t ring = 0; ring <= rings; ring++) {
            float theta = static_cast<float>(ring) / static_cast<float>(rings) * static_cast<float>(M_PI);
            float sinTheta = std::sin(theta);
            float cosTheta = std::cos(theta);

            for (uint32_t segment = 0; segment <= segments; segment++) {
                float phi = static_cast<float>(segment) / static_cast<float>(segments) * 2.0f * static_cast<float>(M_PI);
                float sinPhi = std::sin(phi);
                float cosPhi = std::cos(phi);

                Vertex3D vertex{};

                float x = cosPhi * sinTheta;
                float y = cosTheta;
                float z = sinPhi * sinTheta;

                vertex.position[0] = x * radius;
                vertex.position[1] = y * radius;
                vertex.position[2] = z * radius;

                // Normal (normalized position for sphere)
                vertex.normal[0] = x;
                vertex.normal[1] = y;
                vertex.normal[2] = z;

                // The push constant color replaces this
                vertex.color[0] = 1.0f;
                vertex.color[1] = 1.0f;
                vertex.color[2] = 1.0f;

                // Unused by the sphere shader
                vertex.uv[0] = 0.0f;
                vertex.uv[1] = 0.0f;

                vertices.push_back(vertex);
            }
        }

        // Generate indices with counter-clockwise winding for front faces (outside of sphere)
        for (uint32_t ring = 0; ring < rings; ring++) {
            for (uint32_t segment = 0; segment < segments; segment++) {
                uint32_t current = ring * (segments + 1) + segment;
                uint32_t next = current + segments + 1;

                // First triangle (counter-clockwise when viewed from outside)
                indices.push_back(current);
                indices.push_back(current + 1);
                indices.push_back(next);

                // Second triangle (counter-clockwise when viewed from outside)
                indices.push_back(current + 1);
                indices.push_back(next + 1);
                indices.push_back(next);
            }
        }
    }
};
