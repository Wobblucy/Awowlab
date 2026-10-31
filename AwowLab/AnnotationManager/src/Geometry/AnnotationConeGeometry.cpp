#include "Geometry/AnnotationConeGeometry.h"
#include "AnnotationRenderer.h"  // For AnnotationVertex
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace annotation_geometry {

AnnotationGeometryData createConeGeometry(uint32_t segments, float defaultAngleDegrees) {
    AnnotationGeometryData data;
    auto& vertices = data.vertices;
    auto& indices = data.indices;

    // two apexes + top and bottom arcs + arc wall (two rings) + left/right edge walls
    vertices.reserve(2 + 4 * (segments + 1) + 8);
    indices.reserve(segments * 12 + 12);

    // Apex at the origin, opening along +Z, height in Y
    const float defaultAngle = defaultAngleDegrees * static_cast<float>(M_PI) / 180.0f;
    const float halfAngle = defaultAngle / 2.0f;
    const float length = 1.0f;
    const float halfHeight = 0.5f;

    uint32_t apexTopIdx = static_cast<uint32_t>(vertices.size());
    AnnotationVertex apexTop{};
    apexTop.position[0] = 0.0f;
    apexTop.position[1] = halfHeight;
    apexTop.position[2] = 0.0f;
    apexTop.normal[0] = 0.0f; apexTop.normal[1] = 1.0f; apexTop.normal[2] = 0.0f;
    apexTop.color[0] = 1.0f; apexTop.color[1] = 1.0f; apexTop.color[2] = 1.0f;
    apexTop.uv[0] = 0.0f;
    apexTop.uv[1] = 0.0f;
    vertices.push_back(apexTop);

    uint32_t arcTopStart = static_cast<uint32_t>(vertices.size());
    for (uint32_t i = 0; i <= segments; ++i) {
        float t = static_cast<float>(i) / static_cast<float>(segments);
        float angle = -halfAngle + t * defaultAngle;

        AnnotationVertex v{};
        v.position[0] = std::sin(angle) * length;
        v.position[1] = halfHeight;
        v.position[2] = std::cos(angle) * length;
        v.normal[0] = 0.0f; v.normal[1] = 1.0f; v.normal[2] = 0.0f;
        v.color[0] = 1.0f; v.color[1] = 1.0f; v.color[2] = 1.0f;
        v.uv[0] = 0.0f;
        v.uv[1] = 1.0f;
        vertices.push_back(v);
    }

    for (uint32_t i = 0; i < segments; ++i) {
        indices.push_back(apexTopIdx);
        indices.push_back(arcTopStart + i);
        indices.push_back(arcTopStart + i + 1);
    }

    // Bottom face, normals flipped
    uint32_t apexBotIdx = static_cast<uint32_t>(vertices.size());
    AnnotationVertex apexBot{};
    apexBot.position[0] = 0.0f;
    apexBot.position[1] = -halfHeight;
    apexBot.position[2] = 0.0f;
    apexBot.normal[0] = 0.0f; apexBot.normal[1] = -1.0f; apexBot.normal[2] = 0.0f;
    apexBot.color[0] = 1.0f; apexBot.color[1] = 1.0f; apexBot.color[2] = 1.0f;
    apexBot.uv[0] = 0.0f;
    apexBot.uv[1] = 0.0f;
    vertices.push_back(apexBot);

    uint32_t arcBotStart = static_cast<uint32_t>(vertices.size());
    for (uint32_t i = 0; i <= segments; ++i) {
        float t = static_cast<float>(i) / static_cast<float>(segments);
        float angle = -halfAngle + t * defaultAngle;

        AnnotationVertex v{};
        v.position[0] = std::sin(angle) * length;
        v.position[1] = -halfHeight;
        v.position[2] = std::cos(angle) * length;
        v.normal[0] = 0.0f; v.normal[1] = -1.0f; v.normal[2] = 0.0f;
        v.color[0] = 1.0f; v.color[1] = 1.0f; v.color[2] = 1.0f;
        v.uv[0] = 0.0f;
        v.uv[1] = 1.0f;
        vertices.push_back(v);
    }

    // Reversed winding
    for (uint32_t i = 0; i < segments; ++i) {
        indices.push_back(apexBotIdx);
        indices.push_back(arcBotStart + i + 1);
        indices.push_back(arcBotStart + i);
    }

    // Curved wall along the arc
    uint32_t sideArcStart = static_cast<uint32_t>(vertices.size());
    for (uint32_t i = 0; i <= segments; ++i) {
        float t = static_cast<float>(i) / static_cast<float>(segments);
        float angle = -halfAngle + t * defaultAngle;
        float cosA = std::cos(angle);
        float sinA = std::sin(angle);

        AnnotationVertex top{};
        top.position[0] = sinA * length;
        top.position[1] = halfHeight;
        top.position[2] = cosA * length;
        top.normal[0] = sinA; top.normal[1] = 0.0f; top.normal[2] = cosA;
        top.color[0] = 1.0f; top.color[1] = 1.0f; top.color[2] = 1.0f;
        top.uv[0] = 0.0f;
        top.uv[1] = 1.0f;
        vertices.push_back(top);

        AnnotationVertex bot{};
        bot.position[0] = sinA * length;
        bot.position[1] = -halfHeight;
        bot.position[2] = cosA * length;
        bot.normal[0] = sinA; bot.normal[1] = 0.0f; bot.normal[2] = cosA;
        bot.color[0] = 1.0f; bot.color[1] = 1.0f; bot.color[2] = 1.0f;
        bot.uv[0] = 0.0f;
        bot.uv[1] = 1.0f;
        vertices.push_back(bot);
    }

    for (uint32_t i = 0; i < segments; ++i) {
        uint32_t top1 = sideArcStart + i * 2;
        uint32_t bot1 = sideArcStart + i * 2 + 1;
        uint32_t top2 = sideArcStart + (i + 1) * 2;
        uint32_t bot2 = sideArcStart + (i + 1) * 2 + 1;

        indices.push_back(top1);
        indices.push_back(bot1);
        indices.push_back(bot2);

        indices.push_back(top1);
        indices.push_back(bot2);
        indices.push_back(top2);
    }

    // Left straight wall, apex to first arc point
    {
        uint32_t leftStart = static_cast<uint32_t>(vertices.size());
        float angle = -halfAngle;
        float cosA = std::cos(angle);
        float sinA = std::sin(angle);
        // Outward normal
        float nx = -cosA;
        float nz = sinA;

        AnnotationVertex v0{}, v1{}, v2{}, v3{};
        v0.position[0] = 0.0f; v0.position[1] = halfHeight; v0.position[2] = 0.0f;
        v1.position[0] = sinA * length; v1.position[1] = halfHeight; v1.position[2] = cosA * length;
        v2.position[0] = sinA * length; v2.position[1] = -halfHeight; v2.position[2] = cosA * length;
        v3.position[0] = 0.0f; v3.position[1] = -halfHeight; v3.position[2] = 0.0f;

        for (auto* v : {&v0, &v1, &v2, &v3}) {
            v->normal[0] = nx; v->normal[1] = 0.0f; v->normal[2] = nz;
            v->color[0] = 1.0f; v->color[1] = 1.0f; v->color[2] = 1.0f;
        }
        v0.uv[0] = 0.0f; v0.uv[1] = 0.0f;
        v1.uv[0] = 0.0f; v1.uv[1] = 1.0f;
        v2.uv[0] = 0.0f; v2.uv[1] = 1.0f;
        v3.uv[0] = 0.0f; v3.uv[1] = 0.0f;

        vertices.push_back(v0);
        vertices.push_back(v1);
        vertices.push_back(v2);
        vertices.push_back(v3);

        indices.push_back(leftStart);
        indices.push_back(leftStart + 1);
        indices.push_back(leftStart + 2);
        indices.push_back(leftStart);
        indices.push_back(leftStart + 2);
        indices.push_back(leftStart + 3);
    }

    // Right straight wall, apex to last arc point
    {
        uint32_t rightStart = static_cast<uint32_t>(vertices.size());
        float angle = halfAngle;
        float cosA = std::cos(angle);
        float sinA = std::sin(angle);
        // Outward normal
        float nx = cosA;
        float nz = -sinA;

        AnnotationVertex v0{}, v1{}, v2{}, v3{};
        v0.position[0] = 0.0f; v0.position[1] = halfHeight; v0.position[2] = 0.0f;
        v1.position[0] = 0.0f; v1.position[1] = -halfHeight; v1.position[2] = 0.0f;
        v2.position[0] = sinA * length; v2.position[1] = -halfHeight; v2.position[2] = cosA * length;
        v3.position[0] = sinA * length; v3.position[1] = halfHeight; v3.position[2] = cosA * length;

        for (auto* v : {&v0, &v1, &v2, &v3}) {
            v->normal[0] = nx; v->normal[1] = 0.0f; v->normal[2] = nz;
            v->color[0] = 1.0f; v->color[1] = 1.0f; v->color[2] = 1.0f;
        }
        v0.uv[0] = 0.0f; v0.uv[1] = 0.0f;
        v1.uv[0] = 0.0f; v1.uv[1] = 0.0f;
        v2.uv[0] = 0.0f; v2.uv[1] = 1.0f;
        v3.uv[0] = 0.0f; v3.uv[1] = 1.0f;

        vertices.push_back(v0);
        vertices.push_back(v1);
        vertices.push_back(v2);
        vertices.push_back(v3);

        indices.push_back(rightStart);
        indices.push_back(rightStart + 1);
        indices.push_back(rightStart + 2);
        indices.push_back(rightStart);
        indices.push_back(rightStart + 2);
        indices.push_back(rightStart + 3);
    }

    return data;
}

} // namespace annotation_geometry
