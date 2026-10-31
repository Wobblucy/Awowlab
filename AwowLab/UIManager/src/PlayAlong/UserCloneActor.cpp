#include "PlayAlong/UserCloneActor.h"
#include <cmath>

namespace PlayAlong {

void UserCloneActor::update(float deltaTime, const CloneInputState& input) {
    constexpr float HALF_PI = 1.5707963f;

    float forwardX = std::cos(facing_);
    float forwardY = std::sin(facing_);
    float rightX = std::cos(facing_ - HALF_PI);
    float rightY = std::sin(facing_ - HALF_PI);

    float moveX = 0.0f;
    float moveY = 0.0f;

    if (input.forward) {
        moveX += forwardX;
        moveY += forwardY;
    }
    if (input.backward) {
        moveX -= forwardX;
        moveY -= forwardY;
    }
    if (input.strafeRight) {
        moveX += rightX;
        moveY += rightY;
    }
    if (input.strafeLeft) {
        moveX -= rightX;
        moveY -= rightY;
    }

    // Normalize so diagonal movement isn't faster
    float moveLen = std::sqrt(moveX * moveX + moveY * moveY);
    if (moveLen > 0.01f) {
        moveX /= moveLen;
        moveY /= moveLen;
        x_ += moveX * moveSpeed_ * deltaTime;
        y_ += moveY * moveSpeed_ * deltaTime;
    }
}

} // namespace PlayAlong
