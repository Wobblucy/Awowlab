#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include <memory>
#include <cstdint>
#include <string>
#include "AnnotationWorldMarkerRenderer.h"

class AnnotationManager;
class ICamera3D;
struct Annotation;
struct AnnotationGeometryData;

// Vertex structure (matches UIManager's Vertex3D for compatibility)
struct AnnotationVertex {
    float position[3];
    float normal[3];
    float color[3];
    float uv[2];
};

// Renders annotations using a transparent Vulkan pipeline
class AnnotationRenderer {
public:
    AnnotationRenderer();
    ~AnnotationRenderer();

    AnnotationRenderer(const AnnotationRenderer&) = delete;
    AnnotationRenderer& operator=(const AnnotationRenderer&) = delete;

    bool initialize(VkDevice device, VkPhysicalDevice physicalDevice,
                    VkRenderPass renderPass, uint32_t subpass);

    void cleanup(VkDevice device);

    // The camera UBO is owned by the main scene renderer
    void setCameraUBO(VkBuffer cameraUBO);

    void render(VkDevice device, VkCommandBuffer commandBuffer, VkExtent2D extent,
                const AnnotationManager* manager,
                const ICamera3D* camera,
                int32_t currentTime_ms);

    bool isInitialized() const { return isInitialized_; }

private:
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptorSetLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;
    VkDescriptorSet descriptorSet_ = VK_NULL_HANDLE;

    VkShaderModule vertShaderModule_ = VK_NULL_HANDLE;
    VkShaderModule fragShaderModule_ = VK_NULL_HANDLE;

    // Vertex and index buffers for one base shape
    struct GpuMesh {
        VkBuffer vertexBuffer = VK_NULL_HANDLE;
        VkDeviceMemory vertexMemory = VK_NULL_HANDLE;
        VkBuffer indexBuffer = VK_NULL_HANDLE;
        VkDeviceMemory indexMemory = VK_NULL_HANDLE;
        uint32_t indexCount = 0;
    };

    // Rectangle also draws lines and star rays
    GpuMesh circleMesh_;
    GpuMesh rectMesh_;
    GpuMesh coneMesh_;
    GpuMesh donutMesh_;

    AnnotationWorldMarkerRenderer worldMarkerRenderer_;

    VkBuffer cameraUBO_ = VK_NULL_HANDLE;

    VkDevice device_ = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;

    bool isInitialized_ = false;

    bool createDescriptorSetLayout(VkDevice device);
    bool createPipelineLayout(VkDevice device);
    bool loadShaders(VkDevice device);
    bool createGraphicsPipeline(VkDevice device, VkRenderPass renderPass, uint32_t subpass);
    bool createDescriptorPool(VkDevice device);
    bool createDescriptorSet(VkDevice device);

    static bool uploadMesh(VkDevice device, VkPhysicalDevice physicalDevice,
                           const AnnotationGeometryData& geometry, GpuMesh& mesh);
    static void destroyMesh(VkDevice device, GpuMesh& mesh);
    static void bindMesh(VkCommandBuffer commandBuffer, const GpuMesh& mesh);

    void renderAnnotation(VkCommandBuffer commandBuffer, const Annotation& annotation,
                          float aspectRatio, float fadeAlpha, float elapsedTime_s, int32_t currentTime_ms);
    void buildModelMatrix(const Annotation& annotation, float* modelMatrix,
                          float motionPosX, float motionPosY, float motionRotation);
    void buildStarRayModelMatrix(const Annotation& annotation, float rayAngle, float* modelMatrix,
                                  float motionPosX, float motionPosY);
};
