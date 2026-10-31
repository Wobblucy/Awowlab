#pragma once

#include "AnnotationTypes.h"
#include <vector>
#include <unordered_map>
#include <string>
#include <string_view>
#include <functional>
#include <optional>

class FrameDataProvider;

// Central manager for annotation CRUD operations, persistence, and position resolution
class AnnotationManager {
public:
    AnnotationManager();
    ~AnnotationManager();

    // Call when loading/unloading encounters
    void loadForEncounter(const AnnotationKey& key);
    void saveForEncounter();
    void clearCurrentEncounter();

    void setCurrentEncounter(uint32_t encounterId, const std::string& encounterName,
                              const std::string& logFilePath);

    uint64_t createAnnotation(AnnotationShape shape, float worldX, float worldY,
                               int32_t creationTimestamp_ms);
    uint64_t createActorAnchoredAnnotation(AnnotationShape shape, const std::string& actorGuid,
                                            int32_t creationTimestamp_ms);
    uint64_t createWorldMarker(WorldMarkerType markerType, float worldX, float worldY,
                                int32_t creationTimestamp_ms);
    void updateAnnotation(uint64_t id, const Annotation& updated);
    void deleteAnnotation(uint64_t id);
    const Annotation* getAnnotation(uint64_t id) const;
    Annotation* getAnnotationMutable(uint64_t id);

    const std::vector<Annotation>& getAnnotations() const { return annotations_; }
    std::vector<const Annotation*> getVisibleAnnotations(int32_t currentTime_ms) const;
    size_t getAnnotationCount() const { return annotations_.size(); }
    bool hasAnnotations() const { return !annotations_.empty(); }
    bool isDirty() const { return dirty_; }

    // Preview annotation (for UI configuration, not persisted)
    void setPreviewAnnotation(const Annotation& preview);
    void clearPreviewAnnotation();
    bool hasPreviewAnnotation() const { return hasPreview_; }
    const Annotation* getPreviewAnnotation() const { return hasPreview_ ? &previewAnnotation_ : nullptr; }

    // Call each frame. Actor-anchored positions are smoothed because actor
    // position data arrives intermittently and would otherwise flicker.
    void updatePositions(const std::unordered_map<std::string_view, struct InterpolatedPosition>& actorPositions,
                         int32_t currentTime_ms);

    void setSelectedAnnotation(uint64_t id);
    uint64_t getSelectedAnnotationId() const { return selectedId_; }
    Annotation* getSelectedAnnotation();
    const Annotation* getSelectedAnnotation() const;
    void clearSelection() { selectedId_ = 0; }
    bool hasSelection() const { return selectedId_ != 0; }

    void deleteSelected();

    // For click-to-select. Skips annotations without a valid position.
    const Annotation* getAnnotationAtPosition(float worldX, float worldY, float tolerance = 5.0f) const;

    using ChangeCallback = std::function<void()>;
    void setChangeCallback(ChangeCallback callback) { onChangeCallback_ = std::move(callback); }

    // Writes manual world-anchored annotations to
    // manual_annotations/{encounterName}-manual.json
    bool exportManualAnnotations() const;

    // Reads manual_annotations/{encounterName}-manual.json; returns the number imported
    size_t importManualAnnotations();

private:
    std::vector<Annotation> annotations_;
    AnnotationKey currentKey_;
    uint64_t nextId_ = 1;
    uint64_t selectedId_ = 0;
    bool dirty_ = false;
    ChangeCallback onChangeCallback_;

    Annotation previewAnnotation_;
    bool hasPreview_ = false;

    bool loadFromFile(const std::string& filepath);
    bool saveToFile(const std::string& filepath) const;
    std::string getAnnotationFilePath(const AnnotationKey& key) const;
    std::string getLegacyAnnotationFilePath(const AnnotationKey& key) const;

    std::string serializeToJson() const;
    bool deserializeFromJson(const std::string& json);

    void notifyChange();

    std::optional<size_t> findAnnotationIndex(uint64_t id) const;
};
