#pragma once
#include "Vulkan/Core/VulkanInstance.h"
#include "Vulkan/Device/VulkanDevice.h"
#include "Vulkan/Swapchain/VulkanSwapchain.h"
#include "Vulkan/Commands/VulkanCommands.h"
#include "Vulkan/Sync/VulkanSync.h"
#include "Vulkan/Swapchain/VulkanDepthBuffer.h"
#include "Core/ImGuiManager.h"
#include <memory>
#include <vector>
#include <unordered_map>
#include <string>
#include <cstdint>
#include <functional>

struct GLFWwindow;
struct ActorEvent;
struct CombatantInfoEvent;
struct MapChangeEvent;
class EventDatabase;
class SpellCastDatabase;
class ResourceDatabase;
class CombatDatabase;
class DeathDatabase;
class AuraDatabase;
class DispelDatabase;
class AvoidanceDatabase;
class EmoteDatabase;
class UIEncounterSelector;
class UILandingPage;
class UIWarcraftLogsPanel;

class VulkanRenderer {
public:
    VulkanRenderer();
    ~VulkanRenderer();

    VulkanRenderer(const VulkanRenderer&) = delete;
    VulkanRenderer& operator=(const VulkanRenderer&) = delete;

    bool initialize(GLFWwindow* window);

    void loadEventDatabase(const EventDatabase* db);

    void loadSpellCastDatabase(const SpellCastDatabase* db);

    bool loadSpellGCDData(const std::string& csvPath);

    void loadResourceDatabase(const ResourceDatabase* db);

    void loadCombatDatabase(const CombatDatabase* db);

    void loadDeathDatabase(const DeathDatabase* db);

    void loadAuraDatabase(const AuraDatabase* db);

    void loadDispelDatabase(const DispelDatabase* db);

    void loadAvoidanceDatabase(const AvoidanceDatabase* db);

    void loadEmoteDatabase(const EmoteDatabase* db);

    // Load combatant info (spec IDs) for role-based rendering
    void loadCombatantInfo(const std::vector<CombatantInfoEvent>& combatantInfo);

    // Generate fallback colors for actors without COMBATANT_INFO
    void generateFallbackColors();

    // Load map change events for worldmap floor textures
    void loadMapChangeEvents(const std::vector<MapChangeEvent>& mapChanges, uint32_t encounterId = 0);

    void loadSpellNameMap(const std::unordered_map<uint32_t, std::string>& spellNameMap);

    // Load GUID to name map for resolving pet/guardian names
    void loadGuidToNameMap(const std::unordered_map<std::string, std::string>& guidToNameMap);
    // WCL actor id -> creature id (their GUIDs encode the actor id, not the
    // npc id). Empty for real combat logs.
    void loadActorNpcIdMap(const std::unordered_map<int32_t, uint32_t>& actorIdToNpcId);

    // Load health scaling data for mob size scaling
    void loadHealthScalingData(uint64_t avgPlayerHealth,
                               const std::unordered_map<std::string, uint64_t>& actorMaxHealth);

    // Store ExtractedData reference for snapshot export
    void setExtractedData(const struct ExtractedData* data);

    // Set callback for snapshot export requests
    void setExportCallback(std::function<void(
        const struct ExtractedData*,
        const std::vector<struct Annotation>&,
        const std::vector<struct AutoAnnotationTrigger>&,
        const std::vector<struct CachedAnnotationEvent>&,
        int32_t, int32_t)> callback);

    void drawFrame();

    // Render a frame with encounter selector UI (returns true if selection made)
    bool drawFrameWithEncounterSelector(UIEncounterSelector* selector);

    // Render a frame with landing page UI (returns true if selection made)
    bool drawFrameWithLandingPage(UILandingPage* landingPage);

    // Render a frame with WarcraftLogs fight selector UI (returns true if selection made)
    bool drawFrameWithFightSelector(UIWarcraftLogsPanel* fightSelector);

    // Render a frame with progress overlay (for long-running operations)
    void drawFrameWithProgress(const std::string& title, const std::string& message, float progress);

    // Render a frame with custom ImGui callback (for standalone visualizers)
    void drawFrameWithImGuiCallback(std::function<void()> renderCallback);

    // Render a frame with 3D rendering + ImGui (for PlayAlong visualizer)
    void drawFrameWith3DAndImGui(
        std::function<void(VkCommandBuffer, VkExtent2D)> render3DCallback,
        std::function<void()> imguiCallback);

    void handleResize();

    // Navigation control
    bool shouldReturnToEncounterSelector() const;
    bool shouldReturnToLandingPage() const;
    bool shouldRefreshEncounters() const;
    void resetNavigationFlags();
    void resetRefreshFlag();

    void unloadDatabases();

    // Wait for GPU to complete all pending work (for clean transitions)
    void waitForDeviceIdle();

    // Vulkan resource accessors (for PlayAlong and other standalone renderers)
    VkDevice getDevice() const;
    VkPhysicalDevice getPhysicalDevice() const;
    VkRenderPass getRenderPass() const { return renderPass; }
    VkQueue getGraphicsQueue() const;
    VkCommandPool getCommandPool() const;

    // Get spell icon loader for external use (e.g., PlayAlong mode)
    class SpellIconLoader* getSpellIconLoader();

    // Get auto-annotation trigger manager for direct access (e.g., snapshot import)
    AutoAnnotationTriggerManager* getAutoAnnotationTriggerManager();

private:
    void cleanup();
    void cleanupSwapchain();
    bool recreateSwapchain();

    bool createRenderPass();
    bool createFramebuffers();
    bool recordCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex);

    // Frame rendering helpers (reduce duplication across drawFrame* methods)
    struct FrameAcquireResult {
        bool success;
        uint32_t imageIndex;
        VkCommandBuffer commandBuffer;
    };
    FrameAcquireResult beginFrame();
    bool beginRenderPass(VkCommandBuffer cmd, uint32_t imageIndex, const VkClearColorValue& clearColor);
    void endRenderPassAndSubmit(VkCommandBuffer cmd, uint32_t imageIndex);
    void submitAndPresent(VkCommandBuffer cmd, uint32_t imageIndex);  // When render pass already ended
    void submitMinimalFrame(VkCommandBuffer cmd, uint32_t imageIndex);  // Recovery: submit empty frame to maintain sync

    // The GPU is gone and nothing can be drawn again. Tells the user, then
    // closes the window so every render loop exits through normal shutdown.
    void handleDeviceLost(const char* where);
    bool deviceLost = false;

    std::unique_ptr<VulkanInstance> vulkanInstance;
    std::unique_ptr<VulkanDevice> vulkanDevice;
    std::unique_ptr<VulkanSwapchain> vulkanSwapchain;
    std::unique_ptr<VulkanCommands> vulkanCommands;
    std::unique_ptr<VulkanSync> vulkanSync;
    std::unique_ptr<VulkanDepthBuffer> vulkanDepthBuffer;
    std::unique_ptr<ImGuiManager> imguiManager;

    VkRenderPass renderPass;
    std::vector<VkFramebuffer> swapchainFramebuffers;

    GLFWwindow* window;

    static constexpr int MAX_FRAMES_IN_FLIGHT = 2;
    size_t currentFrame;
    bool framebufferResized;
};
