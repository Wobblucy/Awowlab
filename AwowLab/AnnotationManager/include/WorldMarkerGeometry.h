#pragma once

#include "AnnotationRenderer.h"  // For AnnotationVertex
#include <vector>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Procedural geometry for the raid marker symbols, centred on the origin and
// flat in the XY plane. AnnotationWorldMarkerRenderer turns them to face the camera.
class WorldMarkerGeometry {
public:
    // Four-pointed star
    static void generateStar(float size, std::vector<AnnotationVertex>& vertices,
                             std::vector<uint32_t>& indices) {
        vertices.clear();
        indices.clear();

        float halfSize = size * 0.5f;
        float innerRadius = size * 0.2f;  // Inner cutout for star points

        // 4 tips and 4 valleys, fanned from the centre
        AnnotationVertex center{};
        center.position[0] = 0.0f;
        center.position[1] = 0.0f;
        center.position[2] = 0.0f;
        center.normal[0] = 0.0f;
        center.normal[1] = 0.0f;
        center.normal[2] = 1.0f;
        center.color[0] = 1.0f; center.color[1] = 1.0f; center.color[2] = 1.0f;
        center.uv[0] = 0.5f; center.uv[1] = 0.5f;
        vertices.push_back(center);

        // 8 points alternating outer/inner
        for (int i = 0; i < 8; i++) {
            float angle = static_cast<float>(i) * static_cast<float>(M_PI) / 4.0f - static_cast<float>(M_PI) / 2.0f;  // start at the top
            float radius = (i % 2 == 0) ? halfSize : innerRadius;

            AnnotationVertex v{};
            v.position[0] = std::cos(angle) * radius;
            v.position[1] = std::sin(angle) * radius;
            v.position[2] = 0.0f;
            v.normal[0] = 0.0f;
            v.normal[1] = 0.0f;
            v.normal[2] = 1.0f;
            v.color[0] = 1.0f; v.color[1] = 1.0f; v.color[2] = 1.0f;
            v.uv[0] = 0.5f + v.position[0] / size;
            v.uv[1] = 0.5f + v.position[1] / size;
            vertices.push_back(v);
        }

        for (int i = 0; i < 8; i++) {
            indices.push_back(0);
            indices.push_back(1 + i);
            indices.push_back(1 + ((i + 1) % 8));
        }
    }

    // Filled disc
    static void generateCircle(float size, uint32_t segments,
                               std::vector<AnnotationVertex>& vertices,
                               std::vector<uint32_t>& indices) {
        vertices.clear();
        indices.clear();

        float radius = size * 0.5f;

        AnnotationVertex center{};
        center.position[0] = 0.0f;
        center.position[1] = 0.0f;
        center.position[2] = 0.0f;
        center.normal[0] = 0.0f;
        center.normal[1] = 0.0f;
        center.normal[2] = 1.0f;
        center.color[0] = 1.0f; center.color[1] = 1.0f; center.color[2] = 1.0f;
        center.uv[0] = 0.5f; center.uv[1] = 0.5f;
        vertices.push_back(center);

        for (uint32_t i = 0; i <= segments; i++) {
            float angle = static_cast<float>(i) / static_cast<float>(segments) * 2.0f * static_cast<float>(M_PI);

            AnnotationVertex v{};
            v.position[0] = std::cos(angle) * radius;
            v.position[1] = std::sin(angle) * radius;
            v.position[2] = 0.0f;
            v.normal[0] = 0.0f;
            v.normal[1] = 0.0f;
            v.normal[2] = 1.0f;
            v.color[0] = 1.0f; v.color[1] = 1.0f; v.color[2] = 1.0f;
            v.uv[0] = 0.5f + v.position[0] / size;
            v.uv[1] = 0.5f + v.position[1] / size;
            vertices.push_back(v);
        }

        for (uint32_t i = 0; i < segments; i++) {
            indices.push_back(0);
            indices.push_back(1 + i);
            indices.push_back(2 + i);
        }
    }

    // Diamond (square on its corner)
    static void generateDiamond(float size, std::vector<AnnotationVertex>& vertices,
                                std::vector<uint32_t>& indices) {
        vertices.clear();
        indices.clear();

        float halfSize = size * 0.5f;

        // 4 corner vertices (diamond orientation)
        float positions[4][2] = {
            {0.0f, halfSize},      // Top
            {halfSize, 0.0f},      // Right
            {0.0f, -halfSize},     // Bottom
            {-halfSize, 0.0f}      // Left
        };

        for (int i = 0; i < 4; i++) {
            AnnotationVertex v{};
            v.position[0] = positions[i][0];
            v.position[1] = positions[i][1];
            v.position[2] = 0.0f;
            v.normal[0] = 0.0f;
            v.normal[1] = 0.0f;
            v.normal[2] = 1.0f;
            v.color[0] = 1.0f; v.color[1] = 1.0f; v.color[2] = 1.0f;
            v.uv[0] = 0.5f + v.position[0] / size;
            v.uv[1] = 0.5f + v.position[1] / size;
            vertices.push_back(v);
        }

        indices.push_back(0); indices.push_back(1); indices.push_back(2);
        indices.push_back(0); indices.push_back(2); indices.push_back(3);
    }

    // Equilateral triangle, rotated 60 degrees
    static void generateTriangle(float size, std::vector<AnnotationVertex>& vertices,
                                  std::vector<uint32_t>& indices) {
        vertices.clear();
        indices.clear();

        float halfSize = size * 0.5f;
        float height = halfSize * 1.732f;  // sqrt(3)

        // Base positions (pointing up)
        float basePositions[3][2] = {
            {0.0f, height * 0.5f},                // Top
            {-halfSize, -height * 0.5f},          // Bottom left
            {halfSize, -height * 0.5f}            // Bottom right
        };

        float rotAngle = static_cast<float>(M_PI) / 3.0f;
        float cosR = std::cos(rotAngle);
        float sinR = std::sin(rotAngle);

        for (int i = 0; i < 3; i++) {
            AnnotationVertex v{};
            v.position[0] = basePositions[i][0] * cosR - basePositions[i][1] * sinR;
            v.position[1] = basePositions[i][0] * sinR + basePositions[i][1] * cosR;
            v.position[2] = 0.0f;
            v.normal[0] = 0.0f;
            v.normal[1] = 0.0f;
            v.normal[2] = 1.0f;
            v.color[0] = 1.0f; v.color[1] = 1.0f; v.color[2] = 1.0f;
            v.uv[0] = 0.5f + v.position[0] / size;
            v.uv[1] = 0.5f + v.position[1] / size;
            vertices.push_back(v);
        }

        indices.push_back(0); indices.push_back(1); indices.push_back(2);
    }

    // Crescent: a "C" rotated 200 degrees
    static void generateMoon(float size, uint32_t segments,
                             std::vector<AnnotationVertex>& vertices,
                             std::vector<uint32_t>& indices) {
        vertices.clear();
        indices.clear();

        float outerR = size * 0.5f;
        float innerR = size * 0.38f;
        float innerOffset = size * 0.2f;  // Shift inner circle right

        float rotAngle = 200.0f * static_cast<float>(M_PI) / 180.0f;
        float cosR = std::cos(rotAngle);
        float sinR = std::sin(rotAngle);

        // A triangle strip between the outer and inner arcs, covering only the
        // left side where the outer arc lies outside the inner one
        uint32_t numPoints = segments + 1;

        // Outer arc: full left semicircle (90° to 270°, i.e., top-left around to bottom)
        for (uint32_t i = 0; i < numPoints; i++) {
            float t = static_cast<float>(i) / static_cast<float>(segments);
            float angle = static_cast<float>(M_PI) * 0.5f + static_cast<float>(M_PI) * t;  // 90° to 270°

            float x = std::cos(angle) * outerR;
            float y = std::sin(angle) * outerR;

            AnnotationVertex v{};
            v.position[0] = x * cosR - y * sinR;
            v.position[1] = x * sinR + y * cosR;
            v.position[2] = 0.0f;
            v.normal[0] = 0.0f;
            v.normal[1] = 0.0f;
            v.normal[2] = 1.0f;
            v.color[0] = 1.0f; v.color[1] = 1.0f; v.color[2] = 1.0f;
            v.uv[0] = 0.5f + v.position[0] / size;
            v.uv[1] = 0.5f + v.position[1] / size;
            vertices.push_back(v);
        }

        // Inner arc: same angular range but smaller radius and offset right
        uint32_t innerStart = numPoints;
        for (uint32_t i = 0; i < numPoints; i++) {
            float t = static_cast<float>(i) / static_cast<float>(segments);
            float angle = static_cast<float>(M_PI) * 0.5f + static_cast<float>(M_PI) * t;  // 90° to 270°

            float x = innerOffset + std::cos(angle) * innerR;
            float y = std::sin(angle) * innerR;

            AnnotationVertex v{};
            v.position[0] = x * cosR - y * sinR;
            v.position[1] = x * sinR + y * cosR;
            v.position[2] = 0.0f;
            v.normal[0] = 0.0f;
            v.normal[1] = 0.0f;
            v.normal[2] = 1.0f;
            v.color[0] = 1.0f; v.color[1] = 1.0f; v.color[2] = 1.0f;
            v.uv[0] = 0.5f + v.position[0] / size;
            v.uv[1] = 0.5f + v.position[1] / size;
            vertices.push_back(v);
        }

        // Triangle strip connecting outer to inner
        for (uint32_t i = 0; i < segments; i++) {
            // Triangle 1: outer[i], outer[i+1], inner[i]
            indices.push_back(i);
            indices.push_back(i + 1);
            indices.push_back(innerStart + i);

            // Triangle 2: outer[i+1], inner[i+1], inner[i]
            indices.push_back(i + 1);
            indices.push_back(innerStart + i + 1);
            indices.push_back(innerStart + i);
        }
    }

    static void generateSquare(float size, std::vector<AnnotationVertex>& vertices,
                               std::vector<uint32_t>& indices) {
        vertices.clear();
        indices.clear();

        float halfSize = size * 0.5f;

        float positions[4][2] = {
            {-halfSize, halfSize},     // Top left
            {halfSize, halfSize},      // Top right
            {halfSize, -halfSize},     // Bottom right
            {-halfSize, -halfSize}     // Bottom left
        };

        for (int i = 0; i < 4; i++) {
            AnnotationVertex v{};
            v.position[0] = positions[i][0];
            v.position[1] = positions[i][1];
            v.position[2] = 0.0f;
            v.normal[0] = 0.0f;
            v.normal[1] = 0.0f;
            v.normal[2] = 1.0f;
            v.color[0] = 1.0f; v.color[1] = 1.0f; v.color[2] = 1.0f;
            v.uv[0] = 0.5f + v.position[0] / size;
            v.uv[1] = 0.5f + v.position[1] / size;
            vertices.push_back(v);
        }

        indices.push_back(0); indices.push_back(3); indices.push_back(1);
        indices.push_back(1); indices.push_back(3); indices.push_back(2);
    }

    // X made of two crossed rectangles
    static void generateCross(float size, float thickness,
                              std::vector<AnnotationVertex>& vertices,
                              std::vector<uint32_t>& indices) {
        vertices.clear();
        indices.clear();

        float halfSize = size * 0.5f;
        float halfThick = thickness * 0.5f;


        auto addRect = [&](float angle) {
            float cosA = std::cos(angle);
            float sinA = std::sin(angle);

            uint32_t baseIdx = static_cast<uint32_t>(vertices.size());

            // 4 corners of rectangle (long axis along angle direction)
            float corners[4][2] = {
                {-halfSize, -halfThick},
                {halfSize, -halfThick},
                {halfSize, halfThick},
                {-halfSize, halfThick}
            };

            for (int i = 0; i < 4; i++) {
                AnnotationVertex v{};
                v.position[0] = corners[i][0] * cosA - corners[i][1] * sinA;
                v.position[1] = corners[i][0] * sinA + corners[i][1] * cosA;
                v.position[2] = 0.0f;
                v.normal[0] = 0.0f;
                v.normal[1] = 0.0f;
                v.normal[2] = 1.0f;
                v.color[0] = 1.0f; v.color[1] = 1.0f; v.color[2] = 1.0f;
                v.uv[0] = 0.5f + v.position[0] / size;
                v.uv[1] = 0.5f + v.position[1] / size;
                vertices.push_back(v);
            }

            indices.push_back(baseIdx + 0);
            indices.push_back(baseIdx + 1);
            indices.push_back(baseIdx + 2);
            indices.push_back(baseIdx + 0);
            indices.push_back(baseIdx + 2);
            indices.push_back(baseIdx + 3);
        };

        addRect(static_cast<float>(M_PI) / 4.0f); 
        addRect(-static_cast<float>(M_PI) / 4.0f);
    }

    // Skull: round head, trapezoid jaw, two dark eyes
    static void generateSkull(float size, uint32_t segments,
                              std::vector<AnnotationVertex>& vertices,
                              std::vector<uint32_t>& indices) {
        vertices.clear();
        indices.clear();

        float headRadius = size * 0.35f;
        float jawWidth = size * 0.3f;
        float jawHeight = size * 0.25f;
        float headCenterY = size * 0.1f;

        // Head (filled circle at top)
        AnnotationVertex headCenter{};
        headCenter.position[0] = 0.0f;
        headCenter.position[1] = headCenterY;
        headCenter.position[2] = 0.0f;
        headCenter.normal[0] = 0.0f;
        headCenter.normal[1] = 0.0f;
        headCenter.normal[2] = 1.0f;
        headCenter.color[0] = 1.0f; headCenter.color[1] = 1.0f; headCenter.color[2] = 1.0f;
        headCenter.uv[0] = 0.5f; headCenter.uv[1] = 0.5f;
        vertices.push_back(headCenter);

        uint32_t headCenterIdx = 0;

        for (uint32_t i = 0; i <= segments; i++) {
            float angle = static_cast<float>(i) / static_cast<float>(segments) * 2.0f * static_cast<float>(M_PI);

            AnnotationVertex v{};
            v.position[0] = std::cos(angle) * headRadius;
            v.position[1] = headCenterY + std::sin(angle) * headRadius;
            v.position[2] = 0.0f;
            v.normal[0] = 0.0f;
            v.normal[1] = 0.0f;
            v.normal[2] = 1.0f;
            v.color[0] = 1.0f; v.color[1] = 1.0f; v.color[2] = 1.0f;
            v.uv[0] = 0.5f + v.position[0] / size;
            v.uv[1] = 0.5f + v.position[1] / size;
            vertices.push_back(v);
        }

        for (uint32_t i = 0; i < segments; i++) {
            indices.push_back(headCenterIdx);
            indices.push_back(headCenterIdx + 1 + i);
            indices.push_back(headCenterIdx + 2 + i);
        }

        // Jaw (trapezoid below head)
        uint32_t jawBaseIdx = static_cast<uint32_t>(vertices.size());

        float jawPositions[4][2] = {
            {-jawWidth * 0.6f, headCenterY - headRadius * 0.3f},  // Top left
            {jawWidth * 0.6f, headCenterY - headRadius * 0.3f},   // Top right
            {jawWidth * 0.4f, headCenterY - headRadius - jawHeight},  // Bottom right
            {-jawWidth * 0.4f, headCenterY - headRadius - jawHeight}  // Bottom left
        };

        for (int i = 0; i < 4; i++) {
            AnnotationVertex v{};
            v.position[0] = jawPositions[i][0];
            v.position[1] = jawPositions[i][1];
            v.position[2] = 0.0f;
            v.normal[0] = 0.0f;
            v.normal[1] = 0.0f;
            v.normal[2] = 1.0f;
            v.color[0] = 1.0f; v.color[1] = 1.0f; v.color[2] = 1.0f;
            v.uv[0] = 0.5f + v.position[0] / size;
            v.uv[1] = 0.5f + v.position[1] / size;
            vertices.push_back(v);
        }

        indices.push_back(jawBaseIdx + 0);
        indices.push_back(jawBaseIdx + 3);
        indices.push_back(jawBaseIdx + 1);
        indices.push_back(jawBaseIdx + 1);
        indices.push_back(jawBaseIdx + 3);
        indices.push_back(jawBaseIdx + 2);

        // Eyes
        float eyeRadius = size * 0.08f;
        float eyeOffsetX = size * 0.12f;  // Horizontal distance from center
        float eyeOffsetY = headCenterY + size * 0.05f;  // Slightly above head center
        uint32_t eyeSegments = 8;

        auto addEye = [&](float centerX, float centerY) {
            uint32_t eyeCenterIdx = static_cast<uint32_t>(vertices.size());

            AnnotationVertex eyeCenter{};
            eyeCenter.position[0] = centerX;
            eyeCenter.position[1] = centerY;
            eyeCenter.position[2] = 0.01f;  // in front of the head to avoid z-fighting
            eyeCenter.normal[0] = 0.0f;
            eyeCenter.normal[1] = 0.0f;
            eyeCenter.normal[2] = 1.0f;
            eyeCenter.color[0] = 0.0f; eyeCenter.color[1] = 0.0f; eyeCenter.color[2] = 0.0f;
            eyeCenter.uv[0] = 0.5f; eyeCenter.uv[1] = 0.5f;
            vertices.push_back(eyeCenter);

            for (uint32_t i = 0; i <= eyeSegments; i++) {
                float angle = static_cast<float>(i) / static_cast<float>(eyeSegments) * 2.0f * static_cast<float>(M_PI);

                AnnotationVertex v{};
                v.position[0] = centerX + std::cos(angle) * eyeRadius;
                v.position[1] = centerY + std::sin(angle) * eyeRadius;
                v.position[2] = 0.01f;
                v.normal[0] = 0.0f;
                v.normal[1] = 0.0f;
                v.normal[2] = 1.0f;
                v.color[0] = 0.0f; v.color[1] = 0.0f; v.color[2] = 0.0f;
                v.uv[0] = 0.5f + v.position[0] / size;
                v.uv[1] = 0.5f + v.position[1] / size;
                vertices.push_back(v);
            }

            for (uint32_t i = 0; i < eyeSegments; i++) {
                indices.push_back(eyeCenterIdx);
                indices.push_back(eyeCenterIdx + 1 + i);
                indices.push_back(eyeCenterIdx + 2 + i);
            }
        };

        addEye(-eyeOffsetX, eyeOffsetY);
        addEye(eyeOffsetX, eyeOffsetY);
    }
};
