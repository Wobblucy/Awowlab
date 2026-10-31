#pragma once

#include <vulkan/vulkan.h>
#include <vector>

struct Annotation;
struct AnnotationVertex;
class ICamera3D;

// Draws the floating raid marker symbols as camera-facing billboards
class AnnotationWorldMarkerRenderer {
public:
    AnnotationWorldMarkerRenderer() = default;
    ~AnnotationWorldMarkerRenderer() = default;

    AnnotationWorldMarkerRenderer(const AnnotationWorldMarkerRenderer&) = delete;
    AnnotationWorldMarkerRenderer& operator=(const AnnotationWorldMarkerRenderer&) = delete;

    bool initialize(VkDevice device, VkPhysicalDevice physicalDevice);

    void cleanup(VkDevice device);

    void render(VkCommandBuffer commandBuffer, VkPipelineLayout pipelineLayout,
                const std::vector<const Annotation*>& worldMarkers,
                const ICamera3D* camera);

private:
    struct SymbolBuffers {
        VkBuffer vertexBuffer = VK_NULL_HANDLE;
        VkDeviceMemory vertexMemory = VK_NULL_HANDLE;
        VkBuffer indexBuffer = VK_NULL_HANDLE;
        VkDeviceMemory indexMemory = VK_NULL_HANDLE;
        uint32_t indexCount = 0;
    };

    SymbolBuffers starSymbol_;
    SymbolBuffers circleSymbol_;
    SymbolBuffers diamondSymbol_;
    SymbolBuffers triangleSymbol_;
    SymbolBuffers moonSymbol_;
    SymbolBuffers squareSymbol_;
    SymbolBuffers crossSymbol_;
    SymbolBuffers skullSymbol_;

    bool createSymbolBuffers(VkDevice device, VkPhysicalDevice physicalDevice,
                             const std::vector<AnnotationVertex>& vertices,
                             const std::vector<uint32_t>& indices,
                             SymbolBuffers& outBuffers);

    void buildBillboardModelMatrix(const Annotation& annotation, float* modelMatrix,
                                   const float* viewMatrix);

    void cleanupSymbolBuffers(VkDevice device, SymbolBuffers& buffers);
};
