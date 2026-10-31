#pragma once
#include "SphereGeometry.h"
#include <vector>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

class CylinderGeometry {
public:
    // Generate a cylinder centered at origin
    static void generateCylinder(float size, uint32_t segments, std::vector<Vertex3D>& vertices, std::vector<uint32_t>& indices) {
        vertices.clear();
        indices.clear();

        float radius = size * 0.4f;
        float halfHeight = size * 0.5f;

        // Side vertices (need separate vertices for proper normals)
        for (uint32_t i = 0; i <= segments; i++) {
            float angle = static_cast<float>(i) / static_cast<float>(segments) * 2.0f * static_cast<float>(M_PI);
            float cosA = std::cos(angle);
            float sinA = std::sin(angle);

            // Top ring vertex
            Vertex3D top{};
            top.position[0] = cosA * radius;
            top.position[1] = halfHeight;
            top.position[2] = sinA * radius;
            top.normal[0] = cosA;
            top.normal[1] = 0.0f;
            top.normal[2] = sinA;
            top.color[0] = 1.0f; top.color[1] = 1.0f; top.color[2] = 1.0f;
            vertices.push_back(top);

            // Bottom ring vertex
            Vertex3D bottom{};
            bottom.position[0] = cosA * radius;
            bottom.position[1] = -halfHeight;
            bottom.position[2] = sinA * radius;
            bottom.normal[0] = cosA;
            bottom.normal[1] = 0.0f;
            bottom.normal[2] = sinA;
            bottom.color[0] = 1.0f; bottom.color[1] = 1.0f; bottom.color[2] = 1.0f;
            vertices.push_back(bottom);
        }

        // Side indices (quads as two triangles)
        for (uint32_t i = 0; i < segments; i++) {
            uint32_t top1 = i * 2;
            uint32_t bot1 = i * 2 + 1;
            uint32_t top2 = (i + 1) * 2;
            uint32_t bot2 = (i + 1) * 2 + 1;

            indices.push_back(top1);
            indices.push_back(bot1);
            indices.push_back(bot2);

            indices.push_back(top1);
            indices.push_back(bot2);
            indices.push_back(top2);
        }

        // Top cap
        uint32_t topCenterIdx = static_cast<uint32_t>(vertices.size());
        Vertex3D topCenter{};
        topCenter.position[0] = 0.0f;
        topCenter.position[1] = halfHeight;
        topCenter.position[2] = 0.0f;
        topCenter.normal[0] = 0.0f;
        topCenter.normal[1] = 1.0f;
        topCenter.normal[2] = 0.0f;
        topCenter.color[0] = 1.0f; topCenter.color[1] = 1.0f; topCenter.color[2] = 1.0f;
        vertices.push_back(topCenter);

        for (uint32_t i = 0; i <= segments; i++) {
            float angle = static_cast<float>(i) / static_cast<float>(segments) * 2.0f * static_cast<float>(M_PI);
            Vertex3D v{};
            v.position[0] = std::cos(angle) * radius;
            v.position[1] = halfHeight;
            v.position[2] = std::sin(angle) * radius;
            v.normal[0] = 0.0f;
            v.normal[1] = 1.0f;
            v.normal[2] = 0.0f;
            v.color[0] = 1.0f; v.color[1] = 1.0f; v.color[2] = 1.0f;
            vertices.push_back(v);
        }

        for (uint32_t i = 0; i < segments; i++) {
            indices.push_back(topCenterIdx);
            indices.push_back(topCenterIdx + 2 + i);
            indices.push_back(topCenterIdx + 1 + i);
        }

        // Bottom cap
        uint32_t botCenterIdx = static_cast<uint32_t>(vertices.size());
        Vertex3D botCenter{};
        botCenter.position[0] = 0.0f;
        botCenter.position[1] = -halfHeight;
        botCenter.position[2] = 0.0f;
        botCenter.normal[0] = 0.0f;
        botCenter.normal[1] = -1.0f;
        botCenter.normal[2] = 0.0f;
        botCenter.color[0] = 1.0f; botCenter.color[1] = 1.0f; botCenter.color[2] = 1.0f;
        vertices.push_back(botCenter);

        for (uint32_t i = 0; i <= segments; i++) {
            float angle = static_cast<float>(i) / static_cast<float>(segments) * 2.0f * static_cast<float>(M_PI);
            Vertex3D v{};
            v.position[0] = std::cos(angle) * radius;
            v.position[1] = -halfHeight;
            v.position[2] = std::sin(angle) * radius;
            v.normal[0] = 0.0f;
            v.normal[1] = -1.0f;
            v.normal[2] = 0.0f;
            v.color[0] = 1.0f; v.color[1] = 1.0f; v.color[2] = 1.0f;
            vertices.push_back(v);
        }

        for (uint32_t i = 0; i < segments; i++) {
            indices.push_back(botCenterIdx);
            indices.push_back(botCenterIdx + 1 + i);
            indices.push_back(botCenterIdx + 2 + i);
        }
    }
};
