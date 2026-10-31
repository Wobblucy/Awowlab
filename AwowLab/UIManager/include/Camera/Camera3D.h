#pragma once
#include <array>
#include "AnnotationManager/include/ICamera3D.h"

class Camera3D : public ICamera3D {
public:
    Camera3D();
    ~Camera3D() override = default;

    void orbit(float deltaYaw, float deltaPitch);
    void rotate(float deltaYaw, float deltaPitch);  // Look around (over-the-shoulder)
    void moveRelative(float strafe, float forward);  // Move relative to facing direction
    void zoom(float delta);
    void pan(float deltaX, float deltaY);

    void setTarget(float x, float y, float z);
    void smoothTargetTo(float x, float y, float z, float alpha);
    void getTarget(float& x, float& y, float& z) const;

    void getViewMatrix(float* mat) const override;
    void getProjectionMatrix(float* mat, float aspectRatio);

    // Pixel coordinates with (0,0) at the top-left. visible is false when the
    // point is behind the camera or outside the frustum.
    struct ScreenProjection {
        float x;
        float y;
        bool visible;
    };
    ScreenProjection worldToScreen(float worldX, float worldY, float worldZ,
                                    float screenWidth, float screenHeight);

    float getYaw() const { return yaw; }
    float getPitch() const { return pitch; }
    float getDistance() const { return distance; }

    // Top-down view. Locks pitch near 90 degrees; the projection stays perspective.
    void setOrthographicMode(bool enabled);
    bool isOrthographicMode() const { return orthographicMode; }

    // Over-the-shoulder chase mode: the camera sits behind a followed actor and
    // looks the way it faces. Chase distance is separate from the orbit distance,
    // so switching back to orbit returns to the previous view.
    void setChaseMode(bool enabled);
    bool isChaseMode() const { return chaseMode; }

    // Place the camera behind an actor at (x, y). The camera's current yaw is
    // the look direction (driven by RMB drag) and also the actor's facing.
    // Only takes effect while chase mode is active; call once per frame.
    void chaseFollow(float x, float y);

    // Stored only; the view matrix does not currently use it
    void setHeightOffset(float height);
    float getHeightOffset() const;

private:
    void updateViewMatrix();
    void updateProjectionMatrix(float aspectRatio);

private:
    float distance;
    float yaw;        // Camera facing direction (horizontal)
    float pitch;      // Camera look angle (vertical)
    float target[3];
    float heightOffset;
    float fov;
    float nearPlane;
    float farPlane;

    bool orthographicMode = false;
    float savedPitch = 0.0f;  // restored when leaving top-down mode

    // Over-the-shoulder chase mode
    bool chaseMode = false;
    float chaseDistance = 22.0f;   // How far behind the actor the eye sits
    float chaseHeight = 6.0f;      // Eye height above the actor's feet
    float chaseLookAhead = 4.0f;   // Look at a point slightly ahead of the actor
    float chaseEye[3] = {0.0f, 0.0f, 0.0f};     // Computed eye position (chase mode)
    float chaseCenter[3] = {0.0f, 0.0f, 0.0f};  // Computed look-at point (chase mode)

    float viewMatrix[16];
    float projMatrix[16];
};
