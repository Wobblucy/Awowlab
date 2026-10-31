#pragma once

struct CameraSettings {
    bool enableFollow = true;
    float panSensitivity = 0.2f;
    float followSpeed = 3.0f;  // How fast camera follows (1=slow, 10=snappy)
    bool resetRequested = false;  // Flag to request camera reset to centroid
    bool orthographicMode = false;  // Toggle orthographic projection (top-down view)
};
