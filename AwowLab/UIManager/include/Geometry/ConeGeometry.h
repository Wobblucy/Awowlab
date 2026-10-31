#pragma once
#include "SphereGeometry.h"
#include <vector>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

class ConeGeometry {
public:
    // Generate a cone with base at y=0 and apex pointing up (+Y)
    static void generateCone(float size, uint32_t segments, std::vector<Vertex3D>& vertices, std::vector<uint32_t>& indices) {
        vertices.clear();
        indices.clear();

        float radius = size * 0.5f;
        float height = size;

        // Apex vertex (top point)
        Vertex3D apex{};
        apex.position[0] = 0.0f;
        apex.position[1] = height * 0.5f;
        apex.position[2] = 0.0f;
        apex.normal[0] = 0.0f;
        apex.normal[1] = 1.0f;
        apex.normal[2] = 0.0f;
        apex.color[0] = 1.0f; apex.color[1] = 1.0f; apex.color[2] = 1.0f;

        // Base center vertex
        Vertex3D baseCenter{};
        baseCenter.position[0] = 0.0f;
        baseCenter.position[1] = -height * 0.5f;
        baseCenter.position[2] = 0.0f;
        baseCenter.normal[0] = 0.0f;
        baseCenter.normal[1] = -1.0f;
        baseCenter.normal[2] = 0.0f;
        baseCenter.color[0] = 1.0f; baseCenter.color[1] = 1.0f; baseCenter.color[2] = 1.0f;

        // Generate side vertices (with outward-pointing normals for the cone surface)
        float normalY = radius / std::sqrt(radius * radius + height * height);
        float normalXZ = height / std::sqrt(radius * radius + height * height);

        for (uint32_t i = 0; i <= segments; i++) {
            float angle = static_cast<float>(i) / static_cast<float>(segments) * 2.0f * static_cast<float>(M_PI);
            float cosA = std::cos(angle);
            float sinA = std::sin(angle);

            // Side apex vertex (for this segment)
            Vertex3D sideApex{};
            sideApex.position[0] = 0.0f;
            sideApex.position[1] = height * 0.5f;
            sideApex.position[2] = 0.0f;
            sideApex.normal[0] = cosA * normalXZ;
            sideApex.normal[1] = normalY;
            sideApex.normal[2] = sinA * normalXZ;
            sideApex.color[0] = 1.0f; sideApex.color[1] = 1.0f; sideApex.color[2] = 1.0f;
            vertices.push_back(sideApex);

            // Side base vertex
            Vertex3D sideBase{};
            sideBase.position[0] = cosA * radius;
            sideBase.position[1] = -height * 0.5f;
            sideBase.position[2] = sinA * radius;
            sideBase.normal[0] = cosA * normalXZ;
            sideBase.normal[1] = normalY;
            sideBase.normal[2] = sinA * normalXZ;
            sideBase.color[0] = 1.0f; sideBase.color[1] = 1.0f; sideBase.color[2] = 1.0f;
            vertices.push_back(sideBase);
        }

        for (uint32_t i = 0; i < segments; i++) {
            uint32_t apex1 = i * 2;
            uint32_t base1 = i * 2 + 1;
            [[maybe_unused]] uint32_t apex2 = (i + 1) * 2;
            uint32_t base2 = (i + 1) * 2 + 1;

            indices.push_back(apex1);
            indices.push_back(base2);
            indices.push_back(base1);
        }

        uint32_t baseCenterIdx = static_cast<uint32_t>(vertices.size());
        vertices.push_back(baseCenter);

        for (uint32_t i = 0; i <= segments; i++) {
            float angle = static_cast<float>(i) / static_cast<float>(segments) * 2.0f * static_cast<float>(M_PI);
            float cosA = std::cos(angle);
            float sinA = std::sin(angle);

            Vertex3D baseVert{};
            baseVert.position[0] = cosA * radius;
            baseVert.position[1] = -height * 0.5f;
            baseVert.position[2] = sinA * radius;
            baseVert.normal[0] = 0.0f;
            baseVert.normal[1] = -1.0f;
            baseVert.normal[2] = 0.0f;
            baseVert.color[0] = 1.0f; baseVert.color[1] = 1.0f; baseVert.color[2] = 1.0f;
            vertices.push_back(baseVert);
        }

        // Generate base triangles (fan from center)
        for (uint32_t i = 0; i < segments; i++) {
            indices.push_back(baseCenterIdx);
            indices.push_back(baseCenterIdx + 1 + i);
            indices.push_back(baseCenterIdx + 2 + i);
        }
    }
};
