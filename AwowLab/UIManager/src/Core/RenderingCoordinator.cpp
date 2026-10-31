#include "Core/RenderingCoordinator.h"
#include "Core/FrameDataProvider.h"
#include "Core/Utils/ExecutablePath.h"
#include "Core/ErrorLogger.h"
#include "Rendering/SceneRenderer3D.h"
#include "Rendering/Minimap2D.h"
#include "Geometry/CustomShapeRegistry.h"
#include "UI/SpellIconLoader.h"
#include "UI/ActorAppearanceSettings.h"
#include "UI/SpellAssignmentSettings.h"
#include "Camera/Camera3D.h"
#include "Camera/CameraController.h"
#include "Color/ActorColorGenerator.h"
#include "../../Database/AuraDatabase.h"
#include "../../Database/SpellCastDatabase.h"
#include "AnnotationManager/include/AnnotationRenderer.h"
#include "AnnotationManager/include/AnnotationManager.h"
#include <optional>
#include <cstdio>
#include <cmath>

RenderingCoordinator::RenderingCoordinator()
    : sceneRenderer3D_(std::make_unique<SceneRenderer3D>())
    , minimap2D_(std::make_shared<Minimap2D>())
    , spellIconLoader_(std::make_unique<SpellIconLoader>())
    , annotationRenderer_(std::make_unique<AnnotationRenderer>())
{
}

RenderingCoordinator::~RenderingCoordinator() = default;

bool RenderingCoordinator::initialize3D(VkDevice device, VkPhysicalDevice physicalDevice, VkRenderPass renderPass,
                                          VkQueue graphicsQueue, VkCommandPool commandPool) {
    if (!sceneRenderer3D_->initialize(device, physicalDevice, renderPass, graphicsQueue, commandPool)) {
        return false;
    }

    if (annotationRenderer_) {
        if (!annotationRenderer_->initialize(device, physicalDevice, renderPass, 0)) {
            AWOW_LOG_WARNING("Rendering", "Failed to initialize annotation renderer (non-fatal)");
        } else {
            VkBuffer cameraUBO = sceneRenderer3D_->getCameraUBO();
            if (cameraUBO != VK_NULL_HANDLE) {
                annotationRenderer_->setCameraUBO(cameraUBO);
            }
        }
    }

    return true;
}

bool RenderingCoordinator::initializeSpellIcons(VkDevice device, VkPhysicalDevice physicalDevice,
                                                  VkQueue graphicsQueue, VkCommandPool commandPool,
                                                  VkDescriptorPool descriptorPool) {
    if (!spellIconLoader_->initialize(device, physicalDevice, graphicsQueue,
                                       commandPool, descriptorPool)) {
        AWOW_LOG_ERROR("Rendering", "Failed to initialize spell icon loader");
        return false;
    }

    // Only the path is set here; mappings load on demand after parsing
    std::string mappingPath = awow::getExecutableDirectory() + "data\\spell_icon_table.bin";
    spellIconLoader_->setBinaryMappingPath(mappingPath);

    return true;
}

void RenderingCoordinator::cleanup3D(VkDevice device) {
    if (spellIconLoader_) {
        spellIconLoader_->cleanup();
    }
    if (annotationRenderer_) {
        annotationRenderer_->cleanup(device);
    }
    sceneRenderer3D_->cleanup(device);
}

void RenderingCoordinator::processTextureCleanup() {
    if (spellIconLoader_) {
        spellIconLoader_->processTextureCleanup();
    }
    if (sceneRenderer3D_) {
        sceneRenderer3D_->processTextureCleanup();
    }
}

void RenderingCoordinator::setCamera(std::shared_ptr<Camera3D> camera) {
    camera3D_ = std::move(camera);
}

void RenderingCoordinator::setCameraController(std::shared_ptr<CameraController> cameraController) {
    cameraController_ = std::move(cameraController);
}

void RenderingCoordinator::setColorGenerator(std::shared_ptr<ActorColorGenerator> colorGen) {
    colorGenerator_ = std::move(colorGen);
}

void RenderingCoordinator::setAppearanceSettings(std::shared_ptr<ActorAppearanceSettings> settings) {
    if (sceneRenderer3D_) {
        sceneRenderer3D_->setAppearanceSettings(std::move(settings));
    }
}

void RenderingCoordinator::setCustomShapeRegistry(std::shared_ptr<CustomShapeRegistry> registry) {
    if (sceneRenderer3D_) {
        sceneRenderer3D_->setCustomShapeRegistry(std::move(registry));
    }
}

CustomShapeRegistry* RenderingCoordinator::getCustomShapeRegistry() {
    if (sceneRenderer3D_) {
        return sceneRenderer3D_->getCustomShapeRegistry();
    }
    return nullptr;
}

void RenderingCoordinator::setMinimapBounds(int32_t minX, int32_t maxX, int32_t minY, int32_t maxY) {
    // This runs every frame; only refit when the bounds actually move, or
    // the user's own minimap pan and zoom get thrown away each frame
    if (minimap2D_->setBounds(minX, maxX, minY, maxY)) {
        minimap2D_->autoZoomToPlayerBounds();
    }
}

void RenderingCoordinator::setMinimapWorldMap(float minX, float maxX, float minY, float maxY, void* textureId) {
    minimap2D_->setWorldMapBounds(minX, maxX, minY, maxY);
    minimap2D_->setWorldMapTexture(textureId);
}

void RenderingCoordinator::clearMinimapWorldMap() {
    minimap2D_->clearWorldMap();
}

void RenderingCoordinator::syncMinimapWorldMap() {
    if (!sceneRenderer3D_) {
        return;
    }

    float minX, maxX, minY, maxY;
    void* textureId = nullptr;
    if (sceneRenderer3D_->getWorldMapInfo(minX, maxX, minY, maxY, textureId)) {
        setMinimapWorldMap(minX, maxX, minY, maxY, textureId);
        minimap2D_->autoZoomToPlayerBounds();
    } else {
        clearMinimapWorldMap();
    }
}

void RenderingCoordinator::setMapBounds(uint32_t mapId, const std::string& mapName,
                                          float x1, float y1, float x2, float y2) {
    if (sceneRenderer3D_) {
        sceneRenderer3D_->setMapBounds(mapId, mapName, x1, y1, x2, y2);
    }
}

void RenderingCoordinator::setMapBoundsWithTexture(float x1, float y1, float x2, float y2,
                                                     VkImageView imageView, VkSampler sampler,
                                                     uint32_t textureWidth, uint32_t textureHeight) {
    if (sceneRenderer3D_) {
        sceneRenderer3D_->setMapBoundsWithTexture(x1, y1, x2, y2, imageView, sampler, textureWidth, textureHeight);
    }
}

void RenderingCoordinator::clearMapBounds() {
    if (sceneRenderer3D_) {
        sceneRenderer3D_->clearMapBounds();
    }
}

void RenderingCoordinator::preloadSpellIcons(const std::vector<uint32_t>& spellIds) {
    if (spellIconLoader_) {
        spellIconLoader_->preloadSpellIcons(spellIds);
    }
}

void RenderingCoordinator::preloadMapTexture(uint32_t mapId) {
    if (sceneRenderer3D_) {
        sceneRenderer3D_->preloadMapTexture(mapId);
    }
}

void RenderingCoordinator::setAnnotationManager(AnnotationManager* manager) {
    annotationManager_ = manager;
}

bool RenderingCoordinator::isInitialized() const {
    return sceneRenderer3D_ && sceneRenderer3D_->isInitialized();
}

void RenderingCoordinator::clearTexturedModelDescriptorCache() {
    if (sceneRenderer3D_) {
        sceneRenderer3D_->clearTexturedModelDescriptorCache();
    }
}

void RenderingCoordinator::render3D(VkDevice device, VkCommandBuffer commandBuffer, VkExtent2D extent,
                                     FrameDataProvider* dataProvider,
                                     const ActorVisibilitySettings& visibilitySettings,
                                     const CameraSettings& cameraSettings,
                                     const NameplateSettings& nameplateSettings,
                                     [[maybe_unused]] const FacingIndicatorSettings& facingIndicatorSettings,
                                     std::string_view selectedGuid,
                                     float deltaTime) {
    if (!isInitialized() || !dataProvider || !dataProvider->hasEvents()) {
        return;
    }

    if (!dataProvider->hasPositions()) {
        return;
    }

    const auto& actorPositions = dataProvider->getActorPositions();
    const auto& actorResources = dataProvider->getActorResources();
    const auto& guidToName = dataProvider->getGuidToName();

    // Filter out dead actors (hide 500ms after death)
    filteredPositionsBuffer_.clear();
    for (const auto& [guid, pos] : actorPositions) {
        if (!dataProvider->shouldHideActor(guid)) {
            filteredPositionsBuffer_.emplace(guid, pos);
        }
    }

    if (cameraSettings.enableFollow && cameraController_ && camera3D_) {
        std::optional<std::string_view> focusGuid = selectedGuid.empty()
            ? std::nullopt
            : std::make_optional(selectedGuid);
        cameraController_->updateCameraTarget(camera3D_.get(), filteredPositionsBuffer_, focusGuid,
                                               deltaTime, cameraSettings.followSpeed);
    }

    if (camera3D_) {
        camera3D_->setOrthographicMode(cameraSettings.orthographicMode);
    }

    float animationTime = static_cast<float>(ImGui::GetTime());

    sceneRenderer3D_->render(device, commandBuffer, extent,
                             filteredPositionsBuffer_, actorResources, guidToName,
                             colorGenerator_, camera3D_,
                             visibilitySettings,
                             nameplateSettings.showNameplates,
                             selectedGuid,
                             animationTime);

    // Annotations depth-test without writing depth, so ground annotations end up behind actors
    if (annotationRenderer_ && annotationRenderer_->isInitialized() && annotationManager_) {
        int32_t currentTime_ms = static_cast<int32_t>(dataProvider->getCurrentTime());
        annotationRenderer_->render(device, commandBuffer, extent,
                                     annotationManager_, camera3D_.get(), currentTime_ms);
    }
}

void RenderingCoordinator::renderMinimap(ImDrawList* drawList, ImVec2 canvasPos, ImVec2 canvasSize,
                                          FrameDataProvider* dataProvider,
                                          const ActorVisibilitySettings& visibilitySettings,
                                          const NameplateSettings& nameplateSettings,
                                          std::string_view selectedGuid) {
    if (!dataProvider || !dataProvider->hasEvents() || !dataProvider->hasPositions()) {
        return;
    }

    const auto& actorPositions = dataProvider->getActorPositions();
    const auto& guidToName = dataProvider->getGuidToName();
    const auto& guidToType = dataProvider->getGuidToType();

    minimapPositionsBuffer_.clear();
    minimapPositionsBuffer_.reserve(actorPositions.size());

    for (const auto& [guid, interpPos] : actorPositions) {
        if (!interpPos.hasPosition) continue;

        // Skip dead actors (hide 500ms after death)
        if (dataProvider->shouldHideActor(guid)) continue;

        std::string_view name = guid;
        ActorType type = ActorType::Player;
        UnitReaction reaction = UnitReaction::Friendly;

        auto nameIt = guidToName.find(guid);
        if (nameIt != guidToName.end()) {
            name = nameIt->second;
        }
        auto typeIt = guidToType.find(guid);
        if (typeIt != guidToType.end()) {
            type = typeIt->second;
        }

        if (colorGenerator_) {
            const UnitFlags* flags = colorGenerator_->getActorFlags(guid);
            if (flags) {
                reaction = flags->reaction;
            }
        }

        minimapPositionsBuffer_.push_back({guid, name, static_cast<int32_t>(interpPos.x),
                                           static_cast<int32_t>(interpPos.y), type, reaction});
    }

    minimap2D_->render(drawList, canvasPos, canvasSize,
                       minimapPositionsBuffer_, colorGenerator_,
                       visibilitySettings, nameplateSettings,
                       selectedGuid);
}

void RenderingCoordinator::handleMinimapInput(ImVec2 canvasSize) {
    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
        ImVec2 delta = ImGui::GetIO().MouseDelta;
        minimap2D_->applyPanDelta(delta.x / canvasSize.x, delta.y / canvasSize.y);
    }

    if (ImGui::IsItemHovered()) {
        float wheel = ImGui::GetIO().MouseWheel;
        if (wheel != 0.0f) {
            minimap2D_->applyZoomDelta(wheel);
        }
    }
}

namespace {

// Nameplate layout, in pixels relative to the actor's projected position
constexpr float kHealthBarWidth = 50.0f;
constexpr float kHealthBarHeight = 6.0f;
constexpr float kHealthBarOffsetY = 28.0f;
constexpr float kNameOffsetY = 20.0f;
constexpr float kAuraIconSpacing = 2.0f;
constexpr float kAuraIconOffsetY = 36.0f;  // above the health bar
constexpr float kAuraBorderThickness = 2.0f;
constexpr float kLastCastIconGap = 4.0f;   // after the health bar
constexpr ImU32 kMissingIconColor = IM_COL32(60, 60, 60, 200);

void drawOutlinedText(ImDrawList* drawList, float x, float y, ImU32 color, const char* text) {
    const ImU32 outlineColor = IM_COL32(0, 0, 0, 255);
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            if (dx != 0 || dy != 0) {
                drawList->AddText(ImVec2(x + dx, y + dy), outlineColor, text);
            }
        }
    }
    drawList->AddText(ImVec2(x, y), color, text);
}

// One row of aura icons above the nameplate, with stack counts and remaining duration
void drawNameplateAuras(ImDrawList* drawList, const Nameplate3D& nameplate,
                        const NameplateSettings& nameplateSettings, const AuraDatabase& auraDb,
                        SpellIconLoader* spellIconLoader, uint32_t currentTime) {
    auto& spellSettings = spell_assignment::SpellAssignmentSettings::instance();

    std::vector<const AuraInstance*> shownAuras;
    for (const auto* aura : auraDb.getActiveAurasOnTarget(nameplate.guid, currentTime)) {
        if (spellSettings.isAuraEnabledForNameplate(aura->spell_id)) {
            shownAuras.push_back(aura);
        }
    }
    if (shownAuras.empty()) {
        return;
    }

    const float iconSize = nameplateSettings.auraIconSize;
    const ImU32 buffBorderColor = IM_COL32(0, 200, 0, 255);
    const ImU32 debuffBorderColor = IM_COL32(200, 50, 200, 255);

    float totalWidth = shownAuras.size() * iconSize + (shownAuras.size() - 1) * kAuraIconSpacing;
    float startX = nameplate.screenX - totalWidth * 0.5f;
    float iconY = nameplate.screenY - kAuraIconOffsetY - iconSize;

    for (size_t i = 0; i < shownAuras.size(); ++i) {
        const auto* aura = shownAuras[i];
        float iconX = startX + i * (iconSize + kAuraIconSpacing);

        ImTextureID iconTexture = spellIconLoader ? spellIconLoader->getSpellIcon(aura->spell_id) : 0;
        ImU32 borderColor = (aura->aura_type == AuraType::BUFF) ? buffBorderColor : debuffBorderColor;

        ImVec2 iconMin(iconX, iconY);
        ImVec2 iconMax(iconX + iconSize, iconY + iconSize);

        drawList->AddRectFilled(
            ImVec2(iconMin.x - kAuraBorderThickness, iconMin.y - kAuraBorderThickness),
            ImVec2(iconMax.x + kAuraBorderThickness, iconMax.y + kAuraBorderThickness),
            borderColor);

        if (iconTexture) {
            drawList->AddImage(iconTexture, iconMin, iconMax);
        } else {
            drawList->AddRectFilled(iconMin, iconMax, kMissingIconColor);
        }

        // Stack count in the bottom-right corner
        if (aura->current_stacks > 1) {
            char stackText[8];
            snprintf(stackText, sizeof(stackText), "%d", aura->current_stacks);
            ImVec2 stackPos(iconMax.x - 8.0f, iconMax.y - 12.0f);
            drawList->AddText(ImVec2(stackPos.x + 1, stackPos.y + 1), IM_COL32(0, 0, 0, 255), stackText);
            drawList->AddText(stackPos, IM_COL32(255, 255, 255, 255), stackText);
        }

        bool hasKnownDuration = (aura->removed_at_ms != UINT32_MAX);
        if (hasKnownDuration && static_cast<int32_t>(aura->removed_at_ms) > static_cast<int32_t>(currentTime)) {
            uint32_t remainingMs = aura->removed_at_ms - currentTime;
            float remainingSec = remainingMs / 1000.0f;

            char durationText[16];
            if (remainingSec < 10.0f) {
                snprintf(durationText, sizeof(durationText), "%.1f", remainingSec);
            } else {
                snprintf(durationText, sizeof(durationText), "%d", static_cast<int>(remainingSec));
            }

            ImVec2 textSize = ImGui::CalcTextSize(durationText);
            float textX = iconX + (iconSize - textSize.x) * 0.5f;
            float textY = iconY - textSize.y - 2.0f;
            drawOutlinedText(drawList, textX, textY, IM_COL32(255, 255, 255, 255), durationText);
        }
    }
}

// Green at full health, through yellow, to red when empty
void drawNameplateHealthBar(ImDrawList* drawList, const Nameplate3D& nameplate) {
    ImVec2 barPos(nameplate.screenX - kHealthBarWidth * 0.5f, nameplate.screenY - kHealthBarOffsetY);
    ImVec2 barEnd(barPos.x + kHealthBarWidth, barPos.y + kHealthBarHeight);

    drawList->AddRectFilled(barPos, barEnd, IM_COL32(80, 0, 0, 200));

    ImU32 healthColor;
    if (nameplate.healthPercent > 0.5f) {
        healthColor = IM_COL32(
            static_cast<int>((1.0f - (nameplate.healthPercent - 0.5f) * 2.0f) * 255),
            200, 0, 220);
    } else {
        healthColor = IM_COL32(
            255,
            static_cast<int>(nameplate.healthPercent * 2.0f * 200),
            0, 220);
    }

    drawList->AddRectFilled(
        barPos,
        ImVec2(barPos.x + kHealthBarWidth * nameplate.healthPercent, barPos.y + kHealthBarHeight),
        healthColor);

    drawList->AddRect(barPos, barEnd, IM_COL32(0, 0, 0, 200));
}

// Icon of the actor's most recent cast, to the right of the health bar
void drawNameplateLastCast(ImDrawList* drawList, const Nameplate3D& nameplate,
                           const NameplateSettings& nameplateSettings, const SpellCastDatabase& spellCastDb,
                           SpellIconLoader& spellIconLoader, uint32_t currentTime) {
    auto recentCasts = spellCastDb.getRecentCastsForActor(std::string(nameplate.guid), currentTime, 1);
    if (recentCasts.empty()) {
        return;
    }

    const float iconSize = nameplateSettings.lastCastIconSize;
    const float iconX = nameplate.screenX + kHealthBarWidth * 0.5f + kLastCastIconGap;
    const float iconY = nameplate.screenY - kHealthBarOffsetY - (iconSize - kHealthBarHeight) * 0.5f;

    ImVec2 iconMin(iconX, iconY);
    ImVec2 iconMax(iconX + iconSize, iconY + iconSize);

    ImTextureID iconTexture = spellIconLoader.getSpellIcon(recentCasts[0].spell_id);
    if (iconTexture) {
        drawList->AddImage(iconTexture, iconMin, iconMax);
    } else {
        drawList->AddRectFilled(iconMin, iconMax, kMissingIconColor);
    }

    drawList->AddRect(iconMin, iconMax, IM_COL32(0, 0, 0, 180));
}

void drawNameplateName(ImDrawList* drawList, const Nameplate3D& nameplate,
                       const NameplateSettings& nameplateSettings, const ActorColorGenerator* colorGenerator) {
    bool isPlayer = nameplate.guid.starts_with("Player-");
    const NameplateCategorySettings& catSettings = isPlayer
        ? nameplateSettings.playerSettings
        : nameplateSettings.otherSettings;

    std::string nameStr(nameplate.name);
    if (catSettings.maxNameLength > 0 && nameStr.length() > static_cast<size_t>(catSettings.maxNameLength)) {
        nameStr = nameStr.substr(0, catSettings.maxNameLength);
    }

    ImU32 textColor;
    if (catSettings.useActorColor && colorGenerator) {
        auto actorColor = colorGenerator->getCachedColor(nameplate.guid);
        textColor = IM_COL32(
            static_cast<int>(actorColor.r * 255),
            static_cast<int>(actorColor.g * 255),
            static_cast<int>(actorColor.b * 255),
            255);
    } else {
        textColor = IM_COL32(
            static_cast<int>(catSettings.textColor[0] * 255),
            static_cast<int>(catSettings.textColor[1] * 255),
            static_cast<int>(catSettings.textColor[2] * 255),
            static_cast<int>(catSettings.textColor[3] * 255));
    }

    ImFont* font = ImGui::GetFont();
    float savedScale = font->Scale;
    font->Scale *= catSettings.fontScale;

    ImVec2 textSize = ImGui::CalcTextSize(nameStr.c_str());
    ImVec2 textPos(nameplate.screenX - textSize.x * 0.5f, nameplate.screenY - kNameOffsetY);

    if (catSettings.showBackground) {
        ImVec2 bgMin(textPos.x - 3.0f, textPos.y - 1.0f);
        ImVec2 bgMax(textPos.x + textSize.x + 3.0f, textPos.y + textSize.y + 1.0f);
        drawList->AddRectFilled(bgMin, bgMax,
            IM_COL32(0, 0, 0, static_cast<int>(catSettings.backgroundOpacity * 255)), 2.0f);
    }

    drawList->AddText(ImVec2(textPos.x + 1, textPos.y + 1), IM_COL32(0, 0, 0, 255), nameStr.c_str());
    drawList->AddText(textPos, textColor, nameStr.c_str());

    font->Scale = savedScale;
}

} // namespace

void RenderingCoordinator::renderNameplateOverlay(const NameplateSettings& nameplateSettings,
                                                    FrameDataProvider* dataProvider) {
    if (!nameplateSettings.showNameplates) {
        return;
    }

    const auto& nameplates = sceneRenderer3D_->getNameplates();
    if (nameplates.empty()) {
        return;
    }

    const AuraDatabase* auraDb = dataProvider ? dataProvider->getAuraDatabase() : nullptr;
    const SpellCastDatabase* spellCastDb = dataProvider ? dataProvider->getSpellCastDatabase() : nullptr;
    const uint32_t currentTime = dataProvider ? dataProvider->getCurrentTime() : 0;

    ImDrawList* drawList = ImGui::GetBackgroundDrawList();
    for (const auto& nameplate : nameplates) {
        if (!nameplate.visible) {
            continue;
        }

        if (auraDb && nameplateSettings.showAuras) {
            drawNameplateAuras(drawList, nameplate, nameplateSettings, *auraDb, spellIconLoader_.get(), currentTime);
        }

        if (nameplateSettings.showHealthBars && nameplate.hasHealth) {
            drawNameplateHealthBar(drawList, nameplate);
        }

        if (nameplateSettings.showLastCast && spellCastDb && spellIconLoader_) {
            drawNameplateLastCast(drawList, nameplate, nameplateSettings, *spellCastDb, *spellIconLoader_, currentTime);
        }

        drawNameplateName(drawList, nameplate, nameplateSettings, colorGenerator_.get());
    }
}

void RenderingCoordinator::renderFacingIndicator(const FacingIndicatorSettings& facingSettings,
                                                   FrameDataProvider* dataProvider,
                                                   std::string_view selectedGuid,
                                                   VkExtent2D extent) {
    if (!facingSettings.enabled || selectedGuid.empty()) {
        return;
    }

    if (!dataProvider || !dataProvider->hasPositions() || !camera3D_) {
        return;
    }

    const auto& actorPositions = dataProvider->getActorPositions();
    auto posIt = actorPositions.find(selectedGuid);
    if (posIt == actorPositions.end() || !posIt->second.hasPosition) {
        return;
    }

    const InterpolatedPosition& pos = posIt->second;

    if (!pos.hasFacing) {
        return;
    }

    float facing = pos.facing;


    float viewMatrix[16];
    float projMatrix[16];
    float aspectRatio = static_cast<float>(extent.width) / static_cast<float>(extent.height);
    camera3D_->getViewMatrix(viewMatrix);
    camera3D_->getProjectionMatrix(projMatrix, aspectRatio);

    auto projectToScreen = [&](float worldX, float worldY, float worldZ = 0.0f) -> std::pair<ImVec2, bool> {
        float viewX = viewMatrix[0] * worldX + viewMatrix[4] * worldY + viewMatrix[8] * worldZ + viewMatrix[12];
        float viewY = viewMatrix[1] * worldX + viewMatrix[5] * worldY + viewMatrix[9] * worldZ + viewMatrix[13];
        float viewZ = viewMatrix[2] * worldX + viewMatrix[6] * worldY + viewMatrix[10] * worldZ + viewMatrix[14];
        float viewW = viewMatrix[3] * worldX + viewMatrix[7] * worldY + viewMatrix[11] * worldZ + viewMatrix[15];

        float clipX = projMatrix[0] * viewX + projMatrix[4] * viewY + projMatrix[8] * viewZ + projMatrix[12] * viewW;
        float clipY = projMatrix[1] * viewX + projMatrix[5] * viewY + projMatrix[9] * viewZ + projMatrix[13] * viewW;
        float clipW = projMatrix[3] * viewX + projMatrix[7] * viewY + projMatrix[11] * viewZ + projMatrix[15] * viewW;

        if (clipW <= 0.0f) {
            return {{0, 0}, false};
        }

        float ndcX = clipX / clipW;
        float ndcY = clipY / clipW;

        float screenX = (ndcX + 1.0f) * 0.5f * extent.width;
        float screenY = (ndcY + 1.0f) * 0.5f * extent.height;

        return {{screenX, screenY}, true};
    };

    constexpr float coneHalfAngle = 0.4f;  // ~23 degrees half-angle for cone spread
    float coneLength = facingSettings.size;  // Size in world units (yards)

    float apexX = pos.x;
    float apexY = pos.y;

    // Facing indicator direction matches model rotation matrix
    // Standard rotation: facing=0 points +X, facing=π/2 points +Y
    float baseLeftX = apexX + std::cos(facing + coneHalfAngle) * coneLength;
    float baseLeftY = apexY + std::sin(facing + coneHalfAngle) * coneLength;
    float baseRightX = apexX + std::cos(facing - coneHalfAngle) * coneLength;
    float baseRightY = apexY + std::sin(facing - coneHalfAngle) * coneLength;

    auto [apexScreen, apexVisible] = projectToScreen(apexX, apexY);
    auto [baseLeftScreen, baseLeftVisible] = projectToScreen(baseLeftX, baseLeftY);
    auto [baseRightScreen, baseRightVisible] = projectToScreen(baseRightX, baseRightY);

    if (!apexVisible || !baseLeftVisible || !baseRightVisible) {
        return;
    }

    ImU32 coneColor = IM_COL32(
        static_cast<int>(facingSettings.color[0] * 255),
        static_cast<int>(facingSettings.color[1] * 255),
        static_cast<int>(facingSettings.color[2] * 255),
        static_cast<int>(facingSettings.opacity * 255));

    ImU32 outlineColor = IM_COL32(0, 0, 0, static_cast<int>(facingSettings.opacity * 200));

    ImDrawList* drawList = ImGui::GetBackgroundDrawList();

    drawList->AddTriangleFilled(
        apexScreen,
        baseLeftScreen,
        baseRightScreen,
        coneColor);

    drawList->AddTriangle(
        apexScreen,
        baseLeftScreen,
        baseRightScreen,
        outlineColor,
        2.0f);
}
