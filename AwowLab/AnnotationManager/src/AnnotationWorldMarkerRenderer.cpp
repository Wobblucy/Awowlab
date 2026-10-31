#include "AnnotationWorldMarkerRenderer.h"
#include "AnnotationRenderer.h"  // For AnnotationVertex
#include "AnnotationVulkanSetup.h"
#include "AnnotationTypes.h"
#include "WorldMarkerGeometry.h"
#include "ICamera3D.h"
#include <cstring>

bool AnnotationWorldMarkerRenderer::initialize(VkDevice device, VkPhysicalDevice physicalDevice) {
    std::vector<AnnotationVertex> vertices;
    std::vector<uint32_t> indices;
    const float symbolSize = 1.0f;  // Unit size, scaled by model matrix

    WorldMarkerGeometry::generateStar(symbolSize, vertices, indices);
    if (!createSymbolBuffers(device, physicalDevice, vertices, indices, starSymbol_)) {
        return false;
    }

    // The floating orange marker, not the ground circle
    WorldMarkerGeometry::generateCircle(symbolSize, 24, vertices, indices);
    if (!createSymbolBuffers(device, physicalDevice, vertices, indices, circleSymbol_)) {
        return false;
    }

    WorldMarkerGeometry::generateDiamond(symbolSize, vertices, indices);
    if (!createSymbolBuffers(device, physicalDevice, vertices, indices, diamondSymbol_)) {
        return false;
    }

    WorldMarkerGeometry::generateTriangle(symbolSize, vertices, indices);
    if (!createSymbolBuffers(device, physicalDevice, vertices, indices, triangleSymbol_)) {
        return false;
    }

    WorldMarkerGeometry::generateMoon(symbolSize, 16, vertices, indices);
    if (!createSymbolBuffers(device, physicalDevice, vertices, indices, moonSymbol_)) {
        return false;
    }

    WorldMarkerGeometry::generateSquare(symbolSize, vertices, indices);
    if (!createSymbolBuffers(device, physicalDevice, vertices, indices, squareSymbol_)) {
        return false;
    }

    WorldMarkerGeometry::generateCross(symbolSize, 0.25f, vertices, indices);
    if (!createSymbolBuffers(device, physicalDevice, vertices, indices, crossSymbol_)) {
        return false;
    }

    WorldMarkerGeometry::generateSkull(symbolSize, 16, vertices, indices);
    if (!createSymbolBuffers(device, physicalDevice, vertices, indices, skullSymbol_)) {
        return false;
    }

    return true;
}

void AnnotationWorldMarkerRenderer::cleanup(VkDevice device) {
    cleanupSymbolBuffers(device, starSymbol_);
    cleanupSymbolBuffers(device, circleSymbol_);
    cleanupSymbolBuffers(device, diamondSymbol_);
    cleanupSymbolBuffers(device, triangleSymbol_);
    cleanupSymbolBuffers(device, moonSymbol_);
    cleanupSymbolBuffers(device, squareSymbol_);
    cleanupSymbolBuffers(device, crossSymbol_);
    cleanupSymbolBuffers(device, skullSymbol_);
}

void AnnotationWorldMarkerRenderer::render(VkCommandBuffer commandBuffer, VkPipelineLayout pipelineLayout,
                                            const std::vector<const Annotation*>& worldMarkers,
                                            const ICamera3D* camera) {
    if (!camera || worldMarkers.empty()) return;

    float viewMatrix[16];
    camera->getViewMatrix(viewMatrix);

    // Leading part of the annotation pipeline push constants; the rest is left unset
    struct PushConstants {
        float model[16];
        float color[4];
    } pushConstants;

    for (const auto* annotation : worldMarkers) {
        if (!annotation || annotation->worldMarkerType == WorldMarkerType::None) continue;

        float fadeAlpha = annotation->getFadeAtTime(0);  // world markers do not fade over time

        buildBillboardModelMatrix(*annotation, pushConstants.model, viewMatrix);

        float r, g, b;
        getWorldMarkerColor(annotation->worldMarkerType, r, g, b);
        pushConstants.color[0] = r;
        pushConstants.color[1] = g;
        pushConstants.color[2] = b;
        pushConstants.color[3] = 1.0f * fadeAlpha;

        SymbolBuffers* symbol = nullptr;

        switch (annotation->worldMarkerType) {
            case WorldMarkerType::Star:     symbol = &starSymbol_; break;
            case WorldMarkerType::Circle:   symbol = &circleSymbol_; break;
            case WorldMarkerType::Diamond:  symbol = &diamondSymbol_; break;
            case WorldMarkerType::Triangle: symbol = &triangleSymbol_; break;
            case WorldMarkerType::Moon:     symbol = &moonSymbol_; break;
            case WorldMarkerType::Square:   symbol = &squareSymbol_; break;
            case WorldMarkerType::Cross:    symbol = &crossSymbol_; break;
            case WorldMarkerType::Skull:    symbol = &skullSymbol_; break;
            default: continue;
        }

        if (!symbol || symbol->vertexBuffer == VK_NULL_HANDLE ||
            symbol->indexBuffer == VK_NULL_HANDLE || symbol->indexCount == 0) {
            continue;
        }

        VkDeviceSize offsets[] = {0};
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, &symbol->vertexBuffer, offsets);
        vkCmdBindIndexBuffer(commandBuffer, symbol->indexBuffer, 0, VK_INDEX_TYPE_UINT32);

        vkCmdPushConstants(commandBuffer, pipelineLayout,
                          VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                          0, sizeof(PushConstants), &pushConstants);

        vkCmdDrawIndexed(commandBuffer, symbol->indexCount, 1, 0, 0, 0);
    }
}

bool AnnotationWorldMarkerRenderer::createSymbolBuffers(VkDevice device, VkPhysicalDevice physicalDevice,
                                                         const std::vector<AnnotationVertex>& vertices,
                                                         const std::vector<uint32_t>& indices,
                                                         SymbolBuffers& outBuffers) {
    outBuffers.indexCount = static_cast<uint32_t>(indices.size());

    VkDeviceSize vertexBufferSize = sizeof(AnnotationVertex) * vertices.size();
    if (!annotation_vulkan::createBuffer(device, physicalDevice, vertexBufferSize,
                      VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                      outBuffers.vertexBuffer, outBuffers.vertexMemory)) {
        return false;
    }

    void* vertexData;
    vkMapMemory(device, outBuffers.vertexMemory, 0, vertexBufferSize, 0, &vertexData);
    memcpy(vertexData, vertices.data(), vertexBufferSize);
    vkUnmapMemory(device, outBuffers.vertexMemory);

    VkDeviceSize indexBufferSize = sizeof(uint32_t) * indices.size();
    if (!annotation_vulkan::createBuffer(device, physicalDevice, indexBufferSize,
                      VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                      outBuffers.indexBuffer, outBuffers.indexMemory)) {
        return false;
    }

    void* indexData;
    vkMapMemory(device, outBuffers.indexMemory, 0, indexBufferSize, 0, &indexData);
    memcpy(indexData, indices.data(), indexBufferSize);
    vkUnmapMemory(device, outBuffers.indexMemory);

    return true;
}

void AnnotationWorldMarkerRenderer::buildBillboardModelMatrix(const Annotation& annotation, float* modelMatrix,
                                                               const float* viewMatrix) {
    // Transpose of the view rotation, so the symbol always faces the camera
    float billboardRot[9] = {
        viewMatrix[0], viewMatrix[4], viewMatrix[8],   // Row 0 = camera right
        viewMatrix[1], viewMatrix[5], viewMatrix[9],   // Row 1 = camera up
        viewMatrix[2], viewMatrix[6], viewMatrix[10]   // Row 2 = camera forward
    };

    // Three times the ground circle radius so the symbol reads from a distance
    float scale = annotation.dimensions[0] * 3.0f;

    float worldX = annotation.currentX;
    float worldY = annotation.currentY;
    float worldZ = annotation.markerHeight;

    // Column-major Translation * Rotation * Scale
    modelMatrix[0] = scale * billboardRot[0];
    modelMatrix[1] = scale * billboardRot[1];
    modelMatrix[2] = scale * billboardRot[2];
    modelMatrix[3] = 0.0f;
    modelMatrix[4] = scale * billboardRot[3];
    modelMatrix[5] = scale * billboardRot[4];
    modelMatrix[6] = scale * billboardRot[5];
    modelMatrix[7] = 0.0f;
    modelMatrix[8] = scale * billboardRot[6];
    modelMatrix[9] = scale * billboardRot[7];
    modelMatrix[10] = scale * billboardRot[8];
    modelMatrix[11] = 0.0f;
    modelMatrix[12] = worldX;
    modelMatrix[13] = worldY;
    modelMatrix[14] = worldZ;
    modelMatrix[15] = 1.0f;
}

void AnnotationWorldMarkerRenderer::cleanupSymbolBuffers(VkDevice device, SymbolBuffers& buffers) {
    if (buffers.vertexBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, buffers.vertexBuffer, nullptr);
        buffers.vertexBuffer = VK_NULL_HANDLE;
    }
    if (buffers.vertexMemory != VK_NULL_HANDLE) {
        vkFreeMemory(device, buffers.vertexMemory, nullptr);
        buffers.vertexMemory = VK_NULL_HANDLE;
    }
    if (buffers.indexBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, buffers.indexBuffer, nullptr);
        buffers.indexBuffer = VK_NULL_HANDLE;
    }
    if (buffers.indexMemory != VK_NULL_HANDLE) {
        vkFreeMemory(device, buffers.indexMemory, nullptr);
        buffers.indexMemory = VK_NULL_HANDLE;
    }
    buffers.indexCount = 0;
}
