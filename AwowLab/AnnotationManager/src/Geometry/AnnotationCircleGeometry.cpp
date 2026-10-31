#include "Geometry/AnnotationCircleGeometry.h"
#include "AnnotationRenderer.h"  // For AnnotationVertex
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace annotation_geometry {

AnnotationGeometryData createCircleGeometry(uint32_t segments) {
    AnnotationGeometryData data;
    auto& vertices = data.vertices;
    auto& indices = data.indices;

    // top centre + top ring + two side rings + bottom centre + bottom ring
    vertices.reserve(2 + 4 * (segments + 1));
    indices.reserve(segments * 12);

    const float radius = 1.0f;
    const float halfHeight = 0.5f;

    // uv.x is the distance from the edge (1 at the centre), uv.y the radial position
    uint32_t topCenterIdx = static_cast<uint32_t>(vertices.size());
    AnnotationVertex topCenter{};
    topCenter.position[0] = 0.0f;
    topCenter.position[1] = halfHeight;
    topCenter.position[2] = 0.0f;
    topCenter.normal[0] = 0.0f;
    topCenter.normal[1] = 1.0f;
    topCenter.normal[2] = 0.0f;
    topCenter.color[0] = 1.0f; topCenter.color[1] = 1.0f; topCenter.color[2] = 1.0f;
    topCenter.uv[0] = 1.0f;
    topCenter.uv[1] = 0.0f;
    vertices.push_back(topCenter);

    for (uint32_t i = 0; i <= segments; ++i) {
        float angle = static_cast<float>(i) / static_cast<float>(segments) * 2.0f * static_cast<float>(M_PI);
        AnnotationVertex v{};
        v.position[0] = std::cos(angle) * radius;
        v.position[1] = halfHeight;
        v.position[2] = std::sin(angle) * radius;
        v.normal[0] = 0.0f;
        v.normal[1] = 1.0f;
        v.normal[2] = 0.0f;
        v.color[0] = 1.0f; v.color[1] = 1.0f; v.color[2] = 1.0f;
        v.uv[0] = 0.0f;
        v.uv[1] = 1.0f;
        vertices.push_back(v);
    }

    for (uint32_t i = 0; i < segments; ++i) {
        indices.push_back(topCenterIdx);
        indices.push_back(topCenterIdx + 1 + i);
        indices.push_back(topCenterIdx + 2 + i);
    }

    uint32_t sideStart = static_cast<uint32_t>(vertices.size());
    for (uint32_t i = 0; i <= segments; ++i) {
        float angle = static_cast<float>(i) / static_cast<float>(segments) * 2.0f * static_cast<float>(M_PI);
        float cosA = std::cos(angle);
        float sinA = std::sin(angle);

        AnnotationVertex top{};
        top.position[0] = cosA * radius;
        top.position[1] = halfHeight;
        top.position[2] = sinA * radius;
        top.normal[0] = cosA;
        top.normal[1] = 0.0f;
        top.normal[2] = sinA;
        top.color[0] = 1.0f; top.color[1] = 1.0f; top.color[2] = 1.0f;
        top.uv[0] = 0.0f;
        top.uv[1] = 1.0f;
        vertices.push_back(top);

        AnnotationVertex bottom{};
        bottom.position[0] = cosA * radius;
        bottom.position[1] = -halfHeight;
        bottom.position[2] = sinA * radius;
        bottom.normal[0] = cosA;
        bottom.normal[1] = 0.0f;
        bottom.normal[2] = sinA;
        bottom.color[0] = 1.0f; bottom.color[1] = 1.0f; bottom.color[2] = 1.0f;
        bottom.uv[0] = 0.0f;
        bottom.uv[1] = 1.0f;
        vertices.push_back(bottom);
    }

    for (uint32_t i = 0; i < segments; ++i) {
        uint32_t top1 = sideStart + i * 2;
        uint32_t bot1 = sideStart + i * 2 + 1;
        uint32_t top2 = sideStart + (i + 1) * 2;
        uint32_t bot2 = sideStart + (i + 1) * 2 + 1;

        indices.push_back(top1);
        indices.push_back(bot1);
        indices.push_back(bot2);

        indices.push_back(top1);
        indices.push_back(bot2);
        indices.push_back(top2);
    }

    uint32_t botCenterIdx = static_cast<uint32_t>(vertices.size());
    AnnotationVertex botCenter{};
    botCenter.position[0] = 0.0f;
    botCenter.position[1] = -halfHeight;
    botCenter.position[2] = 0.0f;
    botCenter.normal[0] = 0.0f;
    botCenter.normal[1] = -1.0f;
    botCenter.normal[2] = 0.0f;
    botCenter.color[0] = 1.0f; botCenter.color[1] = 1.0f; botCenter.color[2] = 1.0f;
    botCenter.uv[0] = 1.0f;
    botCenter.uv[1] = 0.0f;
    vertices.push_back(botCenter);

    for (uint32_t i = 0; i <= segments; ++i) {
        float angle = static_cast<float>(i) / static_cast<float>(segments) * 2.0f * static_cast<float>(M_PI);
        AnnotationVertex v{};
        v.position[0] = std::cos(angle) * radius;
        v.position[1] = -halfHeight;
        v.position[2] = std::sin(angle) * radius;
        v.normal[0] = 0.0f;
        v.normal[1] = -1.0f;
        v.normal[2] = 0.0f;
        v.color[0] = 1.0f; v.color[1] = 1.0f; v.color[2] = 1.0f;
        v.uv[0] = 0.0f;
        v.uv[1] = 1.0f;
        vertices.push_back(v);
    }

    for (uint32_t i = 0; i < segments; ++i) {
        indices.push_back(botCenterIdx);
        indices.push_back(botCenterIdx + 2 + i);
        indices.push_back(botCenterIdx + 1 + i);
    }

    return data;
}

} // namespace annotation_geometry
