#pragma once
#include "SphereGeometry.h"
#include <vector>
#include <algorithm>

class PlaneGeometry {
public:
    // Textured floor for world map PNGs, whose bounds come from the CSV
    // (cropped to content). Corners may be given in any order.
    // textureWidth/Height are currently unused.
    static void generateWorldBoundsPlane(float x1, float y1, float x2, float y2,
                                          uint32_t textureWidth, uint32_t textureHeight,
                                          std::vector<Vertex3D>& vertices,
                                          std::vector<uint32_t>& indices) {
        vertices.clear();
        indices.clear();

        float minX = std::min(x1, x2);
        float maxX = std::max(x1, x2);
        float minY = std::min(y1, y2);
        float maxY = std::max(y1, y2);

        (void)textureWidth; (void)textureHeight;

        // World map PNGs are landscape while the world bounds are portrait, so
        // the UVs are turned 90° clockwise: new_U = old_V, new_V = 1 - old_U

        // v0: world SW corner (minX, minY)
        Vertex3D v0{};
        v0.position[0] = minX;
        v0.position[1] = minY;
        v0.position[2] = -1.0f;
        v0.normal[0] = 0.0f; v0.normal[1] = 0.0f; v0.normal[2] = 1.0f;
        v0.color[0] = 1.0f; v0.color[1] = 1.0f; v0.color[2] = 1.0f;
        v0.uv[0] = 1.0f; v0.uv[1] = 1.0f;  // 90° CW: (0,1) → (1,1)

        // v1: world SE corner (maxX, minY)
        Vertex3D v1{};
        v1.position[0] = maxX;
        v1.position[1] = minY;
        v1.position[2] = -1.0f;
        v1.normal[0] = 0.0f; v1.normal[1] = 0.0f; v1.normal[2] = 1.0f;
        v1.color[0] = 1.0f; v1.color[1] = 1.0f; v1.color[2] = 1.0f;
        v1.uv[0] = 1.0f; v1.uv[1] = 0.0f;  // 90° CW: (1,1) → (1,0)

        // v2: world NW corner (minX, maxY)
        Vertex3D v2{};
        v2.position[0] = minX;
        v2.position[1] = maxY;
        v2.position[2] = -1.0f;
        v2.normal[0] = 0.0f; v2.normal[1] = 0.0f; v2.normal[2] = 1.0f;
        v2.color[0] = 1.0f; v2.color[1] = 1.0f; v2.color[2] = 1.0f;
        v2.uv[0] = 0.0f; v2.uv[1] = 1.0f;  // 90° CW: (0,0) → (0,1)

        // v3: world NE corner (maxX, maxY)
        Vertex3D v3{};
        v3.position[0] = maxX;
        v3.position[1] = maxY;
        v3.position[2] = -1.0f;
        v3.normal[0] = 0.0f; v3.normal[1] = 0.0f; v3.normal[2] = 1.0f;
        v3.color[0] = 1.0f; v3.color[1] = 1.0f; v3.color[2] = 1.0f;
        v3.uv[0] = 0.0f; v3.uv[1] = 0.0f;  // 90° CW: (1,0) → (0,0)

        vertices.push_back(v0);
        vertices.push_back(v1);
        vertices.push_back(v2);
        vertices.push_back(v3);

        // Two triangles, counter-clockwise seen from +Z
        indices.push_back(0);
        indices.push_back(1);
        indices.push_back(2);

        indices.push_back(1);
        indices.push_back(3);
        indices.push_back(2);
    }

    // Textured floor for encounter textures rendered in Blender. The UVs turn
    // 90° counter-clockwise to undo the Blender/WoW axis swap.
    static void generateEncounterPlane(float x1, float y1, float x2, float y2,
                                        std::vector<Vertex3D>& vertices,
                                        std::vector<uint32_t>& indices) {
        vertices.clear();
        indices.clear();

        float minX = std::min(x1, x2);
        float maxX = std::max(x1, x2);
        float minY = std::min(y1, y2);
        float maxY = std::max(y1, y2);

        // new_U = 1 - old_V, new_V = old_U

        // v0: SW corner (minX, minY)
        Vertex3D v0{};
        v0.position[0] = minX;
        v0.position[1] = minY;
        v0.position[2] = -1.0f;
        v0.normal[0] = 0.0f; v0.normal[1] = 0.0f; v0.normal[2] = 1.0f;
        v0.color[0] = 1.0f; v0.color[1] = 1.0f; v0.color[2] = 1.0f;
        v0.uv[0] = 0.0f; v0.uv[1] = 0.0f;

        // v1: SE corner (maxX, minY)
        Vertex3D v1{};
        v1.position[0] = maxX;
        v1.position[1] = minY;
        v1.position[2] = -1.0f;
        v1.normal[0] = 0.0f; v1.normal[1] = 0.0f; v1.normal[2] = 1.0f;
        v1.color[0] = 1.0f; v1.color[1] = 1.0f; v1.color[2] = 1.0f;
        v1.uv[0] = 0.0f; v1.uv[1] = 1.0f;

        // v2: NW corner (minX, maxY)
        Vertex3D v2{};
        v2.position[0] = minX;
        v2.position[1] = maxY;
        v2.position[2] = -1.0f;
        v2.normal[0] = 0.0f; v2.normal[1] = 0.0f; v2.normal[2] = 1.0f;
        v2.color[0] = 1.0f; v2.color[1] = 1.0f; v2.color[2] = 1.0f;
        v2.uv[0] = 1.0f; v2.uv[1] = 0.0f;

        // v3: NE corner (maxX, maxY)
        Vertex3D v3{};
        v3.position[0] = maxX;
        v3.position[1] = maxY;
        v3.position[2] = -1.0f;
        v3.normal[0] = 0.0f; v3.normal[1] = 0.0f; v3.normal[2] = 1.0f;
        v3.color[0] = 1.0f; v3.color[1] = 1.0f; v3.color[2] = 1.0f;
        v3.uv[0] = 1.0f; v3.uv[1] = 1.0f;

        vertices.push_back(v0);
        vertices.push_back(v1);
        vertices.push_back(v2);
        vertices.push_back(v3);

        // Two triangles, counter-clockwise seen from +Z
        indices.push_back(0);
        indices.push_back(1);
        indices.push_back(2);
        indices.push_back(1);
        indices.push_back(3);
        indices.push_back(2);
    }
};
