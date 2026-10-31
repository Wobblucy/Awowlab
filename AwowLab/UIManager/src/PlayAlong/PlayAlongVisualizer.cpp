// Setup and the per-frame loop. The rest of the class lives in:
// - PlayAlongVisualizer_Database.cpp  - Database and encounter loading
// - PlayAlongVisualizer_Input.cpp     - Input handling, clone/camera, audio
// - PlayAlongVisualizer_UI.cpp        - UI rendering methods
// - PlayAlongVisualizer_3D.cpp        - 3D rendering, actor positions

#include "PlayAlong/PlayAlongVisualizer.h"
#include "Camera/Camera3D.h"
#include "Rendering/SceneRenderer3D.h"
#include "UI/EncounterTextureManager.h"
#include "Color/ActorColorGenerator.h"
#include "Database/EventDatabase.h"
#include "Database/SpellCastDatabase.h"
#include "Database/DeathDatabase.h"
#include "Database/CombatDatabase.h"
#include "Database/AuraDatabase.h"
#include "AnnotationManager.h"
#include "AutoAnnotationTriggerManager.h"
#include "AnnotationRenderer.h"
#include "UI/ActorAppearanceSettings.h"
#include "Core/ErrorLogger.h"
#include <imgui.h>

namespace PlayAlong {

PlayAlongVisualizer::PlayAlongVisualizer() {
    camera3D_ = std::make_shared<Camera3D>();
    colorGenerator_ = std::make_shared<ActorColorGenerator>();

    // Over-the-shoulder camera
    camera3D_->zoom(-38.0f);      // default distance ~50 -> ~12
    camera3D_->setHeightOffset(5.0f);
    camera3D_->rotate(0.0f, 0.3f);

    // Only enemies are drawn in Play Along
    visibilitySettings_.renderFriendlyPlayers = false;
    visibilitySettings_.renderHostilePlayers = false;
    visibilitySettings_.renderNeutralPlayers = false;
    visibilitySettings_.renderFriendlyCreatures = false;
    visibilitySettings_.renderHostileCreatures = true;
    visibilitySettings_.renderNeutralCreatures = true;
    visibilitySettings_.renderFriendlyVehicles = false;
    visibilitySettings_.renderHostileVehicles = true;
    visibilitySettings_.renderNeutralVehicles = true;
    visibilitySettings_.renderFriendlyPets = false;
    visibilitySettings_.renderHostilePets = false;
    visibilitySettings_.renderNeutralPets = false;

    annotationManager_ = std::make_unique<AnnotationManager>();
    autoAnnotationTriggerManager_ = std::make_unique<AutoAnnotationTriggerManager>();

    appearanceSettings_ = std::make_shared<ActorAppearanceSettings>();

    // Force the clone and ghost to render as cones so the over-the-shoulder view
    // shows a clear facing direction instead of a facing-agnostic disc/sphere.
    // These GUIDs are the special actors added in render3D().
    AppearanceOverride coneOverride;
    coneOverride.shape = ActorShape::Cone;
    appearanceSettings_->setGuidOverride("PlayAlongClone-User", coneOverride);
    appearanceSettings_->setGuidOverride("PlayAlongGhost-Target", coneOverride);
}

PlayAlongVisualizer::~PlayAlongVisualizer() {
    if (annotationRenderer_ && device_) {
        annotationRenderer_->cleanup(device_);
    }
    if (encounterTextureManager_) {
        encounterTextureManager_->cleanup();
    }
    if (sceneRenderer_ && device_) {
        sceneRenderer_->cleanup(device_);
    }
}

bool PlayAlongVisualizer::initialize(VkDevice device, VkPhysicalDevice gpu,
                                      VkRenderPass renderPass,
                                      VkDescriptorPool descriptorPool,
                                      VkQueue graphicsQueue, VkCommandPool commandPool) {
    device_ = device;
    gpu_ = gpu;
    renderPass_ = renderPass;

    sceneRenderer_ = std::make_unique<SceneRenderer3D>();
    if (!sceneRenderer_->initialize(device, gpu, renderPass, graphicsQueue, commandPool)) {
        return false;
    }

    sceneRenderer_->setAppearanceSettings(appearanceSettings_);

    encounterTextureManager_ = std::make_unique<EncounterTextureManager>();
    if (!encounterTextureManager_->initialize(device, gpu, graphicsQueue, commandPool, descriptorPool)) {
        AWOW_LOG_WARNING("PlayAlong", "Failed to initialize encounter texture manager");
        encounterTextureManager_.reset();
    }

    annotationRenderer_ = std::make_unique<AnnotationRenderer>();
    if (!annotationRenderer_->initialize(device, gpu, renderPass, 0)) {
        AWOW_LOG_WARNING("PlayAlong", "Failed to initialize annotation renderer");
        annotationRenderer_.reset();  // Continue without annotations
    } else {
        annotationRenderer_->setCameraUBO(sceneRenderer_->getCameraUBO());
    }

    vulkanInitialized_ = true;
    return true;
}

void PlayAlongVisualizer::render() {
    float currentTime = static_cast<float>(ImGui::GetTime());
    float deltaTime = currentTime - lastFrameTime_;
    lastFrameTime_ = currentTime;

    if (countdownActive_) {
        countdownTimeRemaining_ -= deltaTime;
        if (countdownTimeRemaining_ <= 0.0f) {
            countdownActive_ = false;
            isPlaying_ = true;
        }
    }

    if (isPlaying_ && maxTimeMs_ > minTimeMs_) {
        currentTimeMs_ += static_cast<uint32_t>(deltaTime * 1000.0f);
        if (currentTimeMs_ > maxTimeMs_) {
            currentTimeMs_ = maxTimeMs_;
            isPlaying_ = false;
        }
    }

    if (snapHintTimeRemaining_ > 0.0f) {
        snapHintTimeRemaining_ -= deltaTime;
    }

    if (!feedbackInitialized_ && spellCastDb_ && !targetPlayerGuid_.empty()) {
        feedbackSystem_.initialize(keybindSettings_, spellCastDb_, targetPlayerGuid_);
        feedbackInitialized_ = true;
    }

    // Time went backwards: rewind scoring to the new position
    if (feedbackInitialized_ && currentTimeMs_ < previousTimeMs_) {
        feedbackSystem_.resetToTime(static_cast<int32_t>(currentTimeMs_));
        lastFeedbackCount_ = feedbackSystem_.getRecentFeedback().size();
    }

    if (currentTimeMs_ < previousTimeMs_ && autoAnnotationTriggerManager_ && annotationManager_) {
        autoAnnotationTriggerManager_->clearAutoAnnotations(annotationManager_.get());
    }

    if (annotationsEnabled_ && autoAnnotationTriggerManager_ && annotationManager_ &&
        autoAnnotationTriggerManager_->hasTriggers()) {

        if (!autoAnnotationTriggerManager_->isCacheValid()) {
            autoAnnotationTriggerManager_->buildCache(
                spellCastDb_,
                combatDb_,
                auraDb_,
                eventDb_
            );
        }

        autoAnnotationTriggerManager_->updateAnnotations(
            static_cast<int32_t>(currentTimeMs_),
            annotationManager_.get()
        );
    }

    handleInput();
    checkKeybindPresses();

    if (feedbackInitialized_) {
        feedbackSystem_.update(static_cast<int32_t>(currentTimeMs_));

        // Beep once for the newest feedback event
        const auto& recentFeedback = feedbackSystem_.getRecentFeedback();
        if (recentFeedback.size() > lastFeedbackCount_) {
            playFeedbackSound(recentFeedback.back().result);
            lastFeedbackCount_ = recentFeedback.size();
        }
    }

    previousTimeMs_ = currentTimeMs_;

    updateCamera();
    renderUI();
}

} // namespace PlayAlong
