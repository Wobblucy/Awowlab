#include "Input/MouseRaycaster.h"
#include <cmath>
#include <cstring>

void MouseRaycaster::updateMatrices(const float* viewMatrix, const float* projMatrix) {
    std::memcpy(viewMatrix_, viewMatrix, sizeof(float) * 16);
    std::memcpy(projMatrix_, projMatrix, sizeof(float) * 16);

    float viewProj[16];
    multiplyMatrix4x4(projMatrix_, viewMatrix_, viewProj);
    invertMatrix4x4(viewProj, invViewProj_);
    matricesValid_ = true;
}

void MouseRaycaster::setViewport(float width, float height) {
    viewportWidth_ = width;
    viewportHeight_ = height;
}

std::optional<FloorHit> MouseRaycaster::raycastToFloor(float screenX, float screenY, float floorZ) const {
    if (!matricesValid_) {
        lastHit_ = std::nullopt;
        return std::nullopt;
    }

    float ndcX, ndcY;
    screenToNDC(screenX, screenY, ndcX, ndcY);

    float rayOrigin[3], rayDir[3];
    if (!unprojectToRay(ndcX, ndcY, rayOrigin, rayDir)) {
        lastHit_ = std::nullopt;
        return std::nullopt;
    }

    float hitX, hitY;
    if (intersectZPlane(rayOrigin, rayDir, floorZ, hitX, hitY)) {
        FloorHit hit{hitX, hitY, floorZ};
        lastHit_ = hit;
        return hit;
    }

    lastHit_ = std::nullopt;
    return std::nullopt;
}

void MouseRaycaster::screenToNDC(float screenX, float screenY, float& ndcX, float& ndcY) const {
    // No Y flip here: the projection matrix already flips Y, so NDC -1 is the top
    ndcX = (2.0f * screenX / viewportWidth_) - 1.0f;
    ndcY = (2.0f * screenY / viewportHeight_) - 1.0f;
}

bool MouseRaycaster::unprojectToRay(float ndcX, float ndcY,
                                     float* rayOrigin, float* rayDir) const {
    // Vulkan depth runs 0 (near) to 1 (far)
    float nearPoint[4] = {ndcX, ndcY, 0.0f, 1.0f};
    float farPoint[4] = {ndcX, ndcY, 1.0f, 1.0f};

    float nearWorld[4], farWorld[4];
    transformPoint(invViewProj_, nearPoint, nearWorld);
    transformPoint(invViewProj_, farPoint, farWorld);

    const float minW = 0.0000001f;
    if (std::abs(nearWorld[3]) > minW) {
        nearWorld[0] /= nearWorld[3];
        nearWorld[1] /= nearWorld[3];
        nearWorld[2] /= nearWorld[3];
    }
    if (std::abs(farWorld[3]) > minW) {
        farWorld[0] /= farWorld[3];
        farWorld[1] /= farWorld[3];
        farWorld[2] /= farWorld[3];
    }

    rayOrigin[0] = nearWorld[0];
    rayOrigin[1] = nearWorld[1];
    rayOrigin[2] = nearWorld[2];

    rayDir[0] = farWorld[0] - nearWorld[0];
    rayDir[1] = farWorld[1] - nearWorld[1];
    rayDir[2] = farWorld[2] - nearWorld[2];

    float len = std::sqrt(rayDir[0]*rayDir[0] + rayDir[1]*rayDir[1] + rayDir[2]*rayDir[2]);
    if (len > 0.0001f) {
        rayDir[0] /= len;
        rayDir[1] /= len;
        rayDir[2] /= len;
    }

    return true;
}

bool MouseRaycaster::intersectZPlane(const float* rayOrigin, const float* rayDir,
                                      float planeZ, float& hitX, float& hitY) const {
    if (std::abs(rayDir[2]) < 0.0001f) {
        return false;
    }

    float t = (planeZ - rayOrigin[2]) / rayDir[2];

    if (t < 0.0f) {
        return false;
    }

    hitX = rayOrigin[0] + t * rayDir[0];
    hitY = rayOrigin[1] + t * rayDir[1];
    return true;
}

void MouseRaycaster::transformPoint(const float* matrix, const float* point, float* result) const {
    // Column-major
    result[0] = matrix[0]*point[0] + matrix[4]*point[1] + matrix[8]*point[2] + matrix[12]*point[3];
    result[1] = matrix[1]*point[0] + matrix[5]*point[1] + matrix[9]*point[2] + matrix[13]*point[3];
    result[2] = matrix[2]*point[0] + matrix[6]*point[1] + matrix[10]*point[2] + matrix[14]*point[3];
    result[3] = matrix[3]*point[0] + matrix[7]*point[1] + matrix[11]*point[2] + matrix[15]*point[3];
}

void MouseRaycaster::multiplyMatrix4x4(const float* a, const float* b, float* result) const {
    for (int col = 0; col < 4; col++) {
        for (int row = 0; row < 4; row++) {
            result[col * 4 + row] =
                a[0 * 4 + row] * b[col * 4 + 0] +
                a[1 * 4 + row] * b[col * 4 + 1] +
                a[2 * 4 + row] * b[col * 4 + 2] +
                a[3 * 4 + row] * b[col * 4 + 3];
        }
    }
}

void MouseRaycaster::invertMatrix4x4(const float* m, float* inv) const {
    // Cofactor expansion
    float det;
    int i;

    inv[0] = m[5]  * m[10] * m[15] -
             m[5]  * m[11] * m[14] -
             m[9]  * m[6]  * m[15] +
             m[9]  * m[7]  * m[14] +
             m[13] * m[6]  * m[11] -
             m[13] * m[7]  * m[10];

    inv[4] = -m[4]  * m[10] * m[15] +
              m[4]  * m[11] * m[14] +
              m[8]  * m[6]  * m[15] -
              m[8]  * m[7]  * m[14] -
              m[12] * m[6]  * m[11] +
              m[12] * m[7]  * m[10];

    inv[8] = m[4]  * m[9] * m[15] -
             m[4]  * m[11] * m[13] -
             m[8]  * m[5] * m[15] +
             m[8]  * m[7] * m[13] +
             m[12] * m[5] * m[11] -
             m[12] * m[7] * m[9];

    inv[12] = -m[4]  * m[9] * m[14] +
               m[4]  * m[10] * m[13] +
               m[8]  * m[5] * m[14] -
               m[8]  * m[6] * m[13] -
               m[12] * m[5] * m[10] +
               m[12] * m[6] * m[9];

    inv[1] = -m[1]  * m[10] * m[15] +
              m[1]  * m[11] * m[14] +
              m[9]  * m[2] * m[15] -
              m[9]  * m[3] * m[14] -
              m[13] * m[2] * m[11] +
              m[13] * m[3] * m[10];

    inv[5] = m[0]  * m[10] * m[15] -
             m[0]  * m[11] * m[14] -
             m[8]  * m[2] * m[15] +
             m[8]  * m[3] * m[14] +
             m[12] * m[2] * m[11] -
             m[12] * m[3] * m[10];

    inv[9] = -m[0]  * m[9] * m[15] +
              m[0]  * m[11] * m[13] +
              m[8]  * m[1] * m[15] -
              m[8]  * m[3] * m[13] -
              m[12] * m[1] * m[11] +
              m[12] * m[3] * m[9];

    inv[13] = m[0]  * m[9] * m[14] -
              m[0]  * m[10] * m[13] -
              m[8]  * m[1] * m[14] +
              m[8]  * m[2] * m[13] +
              m[12] * m[1] * m[10] -
              m[12] * m[2] * m[9];

    inv[2] = m[1]  * m[6] * m[15] -
             m[1]  * m[7] * m[14] -
             m[5]  * m[2] * m[15] +
             m[5]  * m[3] * m[14] +
             m[13] * m[2] * m[7] -
             m[13] * m[3] * m[6];

    inv[6] = -m[0]  * m[6] * m[15] +
              m[0]  * m[7] * m[14] +
              m[4]  * m[2] * m[15] -
              m[4]  * m[3] * m[14] -
              m[12] * m[2] * m[7] +
              m[12] * m[3] * m[6];

    inv[10] = m[0]  * m[5] * m[15] -
              m[0]  * m[7] * m[13] -
              m[4]  * m[1] * m[15] +
              m[4]  * m[3] * m[13] +
              m[12] * m[1] * m[7] -
              m[12] * m[3] * m[5];

    inv[14] = -m[0]  * m[5] * m[14] +
               m[0]  * m[6] * m[13] +
               m[4]  * m[1] * m[14] -
               m[4]  * m[2] * m[13] -
               m[12] * m[1] * m[6] +
               m[12] * m[2] * m[5];

    inv[3] = -m[1] * m[6] * m[11] +
              m[1] * m[7] * m[10] +
              m[5] * m[2] * m[11] -
              m[5] * m[3] * m[10] -
              m[9] * m[2] * m[7] +
              m[9] * m[3] * m[6];

    inv[7] = m[0] * m[6] * m[11] -
             m[0] * m[7] * m[10] -
             m[4] * m[2] * m[11] +
             m[4] * m[3] * m[10] +
             m[8] * m[2] * m[7] -
             m[8] * m[3] * m[6];

    inv[11] = -m[0] * m[5] * m[11] +
               m[0] * m[7] * m[9] +
               m[4] * m[1] * m[11] -
               m[4] * m[3] * m[9] -
               m[8] * m[1] * m[7] +
               m[8] * m[3] * m[5];

    inv[15] = m[0] * m[5] * m[10] -
              m[0] * m[6] * m[9] -
              m[4] * m[1] * m[10] +
              m[4] * m[2] * m[9] +
              m[8] * m[1] * m[6] -
              m[8] * m[2] * m[5];

    det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];

    if (std::abs(det) < 0.0001f) {
        // Singular: fall back to identity
        std::memset(inv, 0, sizeof(float) * 16);
        inv[0] = inv[5] = inv[10] = inv[15] = 1.0f;
        return;
    }

    det = 1.0f / det;

    for (i = 0; i < 16; i++) {
        inv[i] *= det;
    }
}
