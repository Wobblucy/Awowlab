#include "AnnotationManager.h"
#include "AnnotationSerializer.h"
#include "InterpolatedPosition.h"
#include "Core/ErrorLogger.h"
#include "AssetResolver.h"
#include <fstream>
#include <sstream>
#include <filesystem>
#include <functional>

AnnotationManager::AnnotationManager() = default;
AnnotationManager::~AnnotationManager() {
    if (dirty_ && currentKey_.encounterId != 0) {
        saveForEncounter();
    }
}

void AnnotationManager::setCurrentEncounter(uint32_t encounterId, const std::string& encounterName,
                                             const std::string& logFilePath) {
    if (dirty_ && currentKey_.encounterId != 0) {
        saveForEncounter();
    }

    currentKey_.encounterId = encounterId;
    currentKey_.encounterName = encounterName;
    currentKey_.logFilePath = logFilePath;

    loadForEncounter(currentKey_);
}

void AnnotationManager::loadForEncounter(const AnnotationKey& key) {
    currentKey_ = key;
    annotations_.clear();
    selectedId_ = 0;
    dirty_ = false;

    // Older builds saved under User Settings/annotations/; copy those across once.
    std::string legacyPath = getLegacyAnnotationFilePath(key);
    std::string userPath = getAnnotationFilePath(key);

    if (std::filesystem::exists(legacyPath) && !std::filesystem::exists(userPath)) {
        awow::AssetResolver::ensureDirectoryExists(userPath);
        std::error_code ec;
        std::filesystem::copy_file(legacyPath, userPath, ec);
        if (!ec) {
            AWOW_LOG_INFO("ANNOTATION", "Migrated annotations from " + legacyPath + " to " + userPath);
        }
    }

    // The user's copy wins over the bundled defaults/ one.
    std::string filename = awow::AssetResolver::sanitizeFilename(key.encounterName) + "-manual.json";
    std::string filepath = awow::AssetResolver::resolve("triggers", filename);

    if (std::filesystem::exists(filepath)) {
        loadFromFile(filepath);
    }

    importManualAnnotations();
}

void AnnotationManager::saveForEncounter() {
    if (!dirty_) return;

    std::string filepath = getAnnotationFilePath(currentKey_);
    if (saveToFile(filepath)) {
        dirty_ = false;
    }

    exportManualAnnotations();
}

void AnnotationManager::clearCurrentEncounter() {
    annotations_.clear();
    selectedId_ = 0;
    dirty_ = false;
    notifyChange();
}

uint64_t AnnotationManager::createAnnotation(AnnotationShape shape, float worldX, float worldY,
                                              int32_t creationTimestamp_ms) {
    Annotation annotation;
    annotation.id = nextId_++;
    annotation.shape = shape;
    annotation.worldX = worldX;
    annotation.worldY = worldY;
    annotation.currentX = worldX;
    annotation.currentY = worldY;
    annotation.anchorMode = AnnotationAnchor::World;
    annotation.creationTimestamp_ms = creationTimestamp_ms;
    annotation.positionValid = true;

    // Dimension layout varies by shape type:
    // - Circle: [radius, height, unused, unused]
    // - Rectangle: [width, length, height, rotation]
    // - Cone: [length, angle, height, rotation]
    // - Line: [length, width, height, rotation]
    switch (shape) {
        case AnnotationShape::Circle:
            annotation.dimensions = {10.0f, 3.0f, 0.0f, 0.0f};  // radius=10yd, height=3.0 (visible cylinder)
            break;
        case AnnotationShape::Rectangle:
            annotation.dimensions = {10.0f, 20.0f, 1.0f, 0.0f};  // width=10yd, length=20yd, height=1.0, rotation=0
            break;
        case AnnotationShape::Cone:
            annotation.dimensions = {20.0f, 60.0f, 1.0f, 0.0f};  // length=20yd, angle=60deg, height=1.0, rotation=0
            break;
        case AnnotationShape::Line:
            annotation.dimensions = {30.0f, 2.0f, 0.5f, 0.0f};   // length=30yd, width=2yd, height=0.5, rotation=0
            break;
        default:
            break;
    }

    annotations_.push_back(annotation);
    dirty_ = true;

    selectedId_ = annotation.id;

    notifyChange();
    return annotation.id;
}

uint64_t AnnotationManager::createActorAnchoredAnnotation(AnnotationShape shape, const std::string& actorGuid,
                                                           int32_t creationTimestamp_ms) {
    Annotation annotation;
    annotation.id = nextId_++;
    annotation.shape = shape;
    annotation.anchorMode = AnnotationAnchor::Actor;
    annotation.actorGuid = actorGuid;
    annotation.creationTimestamp_ms = creationTimestamp_ms;
    annotation.positionValid = false;  // Will be updated in updatePositions()

    switch (shape) {
        case AnnotationShape::Circle:
            annotation.dimensions = {10.0f, 0.5f, 0.0f, 0.0f};
            break;
        case AnnotationShape::Rectangle:
            annotation.dimensions = {10.0f, 20.0f, 0.0f, 0.0f};
            break;
        case AnnotationShape::Cone:
            annotation.dimensions = {20.0f, 60.0f, 0.0f, 0.0f};
            break;
        case AnnotationShape::Line:
            annotation.dimensions = {30.0f, 2.0f, 0.0f, 0.0f};
            break;
        default:
            break;
    }

    annotations_.push_back(annotation);
    dirty_ = true;

    selectedId_ = annotation.id;

    notifyChange();
    return annotation.id;
}

uint64_t AnnotationManager::createWorldMarker(WorldMarkerType markerType, float worldX, float worldY,
                                               int32_t creationTimestamp_ms) {
    Annotation annotation;
    annotation.id = nextId_++;
    annotation.shape = AnnotationShape::Circle;  // Ground circle
    annotation.anchorMode = AnnotationAnchor::World;
    annotation.worldX = worldX;
    annotation.worldY = worldY;
    annotation.currentX = worldX;
    annotation.currentY = worldY;
    annotation.creationTimestamp_ms = creationTimestamp_ms;
    annotation.positionValid = true;

    annotation.worldMarkerType = markerType;
    annotation.markerHeight = 8.0f;  // Default floating height

    float r, g, b;
    getWorldMarkerColor(markerType, r, g, b);
    annotation.color = {r, g, b, 0.4f};  // 40% alpha for ground circle

    // 1 yard radius, 5 yard tall cylinder
    annotation.dimensions = {1.0f, 5.0f, 0.0f, 0.0f};

    // World markers persist for entire encounter by default
    annotation.durationType = AnnotationDuration::Encounter;
    annotation.visible = true;

    annotations_.push_back(annotation);
    dirty_ = true;

    selectedId_ = annotation.id;

    notifyChange();
    return annotation.id;
}

void AnnotationManager::updateAnnotation(uint64_t id, const Annotation& updated) {
    auto idx = findAnnotationIndex(id);
    if (idx.has_value()) {
        annotations_[*idx] = updated;
        annotations_[*idx].id = id;  // Preserve original ID
        dirty_ = true;
        notifyChange();
    }
}

void AnnotationManager::deleteAnnotation(uint64_t id) {
    auto idx = findAnnotationIndex(id);
    if (idx.has_value()) {
        annotations_.erase(annotations_.begin() + *idx);
        if (selectedId_ == id) {
            selectedId_ = 0;
        }
        dirty_ = true;
        notifyChange();
    }
}

const Annotation* AnnotationManager::getAnnotation(uint64_t id) const {
    auto idx = findAnnotationIndex(id);
    if (idx.has_value()) {
        return &annotations_[*idx];
    }
    return nullptr;
}

Annotation* AnnotationManager::getAnnotationMutable(uint64_t id) {
    auto idx = findAnnotationIndex(id);
    if (idx.has_value()) {
        dirty_ = true;
        return &annotations_[*idx];
    }
    return nullptr;
}

std::vector<const Annotation*> AnnotationManager::getVisibleAnnotations(int32_t currentTime_ms) const {
    std::vector<const Annotation*> visible;
    visible.reserve(annotations_.size() + 1);  // +1 for possible preview

    for (const auto& annotation : annotations_) {
        if (annotation.isVisibleAtTime(currentTime_ms)) {
            visible.push_back(&annotation);
        }
    }

    // Include preview annotation if set (always visible)
    if (hasPreview_) {
        visible.push_back(&previewAnnotation_);
    }

    return visible;
}

void AnnotationManager::setPreviewAnnotation(const Annotation& preview) {
    previewAnnotation_ = preview;
    previewAnnotation_.id = 0;  // Preview has no ID
    previewAnnotation_.visible = true;
    previewAnnotation_.positionValid = true;
    previewAnnotation_.isPreview = true;  // Mark as preview for direction line rendering
    previewAnnotation_.currentX = preview.worldX;
    previewAnnotation_.currentY = preview.worldY;
    hasPreview_ = true;
}

void AnnotationManager::clearPreviewAnnotation() {
    hasPreview_ = false;
}

void AnnotationManager::updatePositions(
    const std::unordered_map<std::string_view, InterpolatedPosition>& actorPositions,
    int32_t currentTime_ms) {

    constexpr float SMOOTHING_DURATION_MS = 150.0f;
    constexpr float JUMP_THRESHOLD_SQ = 25.0f;       // 5 yards squared - skip smoothing for teleports
    constexpr float CHANGE_THRESHOLD_SQ = 0.01f;     // 0.1 yard squared - ignore tiny changes

    for (auto& annotation : annotations_) {
        if (annotation.anchorMode == AnnotationAnchor::Actor) {
            auto it = actorPositions.find(annotation.actorGuid);
            if (it != actorPositions.end() && it->second.hasPosition) {
                float newX = it->second.x;
                float newY = it->second.y;

                if (!annotation.hasValidTarget) {
                    // First position: snap, there is nothing to smooth from
                    annotation.currentX = newX;
                    annotation.currentY = newY;
                    annotation.targetX = newX;
                    annotation.targetY = newY;
                    annotation.prevX = newX;
                    annotation.prevY = newY;
                    annotation.smoothStartTime_ms = currentTime_ms;
                    annotation.hasValidTarget = true;
                } else {
                    float dx = newX - annotation.targetX;
                    float dy = newY - annotation.targetY;
                    float distSq = dx * dx + dy * dy;

                    if (distSq > CHANGE_THRESHOLD_SQ) {
                        if (distSq > JUMP_THRESHOLD_SQ) {
                            // Teleport: snap rather than slide across the room
                            annotation.currentX = newX;
                            annotation.currentY = newY;
                            annotation.targetX = newX;
                            annotation.targetY = newY;
                            annotation.prevX = newX;
                            annotation.prevY = newY;
                            annotation.smoothStartTime_ms = currentTime_ms;
                        } else {
                            // Restart smoothing from where the marker is drawn now
                            annotation.prevX = annotation.currentX;
                            annotation.prevY = annotation.currentY;
                            annotation.targetX = newX;
                            annotation.targetY = newY;
                            annotation.smoothStartTime_ms = currentTime_ms;
                        }
                    }

                    float elapsed = static_cast<float>(currentTime_ms - annotation.smoothStartTime_ms);
                    float t = std::clamp(elapsed / SMOOTHING_DURATION_MS, 0.0f, 1.0f);

                    annotation.currentX = annotation.prevX + t * (annotation.targetX - annotation.prevX);
                    annotation.currentY = annotation.prevY + t * (annotation.targetY - annotation.prevY);
                }

                annotation.positionValid = true;
            } else {
                // Actor not visible: hide and reset smoothing
                annotation.positionValid = false;
                annotation.hasValidTarget = false;
            }
        } else {
            // World anchor: lerp toward the next sampled position if there is one
            if (annotation.hasNextPosition && annotation.durationMs > 0) {
                float elapsed = static_cast<float>(currentTime_ms - annotation.creationTimestamp_ms);
                float t = std::clamp(elapsed / static_cast<float>(annotation.durationMs), 0.0f, 1.0f);
                annotation.currentX = annotation.worldX + t * (annotation.nextX - annotation.worldX);
                annotation.currentY = annotation.worldY + t * (annotation.nextY - annotation.worldY);
            } else {
                // Static position, or the last sample
                annotation.currentX = annotation.worldX;
                annotation.currentY = annotation.worldY;
            }
            annotation.positionValid = true;
        }
    }
}

void AnnotationManager::setSelectedAnnotation(uint64_t id) {
    if (id == 0 || findAnnotationIndex(id).has_value()) {
        selectedId_ = id;
    }
}

void AnnotationManager::deleteSelected() {
    if (selectedId_ != 0) {
        deleteAnnotation(selectedId_);
    }
}

Annotation* AnnotationManager::getSelectedAnnotation() {
    if (selectedId_ == 0) return nullptr;
    return getAnnotationMutable(selectedId_);
}

const Annotation* AnnotationManager::getSelectedAnnotation() const {
    if (selectedId_ == 0) return nullptr;
    return getAnnotation(selectedId_);
}

const Annotation* AnnotationManager::getAnnotationAtPosition(float worldX, float worldY,
                                                              float tolerance) const {
    // Check annotations in reverse order (last added = on top)
    for (auto it = annotations_.rbegin(); it != annotations_.rend(); ++it) {
        const auto& annotation = *it;
        if (!annotation.positionValid || annotation.deleted) continue;

        float dx = worldX - annotation.currentX;
        float dy = worldY - annotation.currentY;

        // Bounding-radius test per shape, not an exact shape hit test
        float hitRadius = tolerance;

        switch (annotation.shape) {
            case AnnotationShape::Circle:
                hitRadius = annotation.dimensions[0] + tolerance;
                break;
            case AnnotationShape::Rectangle:
                hitRadius = std::max(annotation.dimensions[0], annotation.dimensions[1]) / 2.0f + tolerance;
                break;
            case AnnotationShape::Cone:
                hitRadius = annotation.dimensions[0] + tolerance;
                break;
            case AnnotationShape::Line:
                hitRadius = annotation.dimensions[0] / 2.0f + tolerance;
                break;
            default:
                break;
        }

        float distSq = dx * dx + dy * dy;
        if (distSq <= hitRadius * hitRadius) {
            return &annotation;
        }
    }

    return nullptr;
}

std::optional<size_t> AnnotationManager::findAnnotationIndex(uint64_t id) const {
    for (size_t i = 0; i < annotations_.size(); ++i) {
        if (annotations_[i].id == id) {
            return i;
        }
    }
    return std::nullopt;
}

void AnnotationManager::notifyChange() {
    if (onChangeCallback_) {
        onChangeCallback_();
    }
}

std::string AnnotationManager::getAnnotationFilePath(const AnnotationKey& key) const {
    // triggers/{encounterName}-manual.json under the user folder
    std::string filename = awow::AssetResolver::sanitizeFilename(key.encounterName) + "-manual.json";
    return awow::AssetResolver::getUserPath("triggers", filename);
}

std::string AnnotationManager::getLegacyAnnotationFilePath(const AnnotationKey& key) const {
    // Old layout: User Settings/annotations/{encounterId}_{hash}.json
    std::hash<std::string> hasher;
    size_t logHash = hasher(key.logFilePath);

    std::ostringstream oss;
    oss << "User Settings/annotations/"
        << key.encounterId << "_"
        << std::hex << (logHash & 0xFFFFFFFF)
        << ".json";

    return oss.str();
}

bool AnnotationManager::loadFromFile(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        return false;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string json = buffer.str();

    return deserializeFromJson(json);
}

bool AnnotationManager::saveToFile(const std::string& filepath) const {
    std::filesystem::path dir = std::filesystem::path(filepath).parent_path();
    if (!std::filesystem::exists(dir)) {
        std::filesystem::create_directories(dir);
    }

    std::string json = serializeToJson();

    std::ofstream file(filepath);
    if (!file.is_open()) {
        AWOW_LOG_ERROR("ANNOTATION", "Failed to open annotation file for writing: " + filepath);
        return false;
    }

    file << json;
    return true;
}

std::string AnnotationManager::serializeToJson() const {
    return AnnotationSerializer::toJson(annotations_, currentKey_);
}

bool AnnotationManager::deserializeFromJson(const std::string& json) {
    uint64_t maxId = 0;
    if (!AnnotationSerializer::fromJson(json, annotations_, maxId)) {
        return false;
    }
    nextId_ = maxId + 1;
    return true;
}

bool AnnotationManager::exportManualAnnotations() const {
    if (currentKey_.encounterName.empty()) {
        AWOW_LOG_WARNING("ANNOTATION", "Cannot export annotations: no encounter loaded");
        return false;
    }

    // Only manual, world-anchored annotations are shareable
    size_t exportCount = 0;
    for (const auto& annotation : annotations_) {
        if (annotation.deleted) continue;
        if (annotation.isAutoGenerated) continue;
        if (annotation.anchorMode != AnnotationAnchor::World) continue;
        ++exportCount;
    }

    if (exportCount == 0) {
        return false;
    }

    std::filesystem::create_directories("manual_annotations");

    std::string safeName = currentKey_.encounterName;
    for (char& c : safeName) {
        if (c == '/' || c == '\\' || c == ':' || c == '*' ||
            c == '?' || c == '"' || c == '<' || c == '>' || c == '|') {
            c = '_';
        }
    }
    std::string filepath = "manual_annotations/" + safeName + "-manual.json";

    std::ofstream file(filepath);
    if (!file.is_open()) {
        AWOW_LOG_ERROR("ANNOTATION", "Failed to open file for manual annotation export: " + filepath);
        return false;
    }

    file << AnnotationSerializer::toShareableJson(annotations_, currentKey_.encounterName);

    return true;
}

size_t AnnotationManager::importManualAnnotations() {
    if (currentKey_.encounterName.empty()) {
        return 0;
    }

    std::string safeName = currentKey_.encounterName;
    for (char& c : safeName) {
        if (c == '/' || c == '\\' || c == ':' || c == '*' ||
            c == '?' || c == '"' || c == '<' || c == '>' || c == '|') {
            c = '_';
        }
    }
    std::string filepath = "manual_annotations/" + safeName + "-manual.json";

    if (!std::filesystem::exists(filepath)) {
        return 0;
    }

    std::ifstream file(filepath);
    if (!file.is_open()) {
        AWOW_LOG_ERROR("ANNOTATION", "Failed to open manual annotations file: " + filepath);
        return 0;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string json = buffer.str();

    auto imported = AnnotationSerializer::fromShareableJson(json);
    if (imported.empty()) {
        return 0;
    }

    for (auto& annotation : imported) {
        annotation.id = nextId_++;
        annotations_.push_back(annotation);
    }

    dirty_ = true;
    notifyChange();

    return imported.size();
}
