#include "Geometry/AnnotationRectangleGeometry.h"
#include "AnnotationRenderer.h"  // For AnnotationVertex
#include <algorithm>

namespace annotation_geometry {

AnnotationGeometryData createRectangleGeometry() {
    AnnotationGeometryData data;
    auto& vertices = data.vertices;
    auto& indices = data.indices;

    // Unit box centred on the origin; the model matrix scales it
    const float halfWidth = 0.5f;   // X direction
    const float halfLength = 0.5f;  // Z direction
    const float halfHeight = 0.5f;  // Y direction (thickness)

    // 0 at the edges, 1 at the centre
    auto calcEdgeDistance = [halfWidth, halfLength](float x, float z) {
        float distFromX = halfWidth - std::abs(x);   // Distance from left/right edge
        float distFromZ = halfLength - std::abs(z);  // Distance from front/back edge
        float minDist = std::min(distFromX, distFromZ);
        float maxPossible = std::min(halfWidth, halfLength);
        return minDist / maxPossible;
    };

    // The top face gets a centre vertex so the edge-distance UV interpolates
    AnnotationVertex v0{}, v1{}, v2{}, v3{}, vCenter{};

    v0.position[0] = -halfWidth; v0.position[1] = halfHeight; v0.position[2] = -halfLength;
    v1.position[0] = halfWidth;  v1.position[1] = halfHeight; v1.position[2] = -halfLength;
    v2.position[0] = halfWidth;  v2.position[1] = halfHeight; v2.position[2] = halfLength;
    v3.position[0] = -halfWidth; v3.position[1] = halfHeight; v3.position[2] = halfLength;

    vCenter.position[0] = 0.0f; vCenter.position[1] = halfHeight; vCenter.position[2] = 0.0f;

    for (auto* v : {&v0, &v1, &v2, &v3, &vCenter}) {
        v->normal[0] = 0.0f; v->normal[1] = 1.0f; v->normal[2] = 0.0f;
        v->color[0] = 1.0f; v->color[1] = 1.0f; v->color[2] = 1.0f;
    }

    // uv.x = edge distance, uv.y = position along the length
    v0.uv[0] = calcEdgeDistance(-halfWidth, -halfLength); v0.uv[1] = 0.0f;
    v1.uv[0] = calcEdgeDistance(halfWidth, -halfLength);  v1.uv[1] = 0.0f;
    v2.uv[0] = calcEdgeDistance(halfWidth, halfLength);   v2.uv[1] = 1.0f;
    v3.uv[0] = calcEdgeDistance(-halfWidth, halfLength);  v3.uv[1] = 1.0f;
    vCenter.uv[0] = calcEdgeDistance(0.0f, 0.0f);         vCenter.uv[1] = 0.5f;

    uint32_t baseIdx = static_cast<uint32_t>(vertices.size());
    vertices.push_back(v0);      // 0
    vertices.push_back(v1);      // 1
    vertices.push_back(v2);      // 2
    vertices.push_back(v3);      // 3
    vertices.push_back(vCenter); // 4

    // Fan from the centre
    indices.push_back(baseIdx + 4); indices.push_back(baseIdx + 0); indices.push_back(baseIdx + 1);
    indices.push_back(baseIdx + 4); indices.push_back(baseIdx + 1); indices.push_back(baseIdx + 2);
    indices.push_back(baseIdx + 4); indices.push_back(baseIdx + 2); indices.push_back(baseIdx + 3);
    indices.push_back(baseIdx + 4); indices.push_back(baseIdx + 3); indices.push_back(baseIdx + 0);

    // Bottom face, same layout
    AnnotationVertex v4{}, v5{}, v6{}, v7{}, vCenterBot{};

    v4.position[0] = -halfWidth; v4.position[1] = -halfHeight; v4.position[2] = -halfLength;
    v5.position[0] = halfWidth;  v5.position[1] = -halfHeight; v5.position[2] = -halfLength;
    v6.position[0] = halfWidth;  v6.position[1] = -halfHeight; v6.position[2] = halfLength;
    v7.position[0] = -halfWidth; v7.position[1] = -halfHeight; v7.position[2] = halfLength;
    vCenterBot.position[0] = 0.0f; vCenterBot.position[1] = -halfHeight; vCenterBot.position[2] = 0.0f;

    for (auto* v : {&v4, &v5, &v6, &v7, &vCenterBot}) {
        v->normal[0] = 0.0f; v->normal[1] = -1.0f; v->normal[2] = 0.0f;
        v->color[0] = 1.0f; v->color[1] = 1.0f; v->color[2] = 1.0f;
    }

    v4.uv[0] = calcEdgeDistance(-halfWidth, -halfLength); v4.uv[1] = 0.0f;
    v5.uv[0] = calcEdgeDistance(halfWidth, -halfLength);  v5.uv[1] = 0.0f;
    v6.uv[0] = calcEdgeDistance(halfWidth, halfLength);   v6.uv[1] = 1.0f;
    v7.uv[0] = calcEdgeDistance(-halfWidth, halfLength);  v7.uv[1] = 1.0f;
    vCenterBot.uv[0] = calcEdgeDistance(0.0f, 0.0f);      vCenterBot.uv[1] = 0.5f;

    baseIdx = static_cast<uint32_t>(vertices.size());
    vertices.push_back(v4);         // 0
    vertices.push_back(v5);         // 1
    vertices.push_back(v6);         // 2
    vertices.push_back(v7);         // 3
    vertices.push_back(vCenterBot); // 4

    // Reversed winding
    indices.push_back(baseIdx + 4); indices.push_back(baseIdx + 1); indices.push_back(baseIdx + 0);
    indices.push_back(baseIdx + 4); indices.push_back(baseIdx + 2); indices.push_back(baseIdx + 1);
    indices.push_back(baseIdx + 4); indices.push_back(baseIdx + 3); indices.push_back(baseIdx + 2);
    indices.push_back(baseIdx + 4); indices.push_back(baseIdx + 0); indices.push_back(baseIdx + 3);

    return data;
}

} // namespace annotation_geometry
