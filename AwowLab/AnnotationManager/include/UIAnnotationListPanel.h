#pragma once

#include <string>
#include <cstdint>

class AnnotationManager;
struct Annotation;
enum class AnnotationShape : uint8_t;

// Result from annotation list panel interaction
struct AnnotationListAction {
    uint64_t snapToId = 0;   // Annotation ID to snap camera to (0 = none)
    uint64_t editId = 0;     // Annotation ID to open properties panel for (0 = none)
    bool exportRequested = false;
};

// Panel showing list of manual annotations with selection and camera snap
class UIAnnotationListPanel {
public:
    UIAnnotationListPanel() = default;
    ~UIAnnotationListPanel() = default;

    // Returns what the user clicked
    AnnotationListAction render(AnnotationManager* manager, int32_t currentTime_ms);

    bool isVisible() const { return visible_; }
    void setVisible(bool visible) { visible_ = visible; }
    bool* getVisiblePtr() { return &visible_; }

private:
    bool visible_ = false;

    const char* getShapeName(AnnotationShape shape) const;

    // "Xs", "Encounter" or "Expired"
    std::string formatDuration(const Annotation& ann, int32_t currentTime_ms) const;
};
