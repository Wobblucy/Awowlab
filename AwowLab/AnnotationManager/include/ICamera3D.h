#pragma once

// Lets AnnotationManager use the camera without depending on UIManager,
// whose Camera3D implements it.
class ICamera3D {
public:
    virtual ~ICamera3D() = default;

    // Used for billboarding
    virtual void getViewMatrix(float* outMatrix16) const = 0;
};
