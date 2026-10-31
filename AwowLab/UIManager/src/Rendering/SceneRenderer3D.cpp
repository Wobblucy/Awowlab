#include "Rendering/SceneRenderer3D.h"
#include "Color/ActorColorGenerator.h"
#include "Vulkan/Pipeline/VulkanPipeline3D.h"
#include "Vulkan/Buffers/VulkanBufferManager3D.h"
#include "Geometry/CustomShapeRegistry.h"
#include "UI/WorldMapTextureManager.h"
#include "UI/SpecIconLoader.h"
#include "Camera/Camera3D.h"
#include "Animation/ActorPositionInterpolator.h"
#include "UI/ActorVisibilitySettings.h"
#include "UI/ActorAppearanceSettings.h"
#include "Core/ErrorLogger.h"
#include <cstring>

SceneRenderer3D::SceneRenderer3D()
    : pipeline_(std::make_unique<VulkanPipeline3D>())
    , bufferManager_(std::make_unique<VulkanBufferManager3D>())
    , worldMapTextureManager_(std::make_unique<WorldMapTextureManager>())
    , specIconLoader_(std::make_unique<SpecIconLoader>())
    , descriptorPool_(VK_NULL_HANDLE)
    , descriptorSet_(VK_NULL_HANDLE)
    , floorDescriptorPool_(VK_NULL_HANDLE)
    , floorDescriptorSet_(VK_NULL_HANDLE)
    , classIconDescriptorPool_(VK_NULL_HANDLE)
    , texturedModelDescriptorPool_(VK_NULL_HANDLE)
    , graphicsQueue_(VK_NULL_HANDLE)
    , commandPool_(VK_NULL_HANDLE)
    , floorVertexBuffer_(VK_NULL_HANDLE)
    , floorVertexMemory_(VK_NULL_HANDLE)
    , floorIndexBuffer_(VK_NULL_HANDLE)
    , floorIndexMemory_(VK_NULL_HANDLE)
    , floorIndexCount_(0)
    , hasFloorBuffers_(false)
    , ringVertexBuffer_(VK_NULL_HANDLE)
    , ringVertexMemory_(VK_NULL_HANDLE)
    , ringIndexBuffer_(VK_NULL_HANDLE)
    , ringIndexMemory_(VK_NULL_HANDLE)
    , ringIndexCount_(0)
    , hasRingBuffers_(false)
    , device_(VK_NULL_HANDLE)
    , physicalDevice_(VK_NULL_HANDLE)
    , isInitialized_(false)
{
}

SceneRenderer3D::~SceneRenderer3D() {
}

bool SceneRenderer3D::initialize(VkDevice device, VkPhysicalDevice physicalDevice, VkRenderPass renderPass,
                                  VkQueue graphicsQueue, VkCommandPool commandPool) {
    device_ = device;
    physicalDevice_ = physicalDevice;
    graphicsQueue_ = graphicsQueue;
    commandPool_ = commandPool;

    if (!pipeline_->initialize(device, renderPass)) {
        AWOW_LOG_ERROR("Renderer3D", "Failed to initialize 3D pipeline");
        return false;
    }

    // Sphere: radius 1.0, 32 segments, 16 rings
    if (!bufferManager_->initialize(device, physicalDevice, 1.0f, 32, 16)) {
        AWOW_LOG_ERROR("Renderer3D", "Failed to initialize 3D buffer manager");
        return false;
    }

    if (!createDescriptorPool(device)) {
        AWOW_LOG_ERROR("Renderer3D", "Failed to create descriptor pool");
        return false;
    }

    if (!createDescriptorSet(device)) {
        AWOW_LOG_ERROR("Renderer3D", "Failed to create descriptor set");
        return false;
    }

    if (!createFloorDescriptorPool(device)) {
        AWOW_LOG_ERROR("Renderer3D", "Failed to create floor descriptor pool");
        return false;
    }

    if (!worldMapTextureManager_->initialize(device, physicalDevice, graphicsQueue, commandPool, floorDescriptorPool_)) {
        AWOW_LOG_WARNING("Renderer3D", "Failed to initialize world map texture manager (non-fatal)");
    }

    if (!createFloorDescriptorSet(device)) {
        AWOW_LOG_ERROR("Renderer3D", "Failed to create floor descriptor set");
        return false;
    }

    if (!createClassIconDescriptorPool(device)) {
        AWOW_LOG_WARNING("Renderer3D", "Failed to create class icon descriptor pool (non-fatal)");
    }

    if (specIconLoader_ && classIconDescriptorPool_ != VK_NULL_HANDLE) {
        if (!specIconLoader_->initialize(device, physicalDevice, graphicsQueue, commandPool, classIconDescriptorPool_)) {
            AWOW_LOG_WARNING("Renderer3D", "Failed to initialize spec icon loader (non-fatal)");
        }
    }

    if (!createRingBuffers(device, physicalDevice)) {
        AWOW_LOG_WARNING("Renderer3D", "Failed to create selection ring buffers (non-fatal)");
    }

    if (!createTexturedModelDescriptorPool(device)) {
        AWOW_LOG_WARNING("Renderer3D", "Failed to create textured model descriptor pool (non-fatal)");
    }

    isInitialized_ = true;
    return true;
}

void SceneRenderer3D::cleanup(VkDevice device) {
    cleanupRingBuffers(device);

    cleanupFloorBuffers(device);

    // Before the descriptor pool its sets were allocated from
    if (specIconLoader_) {
        specIconLoader_->cleanup();
    }

    if (worldMapTextureManager_) {
        worldMapTextureManager_->cleanup();
    }

    // Allocated from classIconDescriptorPool_, destroyed with it below
    specDescriptorSets_.clear();

    texturedModelDescriptorSets_.clear();

    if (texturedModelDescriptorPool_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device, texturedModelDescriptorPool_, nullptr);
        texturedModelDescriptorPool_ = VK_NULL_HANDLE;
    }

    if (classIconDescriptorPool_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device, classIconDescriptorPool_, nullptr);
        classIconDescriptorPool_ = VK_NULL_HANDLE;
    }

    if (floorDescriptorPool_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device, floorDescriptorPool_, nullptr);
        floorDescriptorPool_ = VK_NULL_HANDLE;
    }

    if (descriptorPool_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device, descriptorPool_, nullptr);
        descriptorPool_ = VK_NULL_HANDLE;
    }

    if (bufferManager_) {
        bufferManager_->cleanup(device);
    }

    if (pipeline_) {
        pipeline_->cleanup(device);
    }

    isInitialized_ = false;
}

void SceneRenderer3D::processTextureCleanup() {
    if (worldMapTextureManager_) {
        worldMapTextureManager_->processTextureCleanup();
    }
    if (specIconLoader_) {
        specIconLoader_->processTextureCleanup();
    }
}

void SceneRenderer3D::setAppearanceSettings(std::shared_ptr<ActorAppearanceSettings> settings) {
    appearanceSettings_ = settings;
}

void SceneRenderer3D::setCustomShapeRegistry(std::shared_ptr<CustomShapeRegistry> registry) {
    if (bufferManager_) {
        bufferManager_->setCustomShapeRegistry(std::move(registry));
    }
}

CustomShapeRegistry* SceneRenderer3D::getCustomShapeRegistry() const {
    if (bufferManager_) {
        return bufferManager_->getCustomShapeRegistry();
    }
    return nullptr;
}

VkBuffer SceneRenderer3D::getCameraUBO() const {
    if (bufferManager_) {
        return bufferManager_->getCameraUBO();
    }
    return VK_NULL_HANDLE;
}

void SceneRenderer3D::clearTexturedModelDescriptorCache() {
    texturedModelDescriptorSets_.clear();

    // The pool has no FREE flag, so dropping the map alone never gives the
    // sets back and the pool runs dry after enough encounter switches.
    // Callers wait for the device to go idle first, so no set is in use.
    if (texturedModelDescriptorPool_ != VK_NULL_HANDLE && device_ != VK_NULL_HANDLE) {
        vkResetDescriptorPool(device_, texturedModelDescriptorPool_, 0);
    }
}

bool SceneRenderer3D::createDescriptorPool(VkDevice device) {
    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSize.descriptorCount = 1;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    poolInfo.maxSets = 1;

    if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool_) != VK_SUCCESS) {
        return false;
    }

    return true;
}

bool SceneRenderer3D::createDescriptorSet(VkDevice device) {
    VkDescriptorSetLayout layout = pipeline_->getDescriptorSetLayout();

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool_;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &layout;

    if (vkAllocateDescriptorSets(device, &allocInfo, &descriptorSet_) != VK_SUCCESS) {
        return false;
    }

    VkDescriptorBufferInfo bufferInfo{};
    bufferInfo.buffer = bufferManager_->getCameraUBO();
    bufferInfo.offset = 0;
    bufferInfo.range = sizeof(CameraUBO);

    VkWriteDescriptorSet descriptorWrite{};
    descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrite.dstSet = descriptorSet_;
    descriptorWrite.dstBinding = 0;
    descriptorWrite.dstArrayElement = 0;
    descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    descriptorWrite.descriptorCount = 1;
    descriptorWrite.pBufferInfo = &bufferInfo;

    vkUpdateDescriptorSets(device, 1, &descriptorWrite, 0, nullptr);

    return true;
}

void SceneRenderer3D::updateCameraMatrices(VkDevice device, const std::shared_ptr<Camera3D>& camera, float aspectRatio) {
    float viewMatrix[16];
    float projMatrix[16];

    camera->getViewMatrix(viewMatrix);
    camera->getProjectionMatrix(projMatrix, aspectRatio);

    bufferManager_->updateCameraUBO(device, viewMatrix, projMatrix);
}

void SceneRenderer3D::render(VkDevice device,
                              VkCommandBuffer commandBuffer,
                              VkExtent2D extent,
                              const std::unordered_map<std::string_view, InterpolatedPosition>& actorPositions,
                              const std::unordered_map<std::string_view, InterpolatedResource>& actorResources,
                              const std::unordered_map<std::string_view, std::string_view>& guidToName,
                              const std::shared_ptr<ActorColorGenerator>& colorGen,
                              const std::shared_ptr<Camera3D>& camera,
                              const ActorVisibilitySettings& visibilitySettings,
                              bool showNameplates,
                              std::string_view selectedPlayerGuid,
                              float animationTime) {
    if (!isInitialized_) {
        return;
    }

    float aspectRatio = static_cast<float>(extent.width) / static_cast<float>(extent.height);
    updateCameraMatrices(device, camera, aspectRatio);

    nameplates_.clear();
    if (showNameplates) {
        projectNameplates(actorPositions, actorResources, guidToName, colorGen, camera, aspectRatio, extent, visibilitySettings);
    }

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_->getPipeline());

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(extent.width);
    viewport.height = static_cast<float>(extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = extent;
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                           pipeline_->getPipelineLayout(), 0, 1, &descriptorSet_, 0, nullptr);

    renderFloor(commandBuffer, camera);

    // Each pass skips the actors the other two draw
    renderActors(commandBuffer, actorPositions, colorGen, camera, visibilitySettings, selectedPlayerGuid);

    renderTexturedModelActors(commandBuffer, actorPositions, colorGen, camera, visibilitySettings, selectedPlayerGuid);

    renderClassIconActors(commandBuffer, actorPositions, colorGen, camera, visibilitySettings, selectedPlayerGuid);

    if (!selectedPlayerGuid.empty() && hasRingBuffers_) {
        auto it = actorPositions.find(selectedPlayerGuid);
        if (it != actorPositions.end() && it->second.hasPosition) {
            renderSelectionRing(commandBuffer, it->second.x, it->second.y, animationTime);
        }
    }
}

void SceneRenderer3D::projectNameplates(const std::unordered_map<std::string_view, InterpolatedPosition>& actorPositions,
                                         const std::unordered_map<std::string_view, InterpolatedResource>& actorResources,
                                         const std::unordered_map<std::string_view, std::string_view>& guidToName,
                                         const std::shared_ptr<ActorColorGenerator>& colorGen,
                                         const std::shared_ptr<Camera3D>& camera,
                                         float aspectRatio,
                                         VkExtent2D extent,
                                         const ActorVisibilitySettings& visibilitySettings) {
    float viewMatrix[16];
    float projMatrix[16];
    camera->getViewMatrix(viewMatrix);
    camera->getProjectionMatrix(projMatrix, aspectRatio);

    for (const auto& [guid, interpPos] : actorPositions) {
        if (!interpPos.hasPosition) {
            continue;
        }

        ActorType actorType = ActorType::Unknown;
        const ActorType* typePtr = colorGen->getActorType(guid);
        if (typePtr) {
            actorType = *typePtr;
        }

        UnitReaction reaction = UnitReaction::Neutral;
        const UnitFlags* flagsPtr = colorGen->getActorFlags(guid);
        if (flagsPtr) {
            reaction = flagsPtr->reaction;
        }

        if (!visibilitySettings.shouldRender(actorType, reaction)) {
            continue;
        }

        float worldX = interpPos.x;
        float worldY = interpPos.y;
        float worldZ = 0.0f;
        float viewX = viewMatrix[0] * worldX + viewMatrix[4] * worldY + viewMatrix[8] * worldZ + viewMatrix[12];
        float viewY = viewMatrix[1] * worldX + viewMatrix[5] * worldY + viewMatrix[9] * worldZ + viewMatrix[13];
        float viewZ = viewMatrix[2] * worldX + viewMatrix[6] * worldY + viewMatrix[10] * worldZ + viewMatrix[14];
        float viewW = viewMatrix[3] * worldX + viewMatrix[7] * worldY + viewMatrix[11] * worldZ + viewMatrix[15];

        float clipX = projMatrix[0] * viewX + projMatrix[4] * viewY + projMatrix[8] * viewZ + projMatrix[12] * viewW;
        float clipY = projMatrix[1] * viewX + projMatrix[5] * viewY + projMatrix[9] * viewZ + projMatrix[13] * viewW;
        float clipW = projMatrix[3] * viewX + projMatrix[7] * viewY + projMatrix[11] * viewZ + projMatrix[15] * viewW;

        if (clipW <= 0.0f) {
            continue;
        }

        float ndcX = clipX / clipW;
        float ndcY = clipY / clipW;

        float screenX = (ndcX + 1.0f) * 0.5f * extent.width;
        float screenY = (ndcY + 1.0f) * 0.5f * extent.height;

        auto nameIt = guidToName.find(guid);
        std::string_view nameplate = (nameIt != guidToName.end())
            ? nameIt->second
            : guid;

        float healthPercent = 0.0f;
        bool hasHealth = false;
        auto resIt = actorResources.find(guid);
        if (resIt != actorResources.end() && resIt->second.hasResource) {
            healthPercent = resIt->second.healthPercent;
            hasHealth = true;
        }

        nameplates_.push_back({guid, nameplate, screenX, screenY, healthPercent, hasHealth, true});
    }
}

bool SceneRenderer3D::createTexturedModelDescriptorPool(VkDevice device) {
    // One set (camera UBO + texture) per material. Some models have 11
    // materials; 300 sets covers about 50 models at ~6 each.
    std::array<VkDescriptorPoolSize, 2> poolSizes{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[0].descriptorCount = 300;
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[1].descriptorCount = 300;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = 300;

    if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &texturedModelDescriptorPool_) != VK_SUCCESS) {
        return false;
    }

    return true;
}

// Set layout shared by the class icon and textured model pipelines:
// camera UBO at binding 0, the texture at binding 1
VkDescriptorSet SceneRenderer3D::allocateTexturedDescriptorSet(VkDescriptorPool pool,
                                                               VkDescriptorSetLayout layout,
                                                               VkImageView imageView,
                                                               VkSampler sampler) {
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = pool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &layout;

    VkDescriptorSet descriptorSet;
    if (vkAllocateDescriptorSets(device_, &allocInfo, &descriptorSet) != VK_SUCCESS) {
        return VK_NULL_HANDLE;
    }

    VkDescriptorBufferInfo bufferInfo{};
    bufferInfo.buffer = bufferManager_->getCameraUBO();
    bufferInfo.offset = 0;
    bufferInfo.range = sizeof(CameraUBO);

    VkDescriptorImageInfo imageInfo{};
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageInfo.imageView = imageView;
    imageInfo.sampler = sampler;

    std::array<VkWriteDescriptorSet, 2> descriptorWrites{};

    descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[0].dstSet = descriptorSet;
    descriptorWrites[0].dstBinding = 0;
    descriptorWrites[0].dstArrayElement = 0;
    descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    descriptorWrites[0].descriptorCount = 1;
    descriptorWrites[0].pBufferInfo = &bufferInfo;

    descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[1].dstSet = descriptorSet;
    descriptorWrites[1].dstBinding = 1;
    descriptorWrites[1].dstArrayElement = 0;
    descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptorWrites[1].descriptorCount = 1;
    descriptorWrites[1].pImageInfo = &imageInfo;

    vkUpdateDescriptorSets(device_, static_cast<uint32_t>(descriptorWrites.size()),
                           descriptorWrites.data(), 0, nullptr);

    return descriptorSet;
}

VkDescriptorSet SceneRenderer3D::getOrCreateTexturedModelDescriptorSet(const CustomShape& shape) {
    auto it = texturedModelDescriptorSets_.find(shape.name);
    if (it != texturedModelDescriptorSets_.end()) {
        return it->second;
    }

    if (texturedModelDescriptorPool_ == VK_NULL_HANDLE || !shape.hasTexture) {
        return VK_NULL_HANDLE;
    }

    // The texture may still be loading on another thread
    if (shape.texture.imageView == VK_NULL_HANDLE || shape.texture.sampler == VK_NULL_HANDLE) {
        return VK_NULL_HANDLE;
    }

    VkDescriptorSet descriptorSet = allocateTexturedDescriptorSet(
        texturedModelDescriptorPool_, pipeline_->getTexturedModelDescriptorSetLayout(),
        shape.texture.imageView, shape.texture.sampler);
    if (descriptorSet != VK_NULL_HANDLE) {
        texturedModelDescriptorSets_[shape.name] = descriptorSet;
    }
    return descriptorSet;
}

VkDescriptorSet SceneRenderer3D::getOrCreateMaterialDescriptorSet(const CustomShape& shape, size_t materialIndex) {
    // Key format: "modelName_materialIndex"
    if (materialIndex >= shape.materials.size()) {
        return VK_NULL_HANDLE;
    }

    const auto& material = shape.materials[materialIndex];
    if (!material.hasTexture) {
        return VK_NULL_HANDLE;
    }

    // The texture may still be loading on another thread
    if (material.texture.imageView == VK_NULL_HANDLE || material.texture.sampler == VK_NULL_HANDLE) {
        return VK_NULL_HANDLE;
    }

    std::string key = shape.name + "_" + std::to_string(materialIndex);
    auto it = texturedModelDescriptorSets_.find(key);
    if (it != texturedModelDescriptorSets_.end()) {
        return it->second;
    }

    if (texturedModelDescriptorPool_ == VK_NULL_HANDLE) {
        return VK_NULL_HANDLE;
    }

    VkDescriptorSet descriptorSet = allocateTexturedDescriptorSet(
        texturedModelDescriptorPool_, pipeline_->getTexturedModelDescriptorSetLayout(),
        material.texture.imageView, material.texture.sampler);
    if (descriptorSet != VK_NULL_HANDLE) {
        texturedModelDescriptorSets_[key] = descriptorSet;
    }
    return descriptorSet;
}
