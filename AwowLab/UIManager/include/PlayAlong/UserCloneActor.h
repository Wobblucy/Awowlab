#pragma once

#include <cmath>

namespace PlayAlong {

// Input state for the user's clone character
struct CloneInputState {
    bool forward = false;
    bool backward = false;
    bool strafeLeft = false;
    bool strafeRight = false;
};

// User-controlled clone actor for Play Along mode
class UserCloneActor {
public:
    UserCloneActor() = default;

    void setPosition(float x, float y) {
        x_ = x;
        y_ = y;
    }

    void setFacing(float facing) {
        facing_ = facing;
    }

    // W/S move along the facing and A/D strafe. Facing is set by the camera
    // (see setFacing), like right-mouse steering in game.
    void update(float deltaTime, const CloneInputState& input);

    float getX() const { return x_; }
    float getY() const { return y_; }
    float getFacing() const { return facing_; }

    // Movement speed in world units per second (WoW default is ~7 yards/sec)
    void setMoveSpeed(float speed) { moveSpeed_ = speed; }
    float getMoveSpeed() const { return moveSpeed_; }

private:
    float x_ = 0.0f;
    float y_ = 0.0f;
    float facing_ = 0.0f;  // Radians, 0 = facing +X

    float moveSpeed_ = 8.4f;      // 20% above the in-game run speed of 7.0
};

} // namespace PlayAlong
