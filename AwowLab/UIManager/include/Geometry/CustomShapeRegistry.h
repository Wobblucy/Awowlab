#pragma once
#include "Vulkan/Buffers/VulkanBufferManager3D.h"
#include "Vulkan/Textures/VulkanTextureManager.h"
#include "Geometry/CustomModelLoader.h"
#include "Geometry/BakedModelPack.h"
#include <unordered_map>
#include <string>
#include <vector>
#include <filesystem>
#include <memory>
#include <thread>
#include <mutex>
#include <atomic>
#include <queue>
#include <condition_variable>
#include <functional>

enum class ModelLoadState {
    NotLoaded,      // In availableModels_, not yet requested
    Queued,         // Waiting for background thread
    Parsing,        // Background thread actively parsing
    PendingGPU,     // Parsed, waiting for main thread GPU upload
    Ready,          // Fully loaded, can render
    Failed          // Load error occurred
};

// Decoded texture data for thread-safe handoff (pixels decoded on background thread)
struct DecodedTexture {
    std::string path;
    std::vector<unsigned char> pixels;  // RGBA pixel data
    int width = 0;
    int height = 0;
};

// Intermediate struct for thread-safe handoff between worker and main thread
struct PendingModel {
    std::string name;
    LoadedModel model;
    std::vector<DecodedTexture> decodedTextures;  // Pre-decoded texture pixels
};

// Model rotation settings (degrees) and Z-axis offset
struct ModelRotation {
    float x = -90.0f;     // Rotation around X axis (degrees) - default corrects Y-up to Z-up
    float y = 0.0f;       // Rotation around Y axis (degrees)
    float z = 0.0f;       // Rotation around Z axis (degrees)
    float zOffset = 0.0f; // Vertical offset (world units)
};

// Per-material texture info for multi-material models
struct MaterialTextureInfo {
    TextureHandle texture{};         // GPU texture resources
    std::string texturePath;         // For reload detection
    uint32_t startIndex = 0;         // First index in index buffer for this material
    uint32_t indexCount = 0;         // Number of indices for this material
    bool hasTexture = false;
};

// Represents a loaded custom shape with its GPU resources
struct CustomShape {
    std::string name;                    // Model name (filename without .obj)
    std::string fullPath;                // For change detection
    ShapeBuffer buffer;                  // GPU vertex/index buffers
    std::filesystem::file_time_type lastModified;
    bool isValid = false;

    std::vector<MaterialTextureInfo> materials;

    // Mirror of the first textured material, for single-texture callers
    std::string diffuseTexturePath;      // For reload detection
    TextureHandle texture;               // GPU texture resources
    bool hasTexture = false;
};

class CustomShapeRegistry {
public:
    CustomShapeRegistry() = default;

    CustomShapeRegistry(VkDevice device, VkPhysicalDevice physicalDevice);

    ~CustomShapeRegistry();

    // Initialize with Vulkan device (needed for buffer creation)
    // Starts background worker thread
    void initialize(VkDevice device, VkPhysicalDevice physicalDevice);

    void cleanup(VkDevice device);

    // Clear loaded models but keep available models list (for encounter swap)
    void clearLoadedModels();

    // Scan directory for available models (fast, no loading)
    // Call this at startup to discover what models are available
    void scanDirectory(const std::string& directory);

    // Load a baked model pack (.awmp). Its models resolve instantly in
    // getShape() - straight memcpy to GPU, no OBJ parse, no worker thread.
    // A same-named OBJ in the custom models directory takes precedence so
    // users can still override any baked model by dropping a file in.
    bool loadBakedPack(const std::string& path);

    // npcId -> model-name defaults table embedded in the loaded pack.
    const std::vector<std::pair<uint32_t, std::string>>& getBakedAssignments() const {
        return bakedPack_.assignments();
    }

    // Scan the directory and load or update every custom model
    // Call this when user opens the shape dropdown for full refresh
    void refreshFromDirectory(const std::string& directory);

    // Called after refreshFromDirectory has replaced or removed shapes, so
    // whoever caches descriptor sets for shape textures can drop them.
    void setShapesReplacedCallback(std::function<void()> callback) {
        onShapesReplaced_ = std::move(callback);
    }

    // Get a custom shape by name (non-blocking, queues async load if needed)
    // Returns nullptr if model doesn't exist or is still loading
    // Caller should handle nullptr gracefully (e.g., use placeholder shape)
    const CustomShape* getShape(const std::string& name);

    // Process pending model uploads on main thread (call once per frame)
    // Uploads up to MAX_UPLOADS_PER_FRAME models to GPU
    void processLoadQueue();

    ModelLoadState getLoadState(const std::string& name) const;

    bool hasShape(const std::string& name) const;

    // Get list of all available custom shape names (sorted)
    std::vector<std::string> getShapeNames() const;

    size_t getShapeCount() const { return shapes_.size(); }

    // Get load errors from last refresh
    const std::vector<ModelLoadError>& getLoadErrors() const { return loadErrors_; }

    // Check if directory needs refresh (any files changed)
    bool needsRefresh(const std::string& directory) const;

    const ShapeBuffer* getShapeBuffer(const std::string& name) const;

    // Model rotation management (per-model, persistent)
    void setModelRotation(const std::string& name, const ModelRotation& rotation);
    ModelRotation getModelRotation(const std::string& name) const;
    const std::unordered_map<std::string, ModelRotation>& getAllRotations() const { return modelRotations_; }

    void saveRotations();
    void loadRotations();

    // Texture manager for loading textures from MTL files
    void setTextureManager(std::shared_ptr<VulkanTextureManager> textureManager);

private:
    VkDevice device_ = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;

    std::unordered_map<std::string, CustomShape> shapes_;           // Loaded models (GPU buffers created)
    std::unordered_map<std::string, std::filesystem::path> availableModels_;  // Scanned models (not yet loaded)
    std::unordered_map<std::string, ModelRotation> modelRotations_;  // Per-model rotation settings
    std::vector<ModelLoadError> loadErrors_;
    std::string lastScannedDirectory_;
    std::string customModelsDirectory_;  // Stored for lazy loading

    CustomModelLoader loader_;  // Main thread only; the worker has its own
    std::shared_ptr<VulkanTextureManager> textureManager_;
    std::function<void()> onShapesReplaced_;

    std::thread workerThread_;
    std::atomic<bool> shutdownRequested_{false};
    std::atomic<bool> workerRunning_{false};

    mutable std::mutex queueMutex_;
    std::condition_variable queueCV_;
    struct QueuedLoad {
        std::string name;
        std::filesystem::path path;
    };
    std::queue<QueuedLoad> loadQueue_;               // Models to parse

    mutable std::mutex pendingMutex_;
    std::queue<PendingModel> pendingModels_;         // Parsed, awaiting GPU upload

    std::unordered_map<std::string, ModelLoadState> loadStates_;

    void workerThreadFunc();

    // Queue a model for async loading (non-blocking)
    void queueModelLoad(const std::string& name, const std::filesystem::path& path);

    // Synchronous single-model load
    bool loadSingleModel(const std::string& name, const std::filesystem::path& path);

    bool createShapeBuffer(const LoadedModel& model, ShapeBuffer& outBuffer);

    // Create a Ready shape from a baked pack view (synchronous fast path).
    // Uploads straight from the view; buffers are host-visible, so this is a
    // plain memcpy and safe to call on the main thread.
    const CustomShape* loadBakedShape(const std::string& name, const BakedModelView& view);

    // Baked models (instant load, no parsing). Disk OBJs of the same name win.
    BakedModelPack bakedPack_;
};
