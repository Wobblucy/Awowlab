#pragma once
#include "SphereGeometry.h"  // For Vertex3D
#include <vector>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

class RingGeometry {
public:
    // Generate a flat ring (annulus) in the X-Y plane at Z=0
    // innerRadius: size of the hole in the center
    // outerRadius: outer edge of the ring
    // segments: number of radial segments (higher = smoother circle)
    // UV mapping: U maps radially (0=inner, 1=outer) for shader edge effects
    static void generateRing(float innerRadius, float outerRadius, uint32_t segments,
                             std::vector<Vertex3D>& vertices,
                             std::vector<uint32_t>& indices) {
        vertices.clear();
        indices.clear();

        for (uint32_t i = 0; i <= segments; i++) {
            float angle = static_cast<float>(i) / static_cast<float>(segments) * 2.0f * static_cast<float>(M_PI);
            float cosA = std::cos(angle);
            float sinA = std::sin(angle);

            Vertex3D inner{};
            inner.position[0] = cosA * innerRadius;
            inner.position[1] = sinA * innerRadius;
            inner.position[2] = 0.0f;
            inner.normal[0] = 0.0f;
            inner.normal[1] = 0.0f;
            inner.normal[2] = 1.0f;  // Facing up (+Z)
            inner.color[0] = 1.0f;
            inner.color[1] = 1.0f;
            inner.color[2] = 1.0f;
            inner.uv[0] = 0.0f;  // Inner edge
            inner.uv[1] = static_cast<float>(i) / static_cast<float>(segments);
            vertices.push_back(inner);

            Vertex3D outer{};
            outer.position[0] = cosA * outerRadius;
            outer.position[1] = sinA * outerRadius;
            outer.position[2] = 0.0f;
            outer.normal[0] = 0.0f;
            outer.normal[1] = 0.0f;
            outer.normal[2] = 1.0f;  // Facing up (+Z)
            outer.color[0] = 1.0f;
            outer.color[1] = 1.0f;
            outer.color[2] = 1.0f;
            outer.uv[0] = 1.0f;  // Outer edge
            outer.uv[1] = static_cast<float>(i) / static_cast<float>(segments);
            vertices.push_back(outer);
        }

        // Generate indices for quads (as two triangles each)
        // Counter-clockwise winding when viewed from above (+Z)
        for (uint32_t i = 0; i < segments; i++) {
            uint32_t inner1 = i * 2;
            uint32_t outer1 = i * 2 + 1;
            uint32_t inner2 = (i + 1) * 2;
            uint32_t outer2 = (i + 1) * 2 + 1;

            // First triangle: inner1 -> outer1 -> outer2
            indices.push_back(inner1);
            indices.push_back(outer1);
            indices.push_back(outer2);

            // Second triangle: inner1 -> outer2 -> inner2
            indices.push_back(inner1);
            indices.push_back(outer2);
            indices.push_back(inner2);
        }
    }
};
