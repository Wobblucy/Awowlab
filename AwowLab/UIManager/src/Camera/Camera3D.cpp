#include "Camera/Camera3D.h"
#include <algorithm>
#include <cmath>

namespace {

constexpr float degreesToRadians(float degrees) {
    return degrees * 3.14159265358979323846f / 180.0f;
}

constexpr float kDefaultPitch = degreesToRadians(45.0f);
constexpr float kFieldOfView = degreesToRadians(60.0f);

// orbit() allows looking from below; rotate() keeps the camera above the floor
constexpr float kMaxPitch = degreesToRadians(80.0f);
constexpr float kMinRotatePitch = degreesToRadians(20.0f);

// 89 rather than 90 degrees: straight down makes the lookAt basis degenerate
constexpr float kTopDownPitch = degreesToRadians(89.0f);

constexpr float kMinDistance = 10.0f;
constexpr float kMaxDistance = 500.0f;

} // namespace

Camera3D::Camera3D()
    : distance(100.0f)
    , yaw(0.0f)
    , pitch(kDefaultPitch)
    , target{ 0.0f, 0.0f, 0.0f }
    , heightOffset(0.0f)
    , fov(kFieldOfView)
    , nearPlane(0.1f)
    , farPlane(10000.0f)
{
    updateViewMatrix();
}

void Camera3D::getViewMatrix(float* mat) const {
    for (int i = 0; i < 16; i++) {
        mat[i] = viewMatrix[i];
    }
}

void Camera3D::getProjectionMatrix(float* mat, float aspectRatio) {
    updateProjectionMatrix(aspectRatio);
    for (int i = 0; i < 16; i++) {
        mat[i] = projMatrix[i];
    }
}

void Camera3D::orbit(float deltaYaw, float deltaPitch) {
    yaw += deltaYaw;

    // Top-down mode locks pitch
    if (!orthographicMode) {
        pitch = std::clamp(pitch + deltaPitch, -kMaxPitch, kMaxPitch);
    }

    updateViewMatrix();
}

void Camera3D::rotate(float deltaYaw, float deltaPitch) {
    yaw += deltaYaw;

    if (!orthographicMode) {
        pitch = std::clamp(pitch + deltaPitch, kMinRotatePitch, kMaxPitch);
    }

    updateViewMatrix();
}

void Camera3D::moveRelative(float strafe, float forward) {
    // Moves the target across the ground (X-Y) plane
    float forwardX = std::cos(yaw);
    float forwardY = std::sin(yaw);

    float rightX = -std::sin(yaw);
    float rightY = std::cos(yaw);

    target[0] += rightX * strafe + forwardX * forward;
    target[1] += rightY * strafe + forwardY * forward;

    updateViewMatrix();
}

void Camera3D::setHeightOffset(float height) {
    heightOffset = height;
    updateViewMatrix();
}

float Camera3D::getHeightOffset() const {
    return heightOffset;
}

void Camera3D::setChaseMode(bool enabled) {
    if (chaseMode == enabled) return;
    chaseMode = enabled;
    // Leaving chase mode drops back to the orbit view already stored in
    // yaw/pitch/distance, so nothing to restore here.
    updateViewMatrix();
}

void Camera3D::chaseFollow(float x, float y) {
    if (!chaseMode) return;

    // Yaw is the look direction, and the caller also uses it as the clone's
    // facing. The eye sits behind the actor along -yaw, raised by pitch so
    // right-mouse pitch tilts the shot like the in-game follow camera.
    float lookX = std::cos(yaw);
    float lookY = std::sin(yaw);

    float horiz = std::cos(pitch);
    float vert = std::sin(pitch);

    chaseEye[0] = x - lookX * chaseDistance * horiz;
    chaseEye[1] = y - lookY * chaseDistance * horiz;
    chaseEye[2] = chaseHeight + chaseDistance * vert;

    // Look slightly ahead of the actor at about chest height, which keeps the
    // actor low in the frame
    chaseCenter[0] = x + lookX * chaseLookAhead;
    chaseCenter[1] = y + lookY * chaseLookAhead;
    chaseCenter[2] = chaseHeight * 0.4f;

    updateViewMatrix();
}

void Camera3D::setOrthographicMode(bool enabled) {
    if (orthographicMode == enabled) return;

    orthographicMode = enabled;

    if (enabled) {
        savedPitch = pitch;
        pitch = kTopDownPitch;
    } else {
        pitch = savedPitch;
    }

    updateViewMatrix();
}

void Camera3D::zoom(float delta) {
    distance = std::clamp(distance + delta, kMinDistance, kMaxDistance);
    updateViewMatrix();
}

void Camera3D::pan(float deltaX, float deltaY) {
    target[0] += deltaX;
    target[1] += deltaY;
    updateViewMatrix();
}

void Camera3D::setTarget(float x, float y, float z) {
    target[0] = x;
    target[1] = y;
    target[2] = z;
    updateViewMatrix();
}

void Camera3D::smoothTargetTo(float x, float y, float z, float alpha) {
    target[0] += (x - target[0]) * alpha;
    target[1] += (y - target[1]) * alpha;
    target[2] += (z - target[2]) * alpha;
    updateViewMatrix();
}

void Camera3D::getTarget(float& x, float& y, float& z) const {
    x = target[0];
    y = target[1];
    z = target[2];
}

void Camera3D::updateViewMatrix() {
    // Z is up and actors sit on the X-Y plane. Outside chase mode the camera
    // orbits the target: yaw around it, pitch above it.
    float eye[3];
    float center[3];

    if (chaseMode) {
        // Eye and look-at point come from chaseFollow()
        eye[0] = chaseEye[0];    eye[1] = chaseEye[1];    eye[2] = chaseEye[2];
        center[0] = chaseCenter[0]; center[1] = chaseCenter[1]; center[2] = chaseCenter[2];
    } else {
        float cosYaw = std::cos(yaw);
        float sinYaw = std::sin(yaw);
        float cosPitch = std::cos(pitch);
        float sinPitch = std::sin(pitch);

        eye[0] = target[0] + distance * cosPitch * cosYaw;
        eye[1] = target[1] + distance * cosPitch * sinYaw;
        eye[2] = target[2] + distance * sinPitch;

        center[0] = target[0];
        center[1] = target[1];
        center[2] = target[2];
    }

    float up[3] = { 0.0f, 0.0f, 1.0f };

    float f[3] = {
        center[0] - eye[0],
        center[1] - eye[1],
        center[2] - eye[2]
    };
    float fLen = std::sqrt(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
    f[0] /= fLen; f[1] /= fLen; f[2] /= fLen;

    float r[3] = {
        f[1] * up[2] - f[2] * up[1],
        f[2] * up[0] - f[0] * up[2],
        f[0] * up[1] - f[1] * up[0]
    };
    float rLen = std::sqrt(r[0] * r[0] + r[1] * r[1] + r[2] * r[2]);
    r[0] /= rLen; r[1] /= rLen; r[2] /= rLen;

    float u[3] = {
        r[1] * f[2] - r[2] * f[1],
        r[2] * f[0] - r[0] * f[2],
        r[0] * f[1] - r[1] * f[0]
    };

    // Column-major lookAt
    viewMatrix[0] = r[0];
    viewMatrix[1] = u[0];
    viewMatrix[2] = -f[0];
    viewMatrix[3] = 0.0f;

    viewMatrix[4] = r[1];
    viewMatrix[5] = u[1];
    viewMatrix[6] = -f[1];
    viewMatrix[7] = 0.0f;

    viewMatrix[8] = r[2];
    viewMatrix[9] = u[2];
    viewMatrix[10] = -f[2];
    viewMatrix[11] = 0.0f;

    viewMatrix[12] = -(r[0] * eye[0] + r[1] * eye[1] + r[2] * eye[2]);
    viewMatrix[13] = -(u[0] * eye[0] + u[1] * eye[1] + u[2] * eye[2]);
    viewMatrix[14] = (f[0] * eye[0] + f[1] * eye[1] + f[2] * eye[2]);
    viewMatrix[15] = 1.0f;
}

void Camera3D::updateProjectionMatrix(float aspectRatio) {
    // Perspective even in "orthographic" mode, which only locks the pitch
    float tanHalfFov = std::tan(fov / 2.0f);

    projMatrix[0] = 1.0f / (aspectRatio * tanHalfFov);
    projMatrix[1] = 0.0f;
    projMatrix[2] = 0.0f;
    projMatrix[3] = 0.0f;

    projMatrix[4] = 0.0f;
    projMatrix[5] = -1.0f / tanHalfFov; // Vulkan Y-flip
    projMatrix[6] = 0.0f;
    projMatrix[7] = 0.0f;

    projMatrix[8] = 0.0f;
    projMatrix[9] = 0.0f;
    projMatrix[10] = farPlane / (nearPlane - farPlane);
    projMatrix[11] = -1.0f;

    projMatrix[12] = 0.0f;
    projMatrix[13] = 0.0f;
    projMatrix[14] = -(farPlane * nearPlane) / (farPlane - nearPlane);
    projMatrix[15] = 0.0f;
}

Camera3D::ScreenProjection Camera3D::worldToScreen(float worldX, float worldY, float worldZ,
                                                    float screenWidth, float screenHeight) {
    ScreenProjection result{0.0f, 0.0f, false};

    float aspectRatio = screenWidth / screenHeight;
    updateProjectionMatrix(aspectRatio);

    float worldPos[4] = {worldX, worldY, worldZ, 1.0f};

    float viewPos[4] = {
        viewMatrix[0] * worldPos[0] + viewMatrix[4] * worldPos[1] + viewMatrix[8] * worldPos[2] + viewMatrix[12] * worldPos[3],
        viewMatrix[1] * worldPos[0] + viewMatrix[5] * worldPos[1] + viewMatrix[9] * worldPos[2] + viewMatrix[13] * worldPos[3],
        viewMatrix[2] * worldPos[0] + viewMatrix[6] * worldPos[1] + viewMatrix[10] * worldPos[2] + viewMatrix[14] * worldPos[3],
        viewMatrix[3] * worldPos[0] + viewMatrix[7] * worldPos[1] + viewMatrix[11] * worldPos[2] + viewMatrix[15] * worldPos[3]
    };

    float clipPos[4] = {
        projMatrix[0] * viewPos[0] + projMatrix[4] * viewPos[1] + projMatrix[8] * viewPos[2] + projMatrix[12] * viewPos[3],
        projMatrix[1] * viewPos[0] + projMatrix[5] * viewPos[1] + projMatrix[9] * viewPos[2] + projMatrix[13] * viewPos[3],
        projMatrix[2] * viewPos[0] + projMatrix[6] * viewPos[1] + projMatrix[10] * viewPos[2] + projMatrix[14] * viewPos[3],
        projMatrix[3] * viewPos[0] + projMatrix[7] * viewPos[1] + projMatrix[11] * viewPos[2] + projMatrix[15] * viewPos[3]
    };

    // Behind the camera
    if (clipPos[3] <= 0.001f) {
        return result;
    }

    float ndcX = clipPos[0] / clipPos[3];
    float ndcY = clipPos[1] / clipPos[3];

    if (ndcX < -1.0f || ndcX > 1.0f || ndcY < -1.0f || ndcY > 1.0f) {
        return result;
    }

    // The projection flips Y, so NDC -1 is the top of the screen
    result.x = (ndcX + 1.0f) * 0.5f * screenWidth;
    result.y = (ndcY + 1.0f) * 0.5f * screenHeight;
    result.visible = true;

    return result;
}
