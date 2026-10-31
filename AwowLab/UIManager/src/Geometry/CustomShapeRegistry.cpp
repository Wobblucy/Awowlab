#include "Geometry/CustomShapeRegistry.h"
#include "Core/UnifiedSettings.h"
#include <algorithm>
#include <cctype>
#include "Core/ErrorLogger.h"
#include <cstring>
#include <cstdio>
#include <sstream>
#include <unordered_set>

// stb_image decodes PNGs on the worker thread; the implementation lives in VulkanTextureManager.cpp
#include "stb_image.h"

CustomShapeRegistry::CustomShapeRegistry(VkDevice device, VkPhysicalDevice physicalDevice)
    : device_(device), physicalDevice_(physicalDevice) {
    if (!workerRunning_) {
        shutdownRequested_ = false;
        workerRunning_ = true;
        workerThread_ = std::thread(&CustomShapeRegistry::workerThreadFunc, this);
    }
}

CustomShapeRegistry::~CustomShapeRegistry() {
    shutdownRequested_ = true;
    queueCV_.notify_all();
    if (workerThread_.joinable()) {
        workerThread_.join();
    }
}

void CustomShapeRegistry::initialize(VkDevice device, VkPhysicalDevice physicalDevice) {
    device_ = device;
    physicalDevice_ = physicalDevice;

    if (!workerRunning_) {
        shutdownRequested_ = false;
        workerRunning_ = true;
        workerThread_ = std::thread(&CustomShapeRegistry::workerThreadFunc, this);
    }
}

void CustomShapeRegistry::cleanup(VkDevice device) {
    shutdownRequested_ = true;
    queueCV_.notify_all();
    if (workerThread_.joinable()) {
        workerThread_.join();
    }
    workerRunning_ = false;

    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        while (!loadQueue_.empty()) loadQueue_.pop();
    }
    {
        std::lock_guard<std::mutex> lock(pendingMutex_);
        while (!pendingModels_.empty()) pendingModels_.pop();
        loadStates_.clear();
    }

    for (auto& [name, shape] : shapes_) {
        shape.buffer.cleanup(device);
        for (auto& mat : shape.materials) {
            if (mat.hasTexture && textureManager_) {
                textureManager_->destroyTexture(mat.texture);
            }
        }
    }
    shapes_.clear();
    availableModels_.clear();
    loadErrors_.clear();
}

void CustomShapeRegistry::clearLoadedModels() {
    // Frees GPU resources on encounter swap; availableModels_ is kept

    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        while (!loadQueue_.empty()) loadQueue_.pop();
    }
    {
        std::lock_guard<std::mutex> lock(pendingMutex_);
        while (!pendingModels_.empty()) pendingModels_.pop();
        loadStates_.clear();
    }

    for (auto& [name, shape] : shapes_) {
        shape.buffer.cleanup(device_);
        for (auto& mat : shape.materials) {
            if (mat.hasTexture && textureManager_) {
                textureManager_->destroyTexture(mat.texture);
            }
        }
    }
    shapes_.clear();
    loadErrors_.clear();
}

void CustomShapeRegistry::setTextureManager(std::shared_ptr<VulkanTextureManager> textureManager) {
    textureManager_ = textureManager;
}

void CustomShapeRegistry::scanDirectory(const std::string& directory) {
    namespace fs = std::filesystem;

    customModelsDirectory_ = directory;
    availableModels_.clear();

    if (!fs::exists(directory)) {
        return;
    }

    try {
        for (const auto& entry : fs::recursive_directory_iterator(directory)) {
            if (entry.is_regular_file()) {
                auto ext = entry.path().extension().string();
                std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                if (ext == ".obj") {
                    std::string name = entry.path().stem().string();
                    availableModels_[name] = entry.path();
                }
            }
        }
    } catch (const std::exception& e) {
        AWOW_LOG_WARNING("Models", std::string("Error scanning model directory: ") + e.what());
    }
}

void CustomShapeRegistry::refreshFromDirectory(const std::string& directory) {
    if (device_ == VK_NULL_HANDLE) {
        AWOW_LOG_WARNING("Models", "Model registry used before initialize()");
        return;
    }

    // This runs on a user action and destroys buffers and textures that
    // frames still in flight may be drawing with, so let the GPU finish first.
    vkDeviceWaitIdle(device_);

    customModelsDirectory_ = directory;
    availableModels_.clear();

    auto loadedModels = loader_.scanAndLoadModels(directory);
    loadErrors_ = loader_.getErrors();
    lastScannedDirectory_ = directory;

    // Track which shapes we've seen (for cleanup of removed files)
    std::unordered_set<std::string> seenNames;

    for (auto& model : loadedModels) {
        seenNames.insert(model.name);
        availableModels_[model.name] = std::filesystem::path(model.fullPath);

        auto it = shapes_.find(model.name);
        if (it != shapes_.end()) {
            if (model.lastModified > it->second.lastModified) {
                it->second.buffer.cleanup(device_);

                for (auto& mat : it->second.materials) {
                    if (mat.hasTexture && textureManager_) {
                        textureManager_->destroyTexture(mat.texture);
                    }
                }
                it->second.materials.clear();
                it->second.hasTexture = false;

                if (createShapeBuffer(model, it->second.buffer)) {
                    it->second.lastModified = model.lastModified;
                    it->second.fullPath = model.fullPath;
                    it->second.isValid = true;

                    for (const auto& segment : model.materialSegments) {
                        MaterialTextureInfo matInfo;
                        matInfo.startIndex = segment.startIndex;
                        matInfo.indexCount = segment.indexCount;
                        matInfo.texturePath = segment.texturePath;

                        if (!segment.texturePath.empty() && textureManager_) {
                            auto texOpt = textureManager_->loadTextureFromFile(segment.texturePath);
                            if (texOpt.has_value()) {
                                matInfo.texture = texOpt.value();
                                matInfo.hasTexture = true;
                            }
                        }
                        it->second.materials.push_back(matInfo);
                    }

                    // The single-texture fields mirror the first textured material
                    for (const auto& mat : it->second.materials) {
                        if (mat.hasTexture) {
                            it->second.texture = mat.texture;
                            it->second.diffuseTexturePath = mat.texturePath;
                            it->second.hasTexture = true;
                            break;
                        }
                    }
                } else {
                    it->second.isValid = false;
                }
            }
        } else {
            CustomShape shape;
            shape.name = model.name;
            shape.fullPath = model.fullPath;
            shape.lastModified = model.lastModified;

            if (createShapeBuffer(model, shape.buffer)) {
                shape.isValid = true;

                for (const auto& segment : model.materialSegments) {
                    MaterialTextureInfo matInfo;
                    matInfo.startIndex = segment.startIndex;
                    matInfo.indexCount = segment.indexCount;
                    matInfo.texturePath = segment.texturePath;

                    if (!segment.texturePath.empty() && textureManager_) {
                        auto texOpt = textureManager_->loadTextureFromFile(segment.texturePath);
                        if (texOpt.has_value()) {
                            matInfo.texture = texOpt.value();
                            matInfo.hasTexture = true;
                        }
                    }
                    shape.materials.push_back(matInfo);
                }

                // The single-texture fields mirror the first textured material
                for (const auto& mat : shape.materials) {
                    if (mat.hasTexture) {
                        shape.texture = mat.texture;
                        shape.diffuseTexturePath = mat.texturePath;
                        shape.hasTexture = true;
                        break;
                    }
                }
            } else {
                shape.isValid = false;
                loadErrors_.push_back({model.name, "Failed to create GPU buffers"});
            }

            shapes_[model.name] = std::move(shape);
        }
    }

    // Remove shapes for deleted files. Baked-pack shapes never came from
    // the directory, so they are not "deleted" by being absent from it.
    for (auto it = shapes_.begin(); it != shapes_.end();) {
        if (it->second.fullPath != "<baked>" && seenNames.find(it->first) == seenNames.end()) {
            it->second.buffer.cleanup(device_);
            for (auto& mat : it->second.materials) {
                if (mat.hasTexture && textureManager_) {
                    textureManager_->destroyTexture(mat.texture);
                }
            }
            it = shapes_.erase(it);
        } else {
            ++it;
        }
    }

    // Cached descriptor sets still point at the image views destroyed above
    if (onShapesReplaced_) {
        onShapesReplaced_();
    }
}

bool CustomShapeRegistry::loadSingleModel(const std::string& name, const std::filesystem::path& path) {
    if (device_ == VK_NULL_HANDLE) {
        AWOW_LOG_WARNING("Models", "Model registry used before initialize(), cannot load " + name);
        return false;
    }

    if (shapes_.find(name) != shapes_.end()) {
        return shapes_[name].isValid;
    }

    auto modelOpt = loader_.loadModel(path.string());
    if (!modelOpt.has_value()) {
        AWOW_LOG_WARNING("Models", "Failed to load model: " + name);
        loadErrors_.push_back({name, "Failed to load OBJ file"});
        return false;
    }

    LoadedModel& model = modelOpt.value();

    CustomShape shape;
    shape.name = model.name;
    shape.fullPath = model.fullPath;
    shape.lastModified = model.lastModified;

    if (createShapeBuffer(model, shape.buffer)) {
        shape.isValid = true;

        shape.materials.clear();
        shape.materials.reserve(model.materialSegments.size());

        for (const auto& segment : model.materialSegments) {
            MaterialTextureInfo matInfo;
            matInfo.startIndex = segment.startIndex;
            matInfo.indexCount = segment.indexCount;
            matInfo.texturePath = segment.texturePath;

            if (!segment.texturePath.empty() && textureManager_) {
                auto texOpt = textureManager_->loadTextureFromFile(segment.texturePath);
                if (texOpt.has_value()) {
                    matInfo.texture = texOpt.value();
                    matInfo.hasTexture = true;
                }
            }

            shape.materials.push_back(matInfo);
        }

        // The single-texture fields mirror the first textured material
        for (const auto& mat : shape.materials) {
            if (mat.hasTexture) {
                shape.texture = mat.texture;
                shape.diffuseTexturePath = mat.texturePath;
                shape.hasTexture = true;
                break;
            }
        }
    } else {
        shape.isValid = false;
        loadErrors_.push_back({model.name, "Failed to create GPU buffers"});
        return false;
    }

    shapes_[model.name] = std::move(shape);
    return true;
}

const CustomShape* CustomShapeRegistry::getShape(const std::string& name) {
    auto it = shapes_.find(name);
    if (it != shapes_.end() && it->second.isValid) {
        return &it->second;
    }

    auto diskIt = availableModels_.find(name);
    const bool onDisk = diskIt != availableModels_.end();

    // Baked models load synchronously: a memcpy to the GPU, no parsing.
    // A same-named OBJ on disk takes precedence so users can override.
    if (!onDisk) {
        if (const BakedModelView* baked = bakedPack_.find(name)) {
            return loadBakedShape(name, *baked);
        }
        return nullptr;  // Model doesn't exist
    }

    // Queue for async loading (non-blocking)
    queueModelLoad(name, diskIt->second);

    return nullptr;
}

bool CustomShapeRegistry::hasShape(const std::string& name) const {
    auto it = shapes_.find(name);
    if (it != shapes_.end() && it->second.isValid) {
        return true;
    }
    // Check if available (not yet loaded) or baked into a pack
    return availableModels_.find(name) != availableModels_.end() ||
           bakedPack_.find(name) != nullptr;
}

std::vector<std::string> CustomShapeRegistry::getShapeNames() const {
    std::vector<std::string> names;
    names.reserve(availableModels_.size() + bakedPack_.modelCount());

    // Return all available models (scanned from disk), not just loaded ones
    for (const auto& [name, _] : availableModels_) {
        names.push_back(name);
    }

    // Baked pack models (skip names shadowed by a disk OBJ)
    for (const auto& [name, _] : bakedPack_.models()) {
        if (availableModels_.find(name) == availableModels_.end())
            names.push_back(name);
    }

    std::sort(names.begin(), names.end());
    return names;
}

const ShapeBuffer* CustomShapeRegistry::getShapeBuffer(const std::string& name) const {
    auto it = shapes_.find(name);
    if (it != shapes_.end() && it->second.isValid) {
        return &it->second.buffer;
    }
    return nullptr;
}

bool CustomShapeRegistry::needsRefresh(const std::string& directory) const {
    namespace fs = std::filesystem;

    if (!fs::exists(directory)) {
        return shapes_.empty() ? false : true;  // Directory deleted
    }

    try {
        for (const auto& entry : fs::directory_iterator(directory)) {
            if (entry.is_regular_file()) {
                auto ext = entry.path().extension().string();
                std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                if (ext == ".obj") {
                    std::string name = entry.path().stem().string();
                    auto it = shapes_.find(name);

                    if (it == shapes_.end()) {
                        return true;  // New file
                    }

                    if (entry.last_write_time() > it->second.lastModified) {
                        return true;  // Modified file
                    }
                }
            }
        }

        for (const auto& [name, shape] : shapes_) {
            if (!fs::exists(shape.fullPath)) {
                return true;  // File deleted
            }
        }
    } catch (const std::exception&) {
        return true;  // Error reading directory, assume refresh needed
    }

    return false;
}

bool CustomShapeRegistry::createShapeBuffer(const LoadedModel& model, ShapeBuffer& outBuffer) {
    return ::createShapeBuffer(device_, physicalDevice_,
                               model.vertices.data(), model.vertices.size(),
                               model.indices.data(), model.indices.size(), outBuffer);
}

bool CustomShapeRegistry::loadBakedPack(const std::string& path) {
    if (!bakedPack_.load(path)) {
        AWOW_LOG_WARNING("Models", "Failed to load baked model pack: " + path);
        return false;
    }
    AWOW_LOG_INFO("Models", "Loaded " + std::to_string(bakedPack_.modelCount()) + " baked models, " +
                  std::to_string(bakedPack_.assignments().size()) + " npc assignments");
    return true;
}

const CustomShape* CustomShapeRegistry::loadBakedShape(const std::string& name, const BakedModelView& view) {
    if (device_ == VK_NULL_HANDLE)
        return nullptr;

    CustomShape shape;
    shape.name = name;
    shape.fullPath = "<baked>";

    // Straight memcpy from the pack blob into host-visible GPU memory - no
    // parsing, no worker thread, no intermediate copies.
    if (!::createShapeBuffer(device_, physicalDevice_, view.vertices, view.vertexCount,
                             view.indices, view.indexCount, shape.buffer)) {
        std::lock_guard<std::mutex> lock(pendingMutex_);
        loadStates_[name] = ModelLoadState::Failed;
        return nullptr;
    }
    shape.isValid = true;
    {
        std::lock_guard<std::mutex> lock(pendingMutex_);
        loadStates_[name] = ModelLoadState::Ready;
    }

    auto [it, inserted] = shapes_.insert_or_assign(name, std::move(shape));
    return &it->second;
}

void CustomShapeRegistry::setModelRotation(const std::string& name, const ModelRotation& rotation) {
    modelRotations_[name] = rotation;
    saveRotations();
}

ModelRotation CustomShapeRegistry::getModelRotation(const std::string& name) const {
    auto it = modelRotations_.find(name);
    if (it != modelRotations_.end()) {
        return it->second;
    }
    return ModelRotation{};  // Default: no rotation
}

void CustomShapeRegistry::saveRotations() {
    std::ostringstream ss;
    constexpr float defaultX = -90.0f;  // Must match ModelRotation default
    for (const auto& [name, rot] : modelRotations_) {
        if (rot.x != defaultX || rot.y != 0.0f || rot.z != 0.0f || rot.zOffset != 0.0f) {
            ss << name << "=" << rot.x << "," << rot.y << "," << rot.z << "," << rot.zOffset << "\n";
        }
    }

    auto& cache = SettingsCache::instance();
    cache.get().customModelRotationsJson = ss.str();
    cache.markDirty();
}

void CustomShapeRegistry::loadRotations() {
    const auto& settings = SettingsCache::instance().get();
    const std::string& data = settings.customModelRotationsJson;

    if (data.empty()) {
        return;
    }

    modelRotations_.clear();
    std::istringstream stream(data);
    std::string line;

    while (std::getline(stream, line)) {
        if (line.empty()) continue;

        size_t eqPos = line.find('=');
        if (eqPos == std::string::npos) continue;

        std::string name = line.substr(0, eqPos);
        std::string values = line.substr(eqPos + 1);

        ModelRotation rot;
        // Older saves have no zOffset, so three values are accepted
        if (sscanf(values.c_str(), "%f,%f,%f,%f", &rot.x, &rot.y, &rot.z, &rot.zOffset) >= 3) {
            modelRotations_[name] = rot;
        }
    }
}

void CustomShapeRegistry::workerThreadFunc() {
    // The main thread has its own loader for refreshFromDirectory; sharing
    // one would race on its error list.
    CustomModelLoader loader;

    while (!shutdownRequested_) {
        std::string modelName;
        std::filesystem::path modelPath;

        {
            std::unique_lock<std::mutex> lock(queueMutex_);
            queueCV_.wait(lock, [this] {
                return shutdownRequested_ || !loadQueue_.empty();
            });

            if (shutdownRequested_) break;
            if (loadQueue_.empty()) continue;

            modelName = loadQueue_.front().name;
            modelPath = loadQueue_.front().path;
            loadQueue_.pop();
        }

        {
            std::lock_guard<std::mutex> lock(pendingMutex_);
            loadStates_[modelName] = ModelLoadState::Parsing;
        }

        // Parse without holding a lock. The path travels with
        // the work item because availableModels_ is rewritten on the main
        // thread by refreshFromDirectory.
        auto modelOpt = loader.loadModel(modelPath.string());
        if (!modelOpt) {
            std::lock_guard<std::mutex> lock(pendingMutex_);
            loadStates_[modelName] = ModelLoadState::Failed;
            continue;
        }

        PendingModel pending;
        pending.name = modelName;
        pending.model = std::move(modelOpt.value());

        // Decode textures here as well, off the main thread
        for (const auto& segment : pending.model.materialSegments) {
            if (!segment.texturePath.empty()) {
                int w, h, channels;
                unsigned char* pixels = stbi_load(segment.texturePath.c_str(), &w, &h, &channels, 4);
                if (pixels) {
                    DecodedTexture decoded;
                    decoded.path = segment.texturePath;
                    decoded.width = w;
                    decoded.height = h;
                    decoded.pixels.assign(pixels, pixels + (w * h * 4));
                    pending.decodedTextures.push_back(std::move(decoded));
                    stbi_image_free(pixels);
                }
            }
        }

        {
            std::lock_guard<std::mutex> lock(pendingMutex_);
            loadStates_[modelName] = ModelLoadState::PendingGPU;
            pendingModels_.push(std::move(pending));
        }
    }

    workerRunning_ = false;
}

void CustomShapeRegistry::queueModelLoad(const std::string& name, const std::filesystem::path& path) {
    {
        std::lock_guard<std::mutex> lock(pendingMutex_);

        auto shapeIt = shapes_.find(name);
        if (shapeIt != shapes_.end() && shapeIt->second.isValid) {
            return;
        }

        auto stateIt = loadStates_.find(name);
        if (stateIt != loadStates_.end()) {
            ModelLoadState state = stateIt->second;
            if (state != ModelLoadState::NotLoaded && state != ModelLoadState::Failed) {
                return;  // Already in progress
            }
        }

        loadStates_[name] = ModelLoadState::Queued;
    }

    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        loadQueue_.push({name, path});
    }
    queueCV_.notify_one();
}

void CustomShapeRegistry::processLoadQueue() {
    // Limit uploads per frame so loading never stalls a frame
    constexpr int MAX_UPLOADS_PER_FRAME = 2;
    int processed = 0;

    while (processed < MAX_UPLOADS_PER_FRAME) {
        PendingModel pending;

        {
            std::lock_guard<std::mutex> lock(pendingMutex_);
            if (pendingModels_.empty()) break;
            pending = std::move(pendingModels_.front());
            pendingModels_.pop();
        }

        CustomShape shape;
        shape.name = pending.name;
        shape.fullPath = pending.model.fullPath;
        shape.lastModified = pending.model.lastModified;

        if (!createShapeBuffer(pending.model, shape.buffer)) {
            std::lock_guard<std::mutex> lock(pendingMutex_);
            loadStates_[pending.name] = ModelLoadState::Failed;
            loadErrors_.push_back({pending.name, "Failed to create GPU buffers"});
            continue;
        }

        size_t texIdx = 0;
        for (const auto& segment : pending.model.materialSegments) {
            MaterialTextureInfo matInfo;
            matInfo.startIndex = segment.startIndex;
            matInfo.indexCount = segment.indexCount;
            matInfo.texturePath = segment.texturePath;

            if (!segment.texturePath.empty() && texIdx < pending.decodedTextures.size()) {
                const auto& decoded = pending.decodedTextures[texIdx];
                if (decoded.path == segment.texturePath && textureManager_) {
                    auto texOpt = textureManager_->createTexture(
                        decoded.pixels.data(), decoded.width, decoded.height);
                    if (texOpt) {
                        matInfo.texture = texOpt.value();
                        matInfo.hasTexture = true;
                    }
                }
                texIdx++;
            }
            shape.materials.push_back(matInfo);
        }

        // The single-texture fields mirror the first textured material
        for (const auto& mat : shape.materials) {
            if (mat.hasTexture) {
                shape.texture = mat.texture;
                shape.diffuseTexturePath = mat.texturePath;
                shape.hasTexture = true;
                break;
            }
        }

        shape.isValid = true;
        shapes_[pending.name] = std::move(shape);

        {
            std::lock_guard<std::mutex> lock(pendingMutex_);
            loadStates_[pending.name] = ModelLoadState::Ready;
        }

        processed++;
    }
}

ModelLoadState CustomShapeRegistry::getLoadState(const std::string& name) const {
    std::lock_guard<std::mutex> lock(pendingMutex_);
    auto it = loadStates_.find(name);
    if (it != loadStates_.end()) {
        return it->second;
    }

    auto shapeIt = shapes_.find(name);
    if (shapeIt != shapes_.end() && shapeIt->second.isValid) {
        return ModelLoadState::Ready;
    }

    return ModelLoadState::NotLoaded;
}
