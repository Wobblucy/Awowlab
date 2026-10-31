#pragma once

#include "AnnotationTypes.h"
#include "Core/LocalizationManager.h"
#include <cstdint>

class AnnotationManager;

// Edits the annotation selected in the viewport
class UIAnnotationPropertiesPanel {
public:
    UIAnnotationPropertiesPanel() = default;

    // Returns true if the annotation was modified
    bool render(AnnotationManager* manager, int32_t currentTimestamp_ms);

    void setVisible(bool visible) { visible_ = visible; }
    bool isVisible() const { return visible_; }

private:
    bool visible_ = false;

    void renderCircleControls(Annotation& annotation);
    void renderRectangleControls(Annotation& annotation);
    void renderConeControls(Annotation& annotation);
    void renderLineControls(Annotation& annotation);
};
