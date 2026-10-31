#include "Geometry/AnnotationDonutGeometry.h"
#include "AnnotationRenderer.h"  // For AnnotationVertex
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace annotation_geometry {

AnnotationGeometryData createDonutGeometry(uint32_t segments, float defaultInnerRadiusRatio) {
    AnnotationGeometryData data;
    auto& vertices = data.vertices;
    auto& indices = data.indices;

    // Top and bottom faces plus the inner and outer walls, each two rings of segments+1
    vertices.reserve(8 * (segments + 1));
    indices.reserve(segments * 24);

    // uv.y marks inner vertices (1) so the vertex shader can move them to the
    // requested inner radius. uv.x, the edge distance, is 0 everywhere.

    const float outerRadius = 1.0f;
    const float innerRadius = outerRadius * defaultInnerRadiusRatio;
    const float halfHeight = 0.5f;

    // Top face
    uint32_t topStart = static_cast<uint32_t>(vertices.size());
    for (uint32_t i = 0; i <= segments; ++i) {
        float angle = static_cast<float>(i) / static_cast<float>(segments) * 2.0f * static_cast<float>(M_PI);
        float cosA = std::cos(angle);
        float sinA = std::sin(angle);

        AnnotationVertex outer{};
        outer.position[0] = cosA * outerRadius;
        outer.position[1] = halfHeight;
        outer.position[2] = sinA * outerRadius;
        outer.normal[0] = 0.0f; outer.normal[1] = 1.0f; outer.normal[2] = 0.0f;
        outer.color[0] = 1.0f; outer.color[1] = 1.0f; outer.color[2] = 1.0f;
        outer.uv[0] = 0.0f;
        outer.uv[1] = 0.0f;
        vertices.push_back(outer);

        AnnotationVertex inner{};
        inner.position[0] = cosA * innerRadius;
        inner.position[1] = halfHeight;
        inner.position[2] = sinA * innerRadius;
        inner.normal[0] = 0.0f; inner.normal[1] = 1.0f; inner.normal[2] = 0.0f;
        inner.color[0] = 1.0f; inner.color[1] = 1.0f; inner.color[2] = 1.0f;
        inner.uv[0] = 0.0f;
        inner.uv[1] = 1.0f;
        vertices.push_back(inner);
    }

    // Quad strip between the two rings
    for (uint32_t i = 0; i < segments; ++i) {
        uint32_t o1 = topStart + i * 2;
        uint32_t i1 = topStart + i * 2 + 1;
        uint32_t o2 = topStart + (i + 1) * 2;
        uint32_t i2 = topStart + (i + 1) * 2 + 1;

        indices.push_back(o1); indices.push_back(i1); indices.push_back(i2);
        indices.push_back(o1); indices.push_back(i2); indices.push_back(o2);
    }

    // Bottom face
    uint32_t botStart = static_cast<uint32_t>(vertices.size());
    for (uint32_t i = 0; i <= segments; ++i) {
        float angle = static_cast<float>(i) / static_cast<float>(segments) * 2.0f * static_cast<float>(M_PI);
        float cosA = std::cos(angle);
        float sinA = std::sin(angle);

        AnnotationVertex outer{};
        outer.position[0] = cosA * outerRadius;
        outer.position[1] = -halfHeight;
        outer.position[2] = sinA * outerRadius;
        outer.normal[0] = 0.0f; outer.normal[1] = -1.0f; outer.normal[2] = 0.0f;
        outer.color[0] = 1.0f; outer.color[1] = 1.0f; outer.color[2] = 1.0f;
        outer.uv[0] = 0.0f;
        outer.uv[1] = 0.0f;
        vertices.push_back(outer);

        AnnotationVertex inner{};
        inner.position[0] = cosA * innerRadius;
        inner.position[1] = -halfHeight;
        inner.position[2] = sinA * innerRadius;
        inner.normal[0] = 0.0f; inner.normal[1] = -1.0f; inner.normal[2] = 0.0f;
        inner.color[0] = 1.0f; inner.color[1] = 1.0f; inner.color[2] = 1.0f;
        inner.uv[0] = 0.0f;
        inner.uv[1] = 1.0f;
        vertices.push_back(inner);
    }

    // Reversed winding
    for (uint32_t i = 0; i < segments; ++i) {
        uint32_t o1 = botStart + i * 2;
        uint32_t i1 = botStart + i * 2 + 1;
        uint32_t o2 = botStart + (i + 1) * 2;
        uint32_t i2 = botStart + (i + 1) * 2 + 1;

        indices.push_back(o1); indices.push_back(i2); indices.push_back(i1);
        indices.push_back(o1); indices.push_back(o2); indices.push_back(i2);
    }

    // Outer wall
    uint32_t outerWallStart = static_cast<uint32_t>(vertices.size());
    for (uint32_t i = 0; i <= segments; ++i) {
        float angle = static_cast<float>(i) / static_cast<float>(segments) * 2.0f * static_cast<float>(M_PI);
        float cosA = std::cos(angle);
        float sinA = std::sin(angle);

        AnnotationVertex top{};
        top.position[0] = cosA * outerRadius;
        top.position[1] = halfHeight;
        top.position[2] = sinA * outerRadius;
        top.normal[0] = cosA; top.normal[1] = 0.0f; top.normal[2] = sinA;
        top.color[0] = 1.0f; top.color[1] = 1.0f; top.color[2] = 1.0f;
        top.uv[0] = 0.0f;
        top.uv[1] = 0.0f;
        vertices.push_back(top);

        AnnotationVertex bot{};
        bot.position[0] = cosA * outerRadius;
        bot.position[1] = -halfHeight;
        bot.position[2] = sinA * outerRadius;
        bot.normal[0] = cosA; bot.normal[1] = 0.0f; bot.normal[2] = sinA;
        bot.color[0] = 1.0f; bot.color[1] = 1.0f; bot.color[2] = 1.0f;
        bot.uv[0] = 0.0f;
        bot.uv[1] = 0.0f;
        vertices.push_back(bot);
    }

    for (uint32_t i = 0; i < segments; ++i) {
        uint32_t top1 = outerWallStart + i * 2;
        uint32_t bot1 = outerWallStart + i * 2 + 1;
        uint32_t top2 = outerWallStart + (i + 1) * 2;
        uint32_t bot2 = outerWallStart + (i + 1) * 2 + 1;

        indices.push_back(top1); indices.push_back(bot1); indices.push_back(bot2);
        indices.push_back(top1); indices.push_back(bot2); indices.push_back(top2);
    }

    // Inner wall, facing the centre
    uint32_t innerWallStart = static_cast<uint32_t>(vertices.size());
    for (uint32_t i = 0; i <= segments; ++i) {
        float angle = static_cast<float>(i) / static_cast<float>(segments) * 2.0f * static_cast<float>(M_PI);
        float cosA = std::cos(angle);
        float sinA = std::sin(angle);

        AnnotationVertex top{};
        top.position[0] = cosA * innerRadius;
        top.position[1] = halfHeight;
        top.position[2] = sinA * innerRadius;
        top.normal[0] = -cosA; top.normal[1] = 0.0f; top.normal[2] = -sinA;
        top.color[0] = 1.0f; top.color[1] = 1.0f; top.color[2] = 1.0f;
        top.uv[0] = 0.0f;
        top.uv[1] = 1.0f;
        vertices.push_back(top);

        AnnotationVertex bot{};
        bot.position[0] = cosA * innerRadius;
        bot.position[1] = -halfHeight;
        bot.position[2] = sinA * innerRadius;
        bot.normal[0] = -cosA; bot.normal[1] = 0.0f; bot.normal[2] = -sinA;
        bot.color[0] = 1.0f; bot.color[1] = 1.0f; bot.color[2] = 1.0f;
        bot.uv[0] = 0.0f;
        bot.uv[1] = 1.0f;
        vertices.push_back(bot);
    }

    // Reversed winding so it faces inward
    for (uint32_t i = 0; i < segments; ++i) {
        uint32_t top1 = innerWallStart + i * 2;
        uint32_t bot1 = innerWallStart + i * 2 + 1;
        uint32_t top2 = innerWallStart + (i + 1) * 2;
        uint32_t bot2 = innerWallStart + (i + 1) * 2 + 1;

        indices.push_back(top1); indices.push_back(bot2); indices.push_back(bot1);
        indices.push_back(top1); indices.push_back(top2); indices.push_back(bot2);
    }

    return data;
}

} // namespace annotation_geometry
