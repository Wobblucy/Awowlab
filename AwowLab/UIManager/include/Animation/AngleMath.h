#pragma once

// Facing angles are radians in [0, 2pi). Anything that blends two of them
// has to go the short way round the circle: 6.2 and 0.1 are 0.18 apart, not
// 6.1, and their midpoint is near 0, not near pi.
namespace angle_math {

constexpr float kTwoPi = 6.28318530718f;
constexpr float kPi = 3.14159265f;

// Shortest-path interpolation from a to b, result in [0, 2pi)
inline float lerpAngle(float a, float b, float t) {
    float diff = b - a;
    // Inputs are in [0, 2pi), so one correction brings diff into [-pi, pi]
    if (diff > kPi) diff -= kTwoPi;
    else if (diff < -kPi) diff += kTwoPi;
    float result = a + t * diff;
    if (result < 0) result += kTwoPi;
    else if (result >= kTwoPi) result -= kTwoPi;
    return result;
}

// Running mean of facing samples. `mean` holds the mean of the first
// count-1 samples; returns the mean including `sample` as the count-th.
inline float addToAngleMean(float mean, float sample, int count) {
    if (count <= 1) {
        return lerpAngle(sample, sample, 0.0f);
    }
    return lerpAngle(mean, sample, 1.0f / static_cast<float>(count));
}

}  // namespace angle_math
