#pragma once
#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include <vulkan/vulkan.h>
#include <imgui.h>
#include "UI/ActorVisibilitySettings.h"
#include "UI/CameraSettings.h"
#include "UI/NameplateSettings.h"
#include "UI/FacingIndicatorSettings.h"
#include "Rendering/Minimap2D.h"

class SceneRenderer3D;
class SpellIconLoader;
class CustomShapeRegistry;
class Camera3D;
class CameraController;
class ActorColorGenerator;
class ActorAppearanceSettings;
class FrameDataProvider;
class AnnotationRenderer;
class AnnotationManager;
#include "Animation/ActorPositionInterpolator.h"  // For InterpolatedPosition

// Coordinates 3D and 2D rendering operations
class RenderingCoordinator {
public:
    RenderingCoordinator();
    ~RenderingCoordinator();

    RenderingCoordinator(const RenderingCoordinator&) = delete;
    RenderingCoordinator& operator=(const RenderingCoordinator&) = delete;

    bool initialize3D(VkDevice device, VkPhysicalDevice physicalDevice, VkRenderPass renderPass,
                      VkQueue graphicsQueue, VkCommandPool commandPool);

    bool initializeSpellIcons(VkDevice device, VkPhysicalDevice physicalDevice,
                              VkQueue graphicsQueue, VkCommandPool commandPool,
                              VkDescriptorPool descriptorPool);

    void cleanup3D(VkDevice device);

    // Set dependencies
    void setCamera(std::shared_ptr<Camera3D> camera);
    void setCameraController(std::shared_ptr<CameraController> cameraController);
    void setColorGenerator(std::shared_ptr<ActorColorGenerator> colorGen);
    void setAppearanceSettings(std::shared_ptr<ActorAppearanceSettings> settings);

    void setMinimapBounds(int32_t minX, int32_t maxX, int32_t minY, int32_t maxY);

    // Set world map for 2D minimap rendering
    void setMinimapWorldMap(float minX, float maxX, float minY, float maxY, void* textureId);
    void clearMinimapWorldMap();

    // Sync minimap world map from 3D scene (call after map changes)
    void syncMinimapWorldMap();

    // Set worldmap bounds for floor texture rendering
    void setMapBounds(uint32_t mapId, const std::string& mapName,
                      float x1, float y1, float x2, float y2);

    // Set worldmap bounds with pre-loaded texture (for encounter-specific textures)
    void setMapBoundsWithTexture(float x1, float y1, float x2, float y2,
                                  VkImageView imageView, VkSampler sampler,
                                  uint32_t textureWidth, uint32_t textureHeight);

    // Clear worldmap bounds (revert to default floor)
    void clearMapBounds();

    // Preload spell icons for a set of spell IDs
    void preloadSpellIcons(const std::vector<uint32_t>& spellIds);

    // Preload a map texture for proactive loading before map switch
    void preloadMapTexture(uint32_t mapId);

    // deltaTime in seconds for frame-rate-independent camera smoothing
    void render3D(VkDevice device, VkCommandBuffer commandBuffer, VkExtent2D extent,
                  FrameDataProvider* dataProvider,
                  const ActorVisibilitySettings& visibilitySettings,
                  const CameraSettings& cameraSettings,
                  const NameplateSettings& nameplateSettings,
                  const FacingIndicatorSettings& facingIndicatorSettings,
                  std::string_view selectedGuid,
                  float deltaTime = 0.016f);

    void renderMinimap(ImDrawList* drawList, ImVec2 canvasPos, ImVec2 canvasSize,
                       FrameDataProvider* dataProvider,
                       const ActorVisibilitySettings& visibilitySettings,
                       const NameplateSettings& nameplateSettings,
                       std::string_view selectedGuid);

    // Handle minimap input (pan/zoom)
    void handleMinimapInput(ImVec2 canvasSize);

    // Render nameplate overlay using ImGui foreground draw list
    void renderNameplateOverlay(const NameplateSettings& nameplateSettings,
                                 FrameDataProvider* dataProvider);

    // Render facing indicator for selected actor (3D floor-projected cone)
    void renderFacingIndicator(const FacingIndicatorSettings& facingSettings,
                                FrameDataProvider* dataProvider,
                                std::string_view selectedGuid,
                                VkExtent2D extent);

    SpellIconLoader* getSpellIconLoader() { return spellIconLoader_.get(); }

    // Process deferred texture deletions - call once per frame
    // Ensures textures aren't destroyed while still in use by GPU
    void processTextureCleanup();

    // Set custom shape registry for loading OBJ files
    void setCustomShapeRegistry(std::shared_ptr<CustomShapeRegistry> registry);

    CustomShapeRegistry* getCustomShapeRegistry();

    void setAnnotationManager(AnnotationManager* manager);

    // Get annotation renderer for initialization (called by MapVisualizer after Vulkan setup)
    AnnotationRenderer* getAnnotationRenderer() { return annotationRenderer_.get(); }

    bool isInitialized() const;

    // Clear cached textured model descriptor sets and reset their pool (call
    // when models are unloaded, with the device idle)
    void clearTexturedModelDescriptorCache();

private:
    std::unique_ptr<SceneRenderer3D> sceneRenderer3D_;
    std::shared_ptr<Minimap2D> minimap2D_;
    std::unique_ptr<SpellIconLoader> spellIconLoader_;
    std::unique_ptr<AnnotationRenderer> annotationRenderer_;

    std::shared_ptr<Camera3D> camera3D_;
    std::shared_ptr<CameraController> cameraController_;
    std::shared_ptr<ActorColorGenerator> colorGenerator_;
    AnnotationManager* annotationManager_ = nullptr;  // Non-owning pointer

    // Reusable buffer for minimap positions
    std::vector<MinimapActorPosition> minimapPositionsBuffer_;

    // Reusable buffer for filtered actor positions (excludes dead actors)
    std::unordered_map<std::string_view, InterpolatedPosition> filteredPositionsBuffer_;
};
