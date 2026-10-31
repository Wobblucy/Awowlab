#pragma once
#include "SphereGeometry.h"
#include <vector>
#include <cstdint>

namespace geometry {

// Add a quad face to the geometry
// Parameters: 4 vertices (x, z, y for each) and normal (nx, nz, ny)
// Arguments are in x, z, y order, as the shape generators use
inline void addQuad(
    std::vector<Vertex3D>& vertices,
    std::vector<uint32_t>& indices,
    float x0, float z0, float y0,
    float x1, float z1, float y1,
    float x2, float z2, float y2,
    float x3, float z3, float y3,
    float nx, float nz, float ny
) {
    uint32_t base = static_cast<uint32_t>(vertices.size());

    Vertex3D v0{};
    v0.position[0] = x0; v0.position[1] = y0; v0.position[2] = z0;
    v0.normal[0] = nx; v0.normal[1] = ny; v0.normal[2] = nz;
    v0.color[0] = 1.0f; v0.color[1] = 1.0f; v0.color[2] = 1.0f;

    Vertex3D v1{};
    v1.position[0] = x1; v1.position[1] = y1; v1.position[2] = z1;
    v1.normal[0] = nx; v1.normal[1] = ny; v1.normal[2] = nz;
    v1.color[0] = 1.0f; v1.color[1] = 1.0f; v1.color[2] = 1.0f;

    Vertex3D v2{};
    v2.position[0] = x2; v2.position[1] = y2; v2.position[2] = z2;
    v2.normal[0] = nx; v2.normal[1] = ny; v2.normal[2] = nz;
    v2.color[0] = 1.0f; v2.color[1] = 1.0f; v2.color[2] = 1.0f;

    Vertex3D v3{};
    v3.position[0] = x3; v3.position[1] = y3; v3.position[2] = z3;
    v3.normal[0] = nx; v3.normal[1] = ny; v3.normal[2] = nz;
    v3.color[0] = 1.0f; v3.color[1] = 1.0f; v3.color[2] = 1.0f;

    vertices.push_back(v0);
    vertices.push_back(v1);
    vertices.push_back(v2);
    vertices.push_back(v3);

    indices.push_back(base + 0);
    indices.push_back(base + 1);
    indices.push_back(base + 2);

    indices.push_back(base + 0);
    indices.push_back(base + 2);
    indices.push_back(base + 3);
}

// Add a triangle face to the geometry
// Parameters: 3 vertices (x, z, y for each) and normal (nx, nz, ny)
inline void addTri(
    std::vector<Vertex3D>& vertices,
    std::vector<uint32_t>& indices,
    float x0, float z0, float y0,
    float x1, float z1, float y1,
    float x2, float z2, float y2,
    float nx, float nz, float ny
) {
    uint32_t base = static_cast<uint32_t>(vertices.size());

    Vertex3D v0{};
    v0.position[0] = x0; v0.position[1] = y0; v0.position[2] = z0;
    v0.normal[0] = nx; v0.normal[1] = ny; v0.normal[2] = nz;
    v0.color[0] = 1.0f; v0.color[1] = 1.0f; v0.color[2] = 1.0f;

    Vertex3D v1{};
    v1.position[0] = x1; v1.position[1] = y1; v1.position[2] = z1;
    v1.normal[0] = nx; v1.normal[1] = ny; v1.normal[2] = nz;
    v1.color[0] = 1.0f; v1.color[1] = 1.0f; v1.color[2] = 1.0f;

    Vertex3D v2{};
    v2.position[0] = x2; v2.position[1] = y2; v2.position[2] = z2;
    v2.normal[0] = nx; v2.normal[1] = ny; v2.normal[2] = nz;
    v2.color[0] = 1.0f; v2.color[1] = 1.0f; v2.color[2] = 1.0f;

    vertices.push_back(v0);
    vertices.push_back(v1);
    vertices.push_back(v2);

    indices.push_back(base + 0);
    indices.push_back(base + 1);
    indices.push_back(base + 2);
}

} // namespace geometry
