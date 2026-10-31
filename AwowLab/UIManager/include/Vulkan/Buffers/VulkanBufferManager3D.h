#pragma once
#include <vulkan/vulkan.h>
#include "Vulkan/Buffers/VulkanBufferUtils.h"
#include "Geometry/SphereGeometry.h"
#include "UI/ActorAppearanceSettings.h"
#include <vector>
#include <array>
#include <memory>

class CustomShapeRegistry;

struct CameraUBO {
    float view[16];  // View matrix
    float proj[16];  // Projection matrix
};

// Encapsulates GPU resources for a single shape type
struct ShapeBuffer {
    VkBuffer vertexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory vertexMemory = VK_NULL_HANDLE;
    VkBuffer indexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory indexMemory = VK_NULL_HANDLE;
    uint32_t indexCount = 0;

    bool isValid() const {
        return vertexBuffer != VK_NULL_HANDLE && indexBuffer != VK_NULL_HANDLE;
    }

    void cleanup(VkDevice device) {
        vulkan_buffers::destroyBuffer(device, indexBuffer, indexMemory);
        vulkan_buffers::destroyBuffer(device, vertexBuffer, vertexMemory);
        indexCount = 0;
    }
};

// Uploads a mesh into host-visible vertex and index buffers. On failure outBuffer is left empty.
bool createShapeBuffer(VkDevice device, VkPhysicalDevice physicalDevice,
                       const Vertex3D* vertices, size_t vertexCount,
                       const uint32_t* indices, size_t indexCount,
                       ShapeBuffer& outBuffer);

// Manages Vulkan buffers for 3D rendering (supports multiple geometries)
class VulkanBufferManager3D {
public:
    VulkanBufferManager3D() = default;
    ~VulkanBufferManager3D() = default;

    bool initialize(VkDevice device, VkPhysicalDevice physicalDevice, float baseSize,
                   uint32_t sphereSegments, uint32_t sphereRings);

    void cleanup(VkDevice device);

    void updateCameraUBO(VkDevice device, const float* viewMatrix, const float* projMatrix);

    const ShapeBuffer& getShapeBuffer(ActorShape shape) const;
    bool hasShape(ActorShape shape) const;

    void setCustomShapeRegistry(std::shared_ptr<CustomShapeRegistry> registry);
    const ShapeBuffer* getCustomShapeBuffer(const std::string& name) const;
    bool hasCustomShape(const std::string& name) const;
    std::vector<std::string> getCustomShapeNames() const;
    CustomShapeRegistry* getCustomShapeRegistry() const { return customShapeRegistry_.get(); }

    VkBuffer getSphereVertexBuffer() const { return shapeBuffers_[static_cast<size_t>(ActorShape::Sphere)].vertexBuffer; }
    VkBuffer getSphereIndexBuffer() const { return shapeBuffers_[static_cast<size_t>(ActorShape::Sphere)].indexBuffer; }
    uint32_t getSphereIndexCount() const { return shapeBuffers_[static_cast<size_t>(ActorShape::Sphere)].indexCount; }

    VkBuffer getCubeVertexBuffer() const { return shapeBuffers_[static_cast<size_t>(ActorShape::Cube)].vertexBuffer; }
    VkBuffer getCubeIndexBuffer() const { return shapeBuffers_[static_cast<size_t>(ActorShape::Cube)].indexBuffer; }
    uint32_t getCubeIndexCount() const { return shapeBuffers_[static_cast<size_t>(ActorShape::Cube)].indexCount; }

    // Unqualified getters return the sphere buffers
    VkBuffer getVertexBuffer() const { return getSphereVertexBuffer(); }
    VkBuffer getIndexBuffer() const { return getSphereIndexBuffer(); }
    VkBuffer getCameraUBO() const { return cameraUBO_; }
    uint32_t getIndexCount() const { return getSphereIndexCount(); }

private:
    // Shape registry: indexed by ActorShape enum
    std::array<ShapeBuffer, static_cast<size_t>(ActorShape::COUNT)> shapeBuffers_;

    // Custom shape registry (managed externally, set via setCustomShapeRegistry)
    std::shared_ptr<CustomShapeRegistry> customShapeRegistry_;

    VkBuffer cameraUBO_ = VK_NULL_HANDLE;
    VkDeviceMemory cameraUBOMemory_ = VK_NULL_HANDLE;
    void* cameraUBOMapped_ = nullptr;  // Persistent mapping

    bool createCameraUniformBuffer(VkDevice device, VkPhysicalDevice physicalDevice);
};
