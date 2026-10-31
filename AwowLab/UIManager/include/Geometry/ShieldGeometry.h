#pragma once
#include "SphereGeometry.h"
#include <vector>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Shield geometry for Tank role - classic kite/heater shield with curved front
// Oriented to stand upright (Z-up), curved surface facing -Y (towards camera)
class ShieldGeometry {
public:
    // Generate a curved shield shape (convex front like a real shield)
    // size: overall height of the shield
    static void generateShield(float size, std::vector<Vertex3D>& vertices, std::vector<uint32_t>& indices) {
        vertices.clear();
        indices.clear();

        float halfWidth = size * 0.38f;
        float halfHeight = size * 0.65f;
        float curveDepth = size * 0.18f;  // How much the shield curves outward

        // Shield profile segments
        const int widthSegments = 8;   // Horizontal divisions for curve
        const int heightSegments = 12; // Vertical divisions

        // Generate curved front surface
        // The shield curves outward in Y based on distance from center
        for (int j = 0; j <= heightSegments; ++j) {
            float v = static_cast<float>(j) / heightSegments;

            // Z position: top to bottom with pointed bottom
            float z;
            if (v < 0.7f) {
                // Upper portion: relatively straight
                z = halfHeight - v * halfHeight * 1.4f;
            } else {
                // Lower portion: tapers to point
                float t = (v - 0.7f) / 0.3f;
                z = halfHeight * (0.02f - t * 0.52f);  // Taper to point
            }

            // Width varies with height (wider at top, narrower at bottom)
            float widthScale;
            if (v < 0.15f) {
                // Top edge slightly narrower
                widthScale = 0.85f + v * 1.0f;
            } else if (v < 0.7f) {
                // Main body full width
                widthScale = 1.0f;
            } else {
                // Taper toward point
                float t = (v - 0.7f) / 0.3f;
                widthScale = 1.0f - t * 0.95f;
            }

            float rowWidth = halfWidth * widthScale;

            for (int i = 0; i <= widthSegments; ++i) {
                float u = static_cast<float>(i) / widthSegments;
                float x = -rowWidth + 2.0f * rowWidth * u;

                // Curve amount based on distance from center (both X and Z)
                float xNorm = (rowWidth > 0.01f) ? (x / rowWidth) : 0.0f;
                float zNorm = z / halfHeight;

                // Parabolic curve: maximum at center, zero at edges
                float curveX = 1.0f - xNorm * xNorm;  // Horizontal curve
                float curveZ = 1.0f - zNorm * zNorm * 0.3f;  // Slight vertical curve
                float y = -curveDepth * curveX * curveZ;  // Negative Y = facing camera

                // Calculate normal from surface gradient
                // Approximate normal by looking at curve derivatives
                float dydx = 2.0f * curveDepth * xNorm * curveZ / (rowWidth > 0.01f ? rowWidth : 1.0f);
                float dydz = 2.0f * curveDepth * curveX * zNorm * 0.3f / halfHeight;

                float nx = -dydx;
                float ny = -1.0f;  // Normal facing -Y (towards camera)
                float nz = -dydz;
                float nLen = std::sqrt(nx*nx + ny*ny + nz*nz);
                nx /= nLen; ny /= nLen; nz /= nLen;

                Vertex3D vert{};
                vert.position[0] = x;
                vert.position[1] = y;
                vert.position[2] = z;
                vert.normal[0] = nx;
                vert.normal[1] = ny;
                vert.normal[2] = nz;
                vert.color[0] = 1.0f;
                vert.color[1] = 1.0f;
                vert.color[2] = 1.0f;
                vertices.push_back(vert);
            }
        }

        for (int j = 0; j < heightSegments; ++j) {
            for (int i = 0; i < widthSegments; ++i) {
                uint32_t topLeft = j * (widthSegments + 1) + i;
                uint32_t topRight = topLeft + 1;
                uint32_t bottomLeft = (j + 1) * (widthSegments + 1) + i;
                uint32_t bottomRight = bottomLeft + 1;

                // Two triangles per quad
                indices.push_back(topLeft);
                indices.push_back(bottomLeft);
                indices.push_back(topRight);

                indices.push_back(topRight);
                indices.push_back(bottomLeft);
                indices.push_back(bottomRight);
            }
        }

        // Generate back face (flat, slightly recessed)
        uint32_t backStart = static_cast<uint32_t>(vertices.size());
        float backY = size * 0.02f;  // Behind the curved front

        for (int j = 0; j <= heightSegments; ++j) {
            float v = static_cast<float>(j) / heightSegments;

            float z;
            if (v < 0.7f) {
                z = halfHeight - v * halfHeight * 1.4f;
            } else {
                float t = (v - 0.7f) / 0.3f;
                z = halfHeight * (0.02f - t * 0.52f);
            }

            float widthScale;
            if (v < 0.15f) {
                widthScale = 0.85f + v * 1.0f;
            } else if (v < 0.7f) {
                widthScale = 1.0f;
            } else {
                float t = (v - 0.7f) / 0.3f;
                widthScale = 1.0f - t * 0.95f;
            }

            float rowWidth = halfWidth * widthScale;

            for (int i = 0; i <= widthSegments; ++i) {
                float u = static_cast<float>(i) / widthSegments;
                float x = -rowWidth + 2.0f * rowWidth * u;

                Vertex3D vert{};
                vert.position[0] = x;
                vert.position[1] = backY;
                vert.position[2] = z;
                vert.normal[0] = 0.0f;
                vert.normal[1] = 1.0f;  // Normal facing +Y (away from camera)
                vert.normal[2] = 0.0f;
                vert.color[0] = 1.0f;
                vert.color[1] = 1.0f;
                vert.color[2] = 1.0f;
                vertices.push_back(vert);
            }
        }

        // Generate back face indices (reversed winding)
        for (int j = 0; j < heightSegments; ++j) {
            for (int i = 0; i < widthSegments; ++i) {
                uint32_t topLeft = backStart + j * (widthSegments + 1) + i;
                uint32_t topRight = topLeft + 1;
                uint32_t bottomLeft = backStart + (j + 1) * (widthSegments + 1) + i;
                uint32_t bottomRight = bottomLeft + 1;

                // Reversed winding for back face
                indices.push_back(topLeft);
                indices.push_back(topRight);
                indices.push_back(bottomLeft);

                indices.push_back(topRight);
                indices.push_back(bottomRight);
                indices.push_back(bottomLeft);
            }
        }

        // Generate rim (edge) connecting front and back
        uint32_t rimStart = static_cast<uint32_t>(vertices.size());

        // Collect edge vertices from front surface
        std::vector<std::pair<int, int>> edgeIndices;  // (j, i) pairs for edge vertices

        for (int j = 0; j <= heightSegments; ++j) {
            // Left edge
            edgeIndices.push_back({j, 0});
        }
        // Bottom edge (skip corners already added)
        for (int i = 1; i <= widthSegments; ++i) {
            edgeIndices.push_back({heightSegments, i});
        }
        // Right edge going up (skip corner already added)
        for (int j = heightSegments - 1; j >= 0; --j) {
            edgeIndices.push_back({j, widthSegments});
        }
        // Top edge going left (skip corners)
        for (int i = widthSegments - 1; i >= 1; --i) {
            edgeIndices.push_back({0, i});
        }

        size_t numEdge = edgeIndices.size();
        for (size_t e = 0; e < numEdge; ++e) {
            int j = edgeIndices[e].first;
            int i = edgeIndices[e].second;

            uint32_t frontIdx = j * (widthSegments + 1) + i;
            uint32_t backIdx = backStart + j * (widthSegments + 1) + i;

            const Vertex3D& frontVert = vertices[frontIdx];
            const Vertex3D& backVert = vertices[backIdx];

            // Calculate outward normal for rim
            // Use position relative to center as approximate outward direction
            float cx = frontVert.position[0];
            float cz = frontVert.position[2];
            float len = std::sqrt(cx*cx + cz*cz);
            float nx = (len > 0.01f) ? cx / len : 0.0f;
            float nz = (len > 0.01f) ? cz / len : 0.0f;

            // Front rim vertex
            Vertex3D rimFront = frontVert;
            rimFront.normal[0] = nx;
            rimFront.normal[1] = 0.0f;
            rimFront.normal[2] = nz;
            vertices.push_back(rimFront);

            // Back rim vertex
            Vertex3D rimBack = backVert;
            rimBack.normal[0] = nx;
            rimBack.normal[1] = 0.0f;
            rimBack.normal[2] = nz;
            vertices.push_back(rimBack);
        }

        for (size_t e = 0; e < numEdge; ++e) {
            size_t next = (e + 1) % numEdge;

            uint32_t f0 = rimStart + static_cast<uint32_t>(e * 2);      // Front current
            uint32_t b0 = rimStart + static_cast<uint32_t>(e * 2 + 1);  // Back current
            uint32_t f1 = rimStart + static_cast<uint32_t>(next * 2);   // Front next
            uint32_t b1 = rimStart + static_cast<uint32_t>(next * 2 + 1); // Back next

            // Two triangles for rim quad
            indices.push_back(f0);
            indices.push_back(f1);
            indices.push_back(b0);

            indices.push_back(b0);
            indices.push_back(f1);
            indices.push_back(b1);
        }
    }
};
