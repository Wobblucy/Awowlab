#pragma once
#include <array>
#include <optional>

// Result of a raycast hit on the floor plane
struct FloorHit {
    float worldX;
    float worldY;
    float worldZ;
};

// Handles mouse-to-world raycasting for floor plane intersection
class MouseRaycaster {
public:
    MouseRaycaster() = default;

    // Call each frame before raycasting
    void updateMatrices(const float* viewMatrix, const float* projMatrix);

    void setViewport(float width, float height);

    // Hit point on the plane Z = floorZ, or nullopt if the ray misses it
    std::optional<FloorHit> raycastToFloor(float screenX, float screenY, float floorZ = -1.0f) const;

    std::optional<FloorHit> getLastHit() const { return lastHit_; }

private:
    void screenToNDC(float screenX, float screenY, float& ndcX, float& ndcY) const;

    // Currently always returns true; a singular matrix was already replaced by identity
    bool unprojectToRay(float ndcX, float ndcY,
                        float* rayOrigin, float* rayDir) const;

    bool intersectZPlane(const float* rayOrigin, const float* rayDir,
                         float planeZ, float& hitX, float& hitY) const;

    void invertMatrix4x4(const float* m, float* inv) const;
    void multiplyMatrix4x4(const float* a, const float* b, float* result) const;
    void transformPoint(const float* matrix, const float* point, float* result) const;

private:
    float viewMatrix_[16] = {0};
    float projMatrix_[16] = {0};
    float invViewProj_[16] = {0};
    float viewportWidth_ = 1280.0f;
    float viewportHeight_ = 720.0f;
    bool matricesValid_ = false;

    mutable std::optional<FloorHit> lastHit_;
};
