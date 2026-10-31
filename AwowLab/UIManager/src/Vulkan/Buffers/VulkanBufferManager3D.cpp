#include "Vulkan/Buffers/VulkanBufferManager3D.h"
#include "Geometry/CustomShapeRegistry.h"
#include "Geometry/CubeGeometry.h"
#include "Geometry/PlaneGeometry.h"
#include "Geometry/ShieldGeometry.h"
#include "Geometry/MedicCrossGeometry.h"
#include "Geometry/SwordGeometry.h"
#include "Geometry/BowGeometry.h"
#include "Geometry/ConeGeometry.h"
#include "Geometry/CylinderGeometry.h"
#include "Geometry/DiamondGeometry.h"
#include "Geometry/PyramidGeometry.h"
#include "Geometry/StarGeometry.h"
#include "Geometry/ClassIconGeometry.h"
#include "Core/ErrorLogger.h"
#include <cstring>

bool VulkanBufferManager3D::initialize(VkDevice device, VkPhysicalDevice physicalDevice,
                                      float baseSize, uint32_t sphereSegments, uint32_t sphereRings) {
    // Every shape except the sphere is generated at its bounding width
    const float shapeSize = baseSize * 2.0f;
    constexpr uint32_t BOW_SEGMENTS = 8;
    constexpr uint32_t ROUND_SHAPE_SEGMENTS = 12;
    constexpr uint32_t CLASS_ICON_SEGMENTS = 32;  // smooth circle for the spec icon disc

    std::vector<Vertex3D> vertices;
    std::vector<uint32_t> indices;

    auto upload = [&](ActorShape shape, const char* name) {
        ShapeBuffer& buffer = shapeBuffers_[static_cast<size_t>(shape)];
        if (!createShapeBuffer(device, physicalDevice, vertices.data(), vertices.size(),
                               indices.data(), indices.size(), buffer)) {
            AWOW_LOG_ERROR("VULKAN", std::string("Failed to create ") + name + " buffers");
            return false;
        }
        return true;
    };

    // Sphere is the default for players, cube for everything else
    SphereGeometry::generateUVSphere(baseSize, sphereSegments, sphereRings, vertices, indices);
    if (!upload(ActorShape::Sphere, "sphere")) return false;

    CubeGeometry::generateCube(shapeSize, vertices, indices);
    if (!upload(ActorShape::Cube, "cube")) return false;

    // Role shapes
    ShieldGeometry::generateShield(shapeSize, vertices, indices);
    if (!upload(ActorShape::Shield, "shield")) return false;

    MedicCrossGeometry::generateMedicCross(shapeSize, vertices, indices);
    if (!upload(ActorShape::MedicCross, "medic cross")) return false;

    SwordGeometry::generateSword(shapeSize, vertices, indices);
    if (!upload(ActorShape::Sword, "sword")) return false;

    BowGeometry::generateBow(shapeSize, BOW_SEGMENTS, vertices, indices);
    if (!upload(ActorShape::Bow, "bow")) return false;

    ConeGeometry::generateCone(shapeSize, ROUND_SHAPE_SEGMENTS, vertices, indices);
    if (!upload(ActorShape::Cone, "cone")) return false;

    CylinderGeometry::generateCylinder(shapeSize, ROUND_SHAPE_SEGMENTS, vertices, indices);
    if (!upload(ActorShape::Cylinder, "cylinder")) return false;

    DiamondGeometry::generateDiamond(shapeSize, vertices, indices);
    if (!upload(ActorShape::Diamond, "diamond")) return false;

    PyramidGeometry::generatePyramid(shapeSize, vertices, indices);
    if (!upload(ActorShape::Pyramid, "pyramid")) return false;

    StarGeometry::generateStar(shapeSize, vertices, indices);
    if (!upload(ActorShape::Star, "star")) return false;

    ClassIconGeometry::generateClassIcon(shapeSize, CLASS_ICON_SEGMENTS, vertices, indices);
    if (!upload(ActorShape::ClassIcon, "classicon")) return false;

    if (!createCameraUniformBuffer(device, physicalDevice)) {
        AWOW_LOG_ERROR("VULKAN", "Failed to create camera uniform buffer");
        return false;
    }

    return true;
}

void VulkanBufferManager3D::cleanup(VkDevice device) {
    if (cameraUBOMapped_ != nullptr) {
        vkUnmapMemory(device, cameraUBOMemory_);
        cameraUBOMapped_ = nullptr;
    }
    vulkan_buffers::destroyBuffer(device, cameraUBO_, cameraUBOMemory_);

    for (auto& buffer : shapeBuffers_) {
        buffer.cleanup(device);
    }
}

const ShapeBuffer& VulkanBufferManager3D::getShapeBuffer(ActorShape shape) const {
    size_t index = static_cast<size_t>(shape);
    if (index < shapeBuffers_.size()) {
        return shapeBuffers_[index];
    }
    // Fallback to sphere if invalid shape
    return shapeBuffers_[static_cast<size_t>(ActorShape::Sphere)];
}

bool VulkanBufferManager3D::hasShape(ActorShape shape) const {
    size_t index = static_cast<size_t>(shape);
    if (index < shapeBuffers_.size()) {
        return shapeBuffers_[index].isValid();
    }
    return false;
}

void VulkanBufferManager3D::setCustomShapeRegistry(std::shared_ptr<CustomShapeRegistry> registry) {
    customShapeRegistry_ = std::move(registry);
}

const ShapeBuffer* VulkanBufferManager3D::getCustomShapeBuffer(const std::string& name) const {
    if (customShapeRegistry_) {
        return customShapeRegistry_->getShapeBuffer(name);
    }
    return nullptr;
}

bool VulkanBufferManager3D::hasCustomShape(const std::string& name) const {
    if (customShapeRegistry_) {
        return customShapeRegistry_->hasShape(name);
    }
    return false;
}

std::vector<std::string> VulkanBufferManager3D::getCustomShapeNames() const {
    if (customShapeRegistry_) {
        return customShapeRegistry_->getShapeNames();
    }
    return {};
}

void VulkanBufferManager3D::updateCameraUBO([[maybe_unused]] VkDevice device, const float* viewMatrix, const float* projMatrix) {
    if (cameraUBOMapped_ != nullptr) {
        CameraUBO ubo{};
        std::memcpy(ubo.view, viewMatrix, sizeof(float) * 16);
        std::memcpy(ubo.proj, projMatrix, sizeof(float) * 16);
        std::memcpy(cameraUBOMapped_, &ubo, sizeof(CameraUBO));
    }
}

bool createShapeBuffer(VkDevice device, VkPhysicalDevice physicalDevice,
                       const Vertex3D* vertices, size_t vertexCount,
                       const uint32_t* indices, size_t indexCount,
                       ShapeBuffer& outBuffer) {
    if (!vulkan_buffers::createFilledBuffer(device, physicalDevice, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                            vertices, sizeof(Vertex3D) * vertexCount,
                                            outBuffer.vertexBuffer, outBuffer.vertexMemory)) {
        return false;
    }

    if (!vulkan_buffers::createFilledBuffer(device, physicalDevice, VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                                            indices, sizeof(uint32_t) * indexCount,
                                            outBuffer.indexBuffer, outBuffer.indexMemory)) {
        outBuffer.cleanup(device);
        return false;
    }

    outBuffer.indexCount = static_cast<uint32_t>(indexCount);
    return true;
}

bool VulkanBufferManager3D::createCameraUniformBuffer(VkDevice device, VkPhysicalDevice physicalDevice) {
    VkDeviceSize bufferSize = sizeof(CameraUBO);

    if (!vulkan_buffers::createBuffer(device, physicalDevice, bufferSize,
                                      VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                      cameraUBO_, cameraUBOMemory_)) {
        return false;
    }

    if (vkMapMemory(device, cameraUBOMemory_, 0, bufferSize, 0, &cameraUBOMapped_) != VK_SUCCESS) {
        cameraUBOMapped_ = nullptr;
        return false;
    }

    return true;
}
