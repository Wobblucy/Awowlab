#pragma once
#include <vulkan/vulkan.h>
#include <memory>
#include <vector>
#include <string>
#include <string_view>
#include <unordered_map>

class VulkanPipeline3D;
class VulkanBufferManager3D;
class WorldMapTextureManager;
class SpecIconLoader;
class CustomShapeRegistry;
class Camera3D;
class ActorColorGenerator;
class ActorAppearanceSettings;
struct InterpolatedPosition;
struct InterpolatedResource;
struct ActorVisibilitySettings;
enum class ActorType;

// World map bounds from MAP_CHANGE event
struct MapBounds {
    float x1 = 0.0f, y1 = 0.0f;  // Top-left corner
    float x2 = 0.0f, y2 = 0.0f;  // Bottom-right corner
    uint32_t mapId = 0;
    std::string mapName;
    bool isValid = false;
};

// Active aura to display on a nameplate
struct Nameplate3D {
    std::string_view guid;      // Actor GUID for resource lookup
    std::string_view name;
    float screenX;
    float screenY;
    float healthPercent;        // 0.0 to 1.0
    bool hasHealth;             // True if health data is available
    bool visible;
};

class SceneRenderer3D {
public:
    SceneRenderer3D();
    ~SceneRenderer3D();

    SceneRenderer3D(const SceneRenderer3D&) = delete;
    SceneRenderer3D& operator=(const SceneRenderer3D&) = delete;

    bool initialize(VkDevice device, VkPhysicalDevice physicalDevice, VkRenderPass renderPass,
                   VkQueue graphicsQueue, VkCommandPool commandPool);

    void cleanup(VkDevice device);

    bool isInitialized() const { return isInitialized_; }

    // Set appearance settings (optional - if not set, uses default appearance)
    void setAppearanceSettings(std::shared_ptr<ActorAppearanceSettings> settings);

    void setCustomShapeRegistry(std::shared_ptr<CustomShapeRegistry> registry);

    CustomShapeRegistry* getCustomShapeRegistry() const;

    // Set current map bounds for floor texture rendering
    void setMapBounds(uint32_t mapId, const std::string& mapName,
                      float x1, float y1, float x2, float y2);

    // Set map bounds with pre-loaded texture (for encounter-specific textures)
    // Uses existing floor buffer infrastructure, just points to external texture
    void setMapBoundsWithTexture(float x1, float y1, float x2, float y2,
                                  VkImageView imageView, VkSampler sampler,
                                  uint32_t textureWidth, uint32_t textureHeight);

    // Clear map bounds (revert to default grid floor)
    void clearMapBounds();

    // Set fallback grid floor when no map texture is available
    // Creates a 300x300 grid centered on the given coordinates
    void setFallbackFloor(float centerX, float centerY);

    // Preload a map texture into cache for smooth map transitions
    void preloadMapTexture(uint32_t mapId);

    // Get current world map info for 2D minimap rendering
    // Returns true if valid world map is available, false otherwise
    bool getWorldMapInfo(float& minX, float& maxX, float& minY, float& maxY, void*& textureId) const;

    void render(VkDevice device,
                VkCommandBuffer commandBuffer,
                VkExtent2D extent,
                const std::unordered_map<std::string_view, InterpolatedPosition>& actorPositions,
                const std::unordered_map<std::string_view, InterpolatedResource>& actorResources,
                const std::unordered_map<std::string_view, std::string_view>& guidToName,
                const std::shared_ptr<ActorColorGenerator>& colorGen,
                const std::shared_ptr<Camera3D>& camera,
                const ActorVisibilitySettings& visibilitySettings,
                bool showNameplates,
                std::string_view selectedPlayerGuid = "",
                float animationTime = 0.0f);

    const std::vector<Nameplate3D>& getNameplates() const { return nameplates_; }

    // Get camera UBO for sharing with other renderers (e.g., annotations)
    VkBuffer getCameraUBO() const;

    // Clear cached textured model descriptor sets and reset their pool (call
    // when models are unloaded, with the device idle)
    void clearTexturedModelDescriptorCache();

    // Process deferred texture deletions - call once per frame
    void processTextureCleanup();

private:
    std::unique_ptr<VulkanPipeline3D> pipeline_;
    std::unique_ptr<VulkanBufferManager3D> bufferManager_;
    std::unique_ptr<WorldMapTextureManager> worldMapTextureManager_;
    std::unique_ptr<SpecIconLoader> specIconLoader_;
    std::shared_ptr<ActorAppearanceSettings> appearanceSettings_;

    // Sphere pipeline descriptors (camera UBO only)
    VkDescriptorPool descriptorPool_;
    VkDescriptorSet descriptorSet_;

    // Floor pipeline descriptors (camera UBO + texture sampler)
    VkDescriptorPool floorDescriptorPool_;
    VkDescriptorSet floorDescriptorSet_;

    // ClassIcon pipeline descriptors (camera UBO + spec texture per descriptor set)
    VkDescriptorPool classIconDescriptorPool_;
    std::unordered_map<uint16_t, VkDescriptorSet> specDescriptorSets_;

    // Textured model pipeline descriptors (camera UBO + model texture per descriptor set)
    VkDescriptorPool texturedModelDescriptorPool_;
    std::unordered_map<std::string, VkDescriptorSet> texturedModelDescriptorSets_;

    // Vulkan context stored for lazy descriptor set allocation
    VkQueue graphicsQueue_;
    VkCommandPool commandPool_;

    // Floor plane buffers (dynamic, based on map bounds)
    VkBuffer floorVertexBuffer_;
    VkDeviceMemory floorVertexMemory_;
    VkBuffer floorIndexBuffer_;
    VkDeviceMemory floorIndexMemory_;
    uint32_t floorIndexCount_;
    bool hasFloorBuffers_;

    MapBounds currentMapBounds_;

    // External texture override (for encounter textures loaded by EncounterTextureManager)
    VkImageView externalImageView_ = VK_NULL_HANDLE;
    VkSampler externalSampler_ = VK_NULL_HANDLE;
    bool usingExternalTexture_ = false;

    // Fallback grid floor (when no map texture is available)
    bool usingFallbackGrid_ = false;

    // Selection ring buffers (for pulsing ring effect)
    VkBuffer ringVertexBuffer_;
    VkDeviceMemory ringVertexMemory_;
    VkBuffer ringIndexBuffer_;
    VkDeviceMemory ringIndexMemory_;
    uint32_t ringIndexCount_;
    bool hasRingBuffers_;

    // Vulkan context for floor buffer updates
    VkDevice device_;
    VkPhysicalDevice physicalDevice_;

    bool isInitialized_;

    std::vector<Nameplate3D> nameplates_;

    bool createDescriptorPool(VkDevice device);
    bool createDescriptorSet(VkDevice device);
    bool createFloorDescriptorPool(VkDevice device);
    bool createFloorDescriptorSet(VkDevice device);
    void updateFloorDescriptorSet(VkDevice device);
    void writeFloorTexture(VkDevice device, VkImageView imageView, VkSampler sampler);

    VkDescriptorSet allocateTexturedDescriptorSet(VkDescriptorPool pool, VkDescriptorSetLayout layout,
                                                  VkImageView imageView, VkSampler sampler);

    bool createClassIconDescriptorPool(VkDevice device);
    VkDescriptorSet getOrCreateSpecDescriptorSet(uint16_t specId);
    void updateCameraMatrices(VkDevice device, const std::shared_ptr<Camera3D>& camera, float aspectRatio);

    bool createTexturedModelDescriptorPool(VkDevice device);
    VkDescriptorSet getOrCreateTexturedModelDescriptorSet(const struct CustomShape& shape);
    VkDescriptorSet getOrCreateMaterialDescriptorSet(const struct CustomShape& shape, size_t materialIndex);

    bool createFloorBuffers(VkDevice device, VkPhysicalDevice physicalDevice,
                            float x1, float y1, float x2, float y2,
                            uint32_t textureWidth, uint32_t textureHeight,
                            bool isEncounterTexture = false);
    void cleanupFloorBuffers(VkDevice device);

    void renderFloor(VkCommandBuffer commandBuffer, const std::shared_ptr<Camera3D>& camera);

    bool createRingBuffers(VkDevice device, VkPhysicalDevice physicalDevice);
    void cleanupRingBuffers(VkDevice device);
    void renderSelectionRing(VkCommandBuffer commandBuffer, float worldX, float worldY, float animationTime);

    void renderActors(VkCommandBuffer commandBuffer,
                      const std::unordered_map<std::string_view, InterpolatedPosition>& actorPositions,
                      const std::shared_ptr<ActorColorGenerator>& colorGen,
                      const std::shared_ptr<Camera3D>& camera,
                      const ActorVisibilitySettings& visibilitySettings,
                      std::string_view selectedPlayerGuid = "");

    void renderClassIconActors(VkCommandBuffer commandBuffer,
                               const std::unordered_map<std::string_view, InterpolatedPosition>& actorPositions,
                               const std::shared_ptr<ActorColorGenerator>& colorGen,
                               const std::shared_ptr<Camera3D>& camera,
                               const ActorVisibilitySettings& visibilitySettings,
                               std::string_view selectedPlayerGuid = "");

    void renderTexturedModelActors(VkCommandBuffer commandBuffer,
                                   const std::unordered_map<std::string_view, InterpolatedPosition>& actorPositions,
                                   const std::shared_ptr<ActorColorGenerator>& colorGen,
                                   const std::shared_ptr<Camera3D>& camera,
                                   const ActorVisibilitySettings& visibilitySettings,
                                   std::string_view selectedPlayerGuid = "");

    void projectNameplates(const std::unordered_map<std::string_view, InterpolatedPosition>& actorPositions,
                           const std::unordered_map<std::string_view, InterpolatedResource>& actorResources,
                           const std::unordered_map<std::string_view, std::string_view>& guidToName,
                           const std::shared_ptr<ActorColorGenerator>& colorGen,
                           const std::shared_ptr<Camera3D>& camera,
                           float aspectRatio,
                           VkExtent2D extent,
                           const ActorVisibilitySettings& visibilitySettings);
};
