#include "Rendering/SceneRenderer3D.h"
#include "Vulkan/Pipeline/VulkanPipeline3D.h"
#include "Vulkan/Buffers/VulkanBufferManager3D.h"
#include "Geometry/RingGeometry.h"
#include <cstring>
#include <cmath>

bool SceneRenderer3D::createRingBuffers(VkDevice device, VkPhysicalDevice physicalDevice) {
    std::vector<Vertex3D> vertices;
    std::vector<uint32_t> indices;

    // Inner radius 1.0, outer 1.4, 48 segments
    RingGeometry::generateRing(1.0f, 1.4f, 48, vertices, indices);

    if (!vulkan_buffers::createFilledBuffer(device, physicalDevice, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                            vertices.data(), sizeof(Vertex3D) * vertices.size(),
                                            ringVertexBuffer_, ringVertexMemory_)) {
        return false;
    }

    if (!vulkan_buffers::createFilledBuffer(device, physicalDevice, VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                                            indices.data(), sizeof(uint32_t) * indices.size(),
                                            ringIndexBuffer_, ringIndexMemory_)) {
        vulkan_buffers::destroyBuffer(device, ringVertexBuffer_, ringVertexMemory_);
        return false;
    }

    ringIndexCount_ = static_cast<uint32_t>(indices.size());
    hasRingBuffers_ = true;

    return true;
}

void SceneRenderer3D::cleanupRingBuffers(VkDevice device) {
    vulkan_buffers::destroyBuffer(device, ringIndexBuffer_, ringIndexMemory_);
    vulkan_buffers::destroyBuffer(device, ringVertexBuffer_, ringVertexMemory_);
    ringIndexCount_ = 0;
    hasRingBuffers_ = false;
}

void SceneRenderer3D::renderSelectionRing(VkCommandBuffer commandBuffer, float worldX, float worldY, float animationTime) {
    constexpr float baseRadius = 0.75f;     // Starting radius (just outside actor)
    constexpr float maxRadius = 1.0f;       // Maximum expansion radius
    constexpr float pulseSpeed = 0.5f;      // Pulses per second
    constexpr float orangeColor[3] = {1.0f, 0.6f, 0.2f};  // Orange/Gold (at expanded)
    constexpr float whiteColor[3] = {1.0f, 1.0f, 1.0f};   // White (at contracted)
    constexpr float maxAlpha = 0.6f;        // Peak opacity

    // abs(sin) gives a smooth 0 -> 1 -> 0 pulse
    constexpr float pi = 3.14159265358979323846f;
    float phase = std::abs(std::sin(animationTime * pulseSpeed * pi));

    float currentRadius = baseRadius + phase * (maxRadius - baseRadius);

    // White when contracted, orange when expanded
    float colorR = whiteColor[0] + phase * (orangeColor[0] - whiteColor[0]);
    float colorG = whiteColor[1] + phase * (orangeColor[1] - whiteColor[1]);
    float colorB = whiteColor[2] + phase * (orangeColor[2] - whiteColor[2]);

    // Brightest when contracted, which draws the eye to the actor
    float alpha = maxAlpha * (1.0f - phase * 0.5f);  // Range: maxAlpha to maxAlpha*0.5

    if (alpha < 0.01f) {
        return;
    }

    // The mesh is built at inner radius 1.0
    float scale = currentRadius / 1.0f;

    // Z=0.1 keeps the ring just above the floor
    float modelMatrix[16] = {
        scale,  0.0f,   0.0f,   0.0f,
        0.0f,   scale,  0.0f,   0.0f,
        0.0f,   0.0f,   scale,  0.0f,
        worldX, worldY, 0.1f,   1.0f
    };

    struct PushConstants {
        float model[16];
        float color[4];
    } pushConstants;

    std::memcpy(pushConstants.model, modelMatrix, sizeof(float) * 16);
    pushConstants.color[0] = colorR;
    pushConstants.color[1] = colorG;
    pushConstants.color[2] = colorB;
    pushConstants.color[3] = alpha;

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_->getSelectionRingPipeline());

    // Same camera UBO set as the sphere pipeline
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                           pipeline_->getSelectionRingPipelineLayout(), 0, 1, &descriptorSet_, 0, nullptr);

    VkDeviceSize offsets[] = {0};
    VkBuffer vertexBuffers[] = {ringVertexBuffer_};
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
    vkCmdBindIndexBuffer(commandBuffer, ringIndexBuffer_, 0, VK_INDEX_TYPE_UINT32);

    vkCmdPushConstants(commandBuffer, pipeline_->getSelectionRingPipelineLayout(),
                      VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushConstants), &pushConstants);

    vkCmdDrawIndexed(commandBuffer, ringIndexCount_, 1, 0, 0, 0);

    // Re-bind sphere pipeline for any subsequent rendering
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_->getPipeline());
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                           pipeline_->getPipelineLayout(), 0, 1, &descriptorSet_, 0, nullptr);
}
