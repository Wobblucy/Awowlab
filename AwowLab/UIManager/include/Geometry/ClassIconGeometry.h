#pragma once
#include "SphereGeometry.h"
#include <vector>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

class ClassIconGeometry {
public:
    // Generate a poker-chip style disc with:
    // - Front face: textured (UVs mapped for spec icon)
    // - Back face: solid color (no UVs needed)
    // - Edge: solid color ring
    // Front face normal points +Z (faces camera after billboard transform)
    static void generateClassIcon(float size, uint32_t segments,
                                   std::vector<Vertex3D>& vertices,
                                   std::vector<uint32_t>& indices) {
        vertices.clear();
        indices.clear();

        float radius = size * 0.5f;
        float halfThickness = size * 0.075f;  // Thin disc (~15% of diameter)

        // Front face (textured, facing +Z)
        uint32_t frontCenterIdx = static_cast<uint32_t>(vertices.size());
        Vertex3D frontCenter{};
        frontCenter.position[0] = 0.0f;
        frontCenter.position[1] = 0.0f;
        frontCenter.position[2] = halfThickness;
        frontCenter.normal[0] = 0.0f;
        frontCenter.normal[1] = 0.0f;
        frontCenter.normal[2] = 1.0f;
        frontCenter.color[0] = 1.0f; frontCenter.color[1] = 1.0f; frontCenter.color[2] = 1.0f;
        frontCenter.uv[0] = 0.5f;
        frontCenter.uv[1] = 0.5f;
        vertices.push_back(frontCenter);

        // Ring vertices with UV mapping
        // Inset UV radius to crop edges (0.42 instead of 0.5 gives ~16% zoom)
        const float uvRadius = 0.42f;
        for (uint32_t i = 0; i <= segments; i++) {
            float angle = static_cast<float>(i) / static_cast<float>(segments) * 2.0f * static_cast<float>(M_PI);
            float cosA = std::cos(angle);
            float sinA = std::sin(angle);

            Vertex3D v{};
            v.position[0] = cosA * radius;
            v.position[1] = sinA * radius;
            v.position[2] = halfThickness;
            v.normal[0] = 0.0f;
            v.normal[1] = 0.0f;
            v.normal[2] = 1.0f;
            v.color[0] = 1.0f; v.color[1] = 1.0f; v.color[2] = 1.0f;
            // Circular UV mapping: inset to hide texture edges, flip V to correct orientation
            v.uv[0] = 0.5f + cosA * uvRadius;
            v.uv[1] = 0.5f - sinA * uvRadius;
            vertices.push_back(v);
        }

        // Front face triangles (fan from center)
        for (uint32_t i = 0; i < segments; i++) {
            indices.push_back(frontCenterIdx);
            indices.push_back(frontCenterIdx + 1 + i);
            indices.push_back(frontCenterIdx + 2 + i);
        }

        // Back face (solid color, facing -Z)
        uint32_t backCenterIdx = static_cast<uint32_t>(vertices.size());
        Vertex3D backCenter{};
        backCenter.position[0] = 0.0f;
        backCenter.position[1] = 0.0f;
        backCenter.position[2] = -halfThickness;
        backCenter.normal[0] = 0.0f;
        backCenter.normal[1] = 0.0f;
        backCenter.normal[2] = -1.0f;
        backCenter.color[0] = 1.0f; backCenter.color[1] = 1.0f; backCenter.color[2] = 1.0f;
        backCenter.uv[0] = 0.0f;  // UVs don't matter for back (solid color)
        backCenter.uv[1] = 0.0f;
        vertices.push_back(backCenter);

        for (uint32_t i = 0; i <= segments; i++) {
            float angle = static_cast<float>(i) / static_cast<float>(segments) * 2.0f * static_cast<float>(M_PI);
            float cosA = std::cos(angle);
            float sinA = std::sin(angle);

            Vertex3D v{};
            v.position[0] = cosA * radius;
            v.position[1] = sinA * radius;
            v.position[2] = -halfThickness;
            v.normal[0] = 0.0f;
            v.normal[1] = 0.0f;
            v.normal[2] = -1.0f;
            v.color[0] = 1.0f; v.color[1] = 1.0f; v.color[2] = 1.0f;
            v.uv[0] = 0.0f;
            v.uv[1] = 0.0f;
            vertices.push_back(v);
        }

        // Back face triangles (fan from center, reversed winding)
        for (uint32_t i = 0; i < segments; i++) {
            indices.push_back(backCenterIdx);
            indices.push_back(backCenterIdx + 2 + i);
            indices.push_back(backCenterIdx + 1 + i);
        }

        // Edge (solid color ring)
        uint32_t edgeStartIdx = static_cast<uint32_t>(vertices.size());

        for (uint32_t i = 0; i <= segments; i++) {
            float angle = static_cast<float>(i) / static_cast<float>(segments) * 2.0f * static_cast<float>(M_PI);
            float cosA = std::cos(angle);
            float sinA = std::sin(angle);

            // Front edge vertex
            Vertex3D front{};
            front.position[0] = cosA * radius;
            front.position[1] = sinA * radius;
            front.position[2] = halfThickness;
            front.normal[0] = cosA;
            front.normal[1] = sinA;
            front.normal[2] = 0.0f;
            front.color[0] = 1.0f; front.color[1] = 1.0f; front.color[2] = 1.0f;
            front.uv[0] = 0.0f;
            front.uv[1] = 0.0f;
            vertices.push_back(front);

            // Back edge vertex
            Vertex3D back{};
            back.position[0] = cosA * radius;
            back.position[1] = sinA * radius;
            back.position[2] = -halfThickness;
            back.normal[0] = cosA;
            back.normal[1] = sinA;
            back.normal[2] = 0.0f;
            back.color[0] = 1.0f; back.color[1] = 1.0f; back.color[2] = 1.0f;
            back.uv[0] = 0.0f;
            back.uv[1] = 0.0f;
            vertices.push_back(back);
        }

        // Edge triangles (quads as two triangles)
        for (uint32_t i = 0; i < segments; i++) {
            uint32_t front1 = edgeStartIdx + i * 2;
            uint32_t back1 = edgeStartIdx + i * 2 + 1;
            uint32_t front2 = edgeStartIdx + (i + 1) * 2;
            uint32_t back2 = edgeStartIdx + (i + 1) * 2 + 1;

            indices.push_back(front1);
            indices.push_back(back1);
            indices.push_back(back2);

            indices.push_back(front1);
            indices.push_back(back2);
            indices.push_back(front2);
        }
    }
};
