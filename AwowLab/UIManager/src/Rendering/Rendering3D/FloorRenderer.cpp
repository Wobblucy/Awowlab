#include "Rendering/SceneRenderer3D.h"
#include "Rendering/FloorMode.h"
#include "Vulkan/Pipeline/VulkanPipeline3D.h"
#include "Vulkan/Buffers/VulkanBufferManager3D.h"
#include "UI/WorldMapTextureManager.h"
#include "Camera/Camera3D.h"
#include "Geometry/PlaneGeometry.h"
#include "Core/ErrorLogger.h"
#include <cstring>
#include <array>

void SceneRenderer3D::setMapBounds(uint32_t mapId, const std::string& /*mapName*/,
                                    float x1, float y1, float x2, float y2) {
    if (currentMapBounds_.isValid && currentMapBounds_.mapId == mapId) {
        return;
    }

    if (!worldMapTextureManager_ || !worldMapTextureManager_->isInitialized()) {
        AWOW_LOG_WARNING("Renderer3D", "Cannot set map bounds: world map texture manager not initialized");
        // Grid floor around the passed bounds (the origin when there are none)
        float centerX = (x1 + x2) * 0.5f;
        float centerY = (y1 + y2) * 0.5f;
        setFallbackFloor(centerX, centerY);
        return;
    }

    MapWorldBounds pngBounds = worldMapTextureManager_->setCurrentMap(mapId);
    if (!pngBounds.valid) {
        AWOW_LOG_INFO("Renderer3D", "No world bounds for map " + std::to_string(mapId) + ", using fallback grid");
        float centerX = (x1 + x2) * 0.5f;
        float centerY = (y1 + y2) * 0.5f;
        setFallbackFloor(centerX, centerY);
        return;
    }

    currentMapBounds_.mapId = mapId;
    currentMapBounds_.mapName = "";
    currentMapBounds_.x1 = pngBounds.minX;
    currentMapBounds_.y1 = pngBounds.minY;
    currentMapBounds_.x2 = pngBounds.maxX;
    currentMapBounds_.y2 = pngBounds.maxY;
    currentMapBounds_.isValid = true;
    usingFallbackGrid_ = false;
    usingExternalTexture_ = false;

    if (device_ != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(device_);
        cleanupFloorBuffers(device_);

        auto [texWidth, texHeight] = worldMapTextureManager_->getCurrentTextureDimensions();

        if (!createFloorBuffers(device_, physicalDevice_,
                                pngBounds.minX, pngBounds.minY,
                                pngBounds.maxX, pngBounds.maxY,
                                texWidth, texHeight, false)) {
            AWOW_LOG_WARNING("Renderer3D", "Failed to create floor buffers for map " + std::to_string(mapId));
            hasFloorBuffers_ = false;
        }

        updateFloorDescriptorSet(device_);
    }
}

void SceneRenderer3D::setMapBoundsWithTexture(float x1, float y1, float x2, float y2,
                                                VkImageView imageView, VkSampler sampler,
                                                uint32_t textureWidth, uint32_t textureHeight) {
    (void)textureWidth; (void)textureHeight;

    externalImageView_ = imageView;
    externalSampler_ = sampler;
    usingExternalTexture_ = true;
    usingFallbackGrid_ = false;

    currentMapBounds_.x1 = x1;
    currentMapBounds_.y1 = y1;
    currentMapBounds_.x2 = x2;
    currentMapBounds_.y2 = y2;
    currentMapBounds_.mapId = 0;  // No mapId for encounter textures
    currentMapBounds_.isValid = true;

    if (device_ != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(device_);
        cleanupFloorBuffers(device_);

        if (!createFloorBuffers(device_, physicalDevice_, x1, y1, x2, y2, textureWidth, textureHeight, true)) {
            AWOW_LOG_WARNING("Renderer3D", "Failed to create floor buffers for encounter texture");
            hasFloorBuffers_ = false;
            return;
        }

        writeFloorTexture(device_, imageView, sampler);
    }
}

void SceneRenderer3D::clearMapBounds() {
    if (device_ != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(device_);
        cleanupFloorBuffers(device_);

        // The external texture is about to be destroyed by EncounterTextureManager;
        // point the set at the default texture so it never references it
        if (usingExternalTexture_) {
            updateFloorDescriptorSet(device_);
        }
    }
    currentMapBounds_ = MapBounds{};
    usingExternalTexture_ = false;
    usingFallbackGrid_ = false;
    externalImageView_ = VK_NULL_HANDLE;
    externalSampler_ = VK_NULL_HANDLE;
}

void SceneRenderer3D::setFallbackFloor(float centerX, float centerY) {
    float halfSize = 150.0f;
    float x1 = centerX - halfSize;
    float y1 = centerY - halfSize;
    float x2 = centerX + halfSize;
    float y2 = centerY + halfSize;

    currentMapBounds_.x1 = x1;
    currentMapBounds_.y1 = y1;
    currentMapBounds_.x2 = x2;
    currentMapBounds_.y2 = y2;
    currentMapBounds_.mapId = 0;
    currentMapBounds_.isValid = true;
    usingFallbackGrid_ = true;
    usingExternalTexture_ = false;

    if (device_ != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(device_);
        cleanupFloorBuffers(device_);

        if (!createFloorBuffers(device_, physicalDevice_, x1, y1, x2, y2, 1, 1, false)) {
            AWOW_LOG_WARNING("Renderer3D", "Failed to create fallback floor buffers");
            hasFloorBuffers_ = false;
            return;
        }

        updateFloorDescriptorSet(device_);
    }
}

void SceneRenderer3D::preloadMapTexture(uint32_t mapId) {
    if (!worldMapTextureManager_ || !worldMapTextureManager_->isInitialized()) {
        return;
    }

    worldMapTextureManager_->preloadMapTexture(mapId);
}

bool SceneRenderer3D::getWorldMapInfo(float& minX, float& maxX, float& minY, float& maxY, void*& textureId) const {
    if (!currentMapBounds_.isValid || !worldMapTextureManager_ || !worldMapTextureManager_->isInitialized()) {
        return false;
    }

    const MapWorldBounds& bounds = worldMapTextureManager_->getCurrentBounds();
    if (!bounds.valid) {
        return false;
    }

    minX = bounds.minX;
    maxX = bounds.maxX;
    minY = bounds.minY;
    maxY = bounds.maxY;
    textureId = worldMapTextureManager_->getCurrentImGuiTextureID();

    return textureId != nullptr;
}

bool SceneRenderer3D::createFloorBuffers(VkDevice device, VkPhysicalDevice physicalDevice,
                                          float x1, float y1, float x2, float y2,
                                          uint32_t textureWidth, uint32_t textureHeight,
                                          bool isEncounterTexture) {
    std::vector<Vertex3D> vertices;
    std::vector<uint32_t> indices;

    if (isEncounterTexture) {
        // Encounter textures are exported from Blender already aligned
        PlaneGeometry::generateEncounterPlane(x1, y1, x2, y2, vertices, indices);
    } else {
        // World map textures need a 90 degree clockwise UV rotation
        PlaneGeometry::generateWorldBoundsPlane(x1, y1, x2, y2, textureWidth, textureHeight, vertices, indices);
    }

    if (!vulkan_buffers::createFilledBuffer(device, physicalDevice, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                            vertices.data(), sizeof(Vertex3D) * vertices.size(),
                                            floorVertexBuffer_, floorVertexMemory_)) {
        return false;
    }

    if (!vulkan_buffers::createFilledBuffer(device, physicalDevice, VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                                            indices.data(), sizeof(uint32_t) * indices.size(),
                                            floorIndexBuffer_, floorIndexMemory_)) {
        vulkan_buffers::destroyBuffer(device, floorVertexBuffer_, floorVertexMemory_);
        return false;
    }

    floorIndexCount_ = static_cast<uint32_t>(indices.size());
    hasFloorBuffers_ = true;

    return true;
}

void SceneRenderer3D::cleanupFloorBuffers(VkDevice device) {
    vulkan_buffers::destroyBuffer(device, floorIndexBuffer_, floorIndexMemory_);
    vulkan_buffers::destroyBuffer(device, floorVertexBuffer_, floorVertexMemory_);
    floorIndexCount_ = 0;
    hasFloorBuffers_ = false;
}

bool SceneRenderer3D::createFloorDescriptorPool(VkDevice device) {
    std::array<VkDescriptorPoolSize, 2> poolSizes{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[0].descriptorCount = 1;
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[1].descriptorCount = 1;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = 1;

    if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &floorDescriptorPool_) != VK_SUCCESS) {
        return false;
    }

    return true;
}

bool SceneRenderer3D::createFloorDescriptorSet(VkDevice device) {
    VkDescriptorSetLayout layout = pipeline_->getFloorDescriptorSetLayout();

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = floorDescriptorPool_;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &layout;

    if (vkAllocateDescriptorSets(device, &allocInfo, &floorDescriptorSet_) != VK_SUCCESS) {
        return false;
    }

    VkDescriptorBufferInfo bufferInfo{};
    bufferInfo.buffer = bufferManager_->getCameraUBO();
    bufferInfo.offset = 0;
    bufferInfo.range = sizeof(CameraUBO);

    VkWriteDescriptorSet uboWrite{};
    uboWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    uboWrite.dstSet = floorDescriptorSet_;
    uboWrite.dstBinding = 0;
    uboWrite.dstArrayElement = 0;
    uboWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboWrite.descriptorCount = 1;
    uboWrite.pBufferInfo = &bufferInfo;

    vkUpdateDescriptorSets(device, 1, &uboWrite, 0, nullptr);

    // Texture binding will be updated when map is set
    return true;
}

void SceneRenderer3D::updateFloorDescriptorSet(VkDevice device) {
    if (!worldMapTextureManager_ || !worldMapTextureManager_->isInitialized()) {
        return;
    }

    VkImageView imageView = worldMapTextureManager_->getCurrentImageView();
    VkSampler sampler = worldMapTextureManager_->getCurrentSampler();

    if (imageView == VK_NULL_HANDLE || sampler == VK_NULL_HANDLE) {
        const TextureHandle* defaultTex = worldMapTextureManager_->getDefaultTexture();
        if (defaultTex) {
            imageView = defaultTex->imageView;
            sampler = defaultTex->sampler;
        }
    }

    if (imageView == VK_NULL_HANDLE || sampler == VK_NULL_HANDLE) {
        return;
    }

    writeFloorTexture(device, imageView, sampler);
}

void SceneRenderer3D::writeFloorTexture(VkDevice device, VkImageView imageView, VkSampler sampler) {
    VkDescriptorImageInfo imageInfo{};
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageInfo.imageView = imageView;
    imageInfo.sampler = sampler;

    VkWriteDescriptorSet textureWrite{};
    textureWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    textureWrite.dstSet = floorDescriptorSet_;
    textureWrite.dstBinding = 1;
    textureWrite.dstArrayElement = 0;
    textureWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    textureWrite.descriptorCount = 1;
    textureWrite.pImageInfo = &imageInfo;

    vkUpdateDescriptorSets(device, 1, &textureWrite, 0, nullptr);
}

void SceneRenderer3D::renderFloor(VkCommandBuffer commandBuffer, [[maybe_unused]] const std::shared_ptr<Camera3D>& camera) {
    if (!currentMapBounds_.isValid || !hasFloorBuffers_) {
        return;
    }

    // The floor shader reads the alpha of the push constant color as its mode
    constexpr float FLOOR_MODE_TEXTURE = 1.0f;
    constexpr float FLOOR_MODE_GRID = 0.5f;

    struct PushConstants {
        float model[16];
        float color[4];
    } pushConstants;

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_->getFloorPipeline());

    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                           pipeline_->getFloorPipelineLayout(), 0, 1, &floorDescriptorSet_, 0, nullptr);

    VkDeviceSize offsets[] = {0};
    VkBuffer vertexBuffers[] = {floorVertexBuffer_};
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
    vkCmdBindIndexBuffer(commandBuffer, floorIndexBuffer_, 0, VK_INDEX_TYPE_UINT32);

    // Identity model matrix (floor is already positioned at world coordinates)
    const float modelMatrix[16] = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    };
    std::memcpy(pushConstants.model, modelMatrix, sizeof(float) * 16);

    const bool worldMapHasImage = worldMapTextureManager_ && worldMapTextureManager_->isInitialized() &&
                                  worldMapTextureManager_->getCurrentImageView() != VK_NULL_HANDLE;
    const bool drawsTexture = floorDrawsTexture(usingFallbackGrid_,
                                                usingExternalTexture_ && externalImageView_ != VK_NULL_HANDLE,
                                                worldMapHasImage);
    pushConstants.color[0] = 1.0f;
    pushConstants.color[1] = 1.0f;
    pushConstants.color[2] = 1.0f;
    pushConstants.color[3] = drawsTexture ? FLOOR_MODE_TEXTURE : FLOOR_MODE_GRID;

    vkCmdPushConstants(commandBuffer, pipeline_->getFloorPipelineLayout(),
                      VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                      0, sizeof(PushConstants), &pushConstants);

    vkCmdDrawIndexed(commandBuffer, floorIndexCount_, 1, 0, 0, 0);

    // Actors are drawn next with the sphere pipeline
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_->getPipeline());
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                           pipeline_->getPipelineLayout(), 0, 1, &descriptorSet_, 0, nullptr);
}
