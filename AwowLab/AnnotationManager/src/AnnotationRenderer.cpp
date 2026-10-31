#include "AnnotationRenderer.h"
#include "AnnotationManager.h"
#include "AnnotationTypes.h"
#include "AnnotationMotionCalculator.h"
#include "AnnotationVulkanSetup.h"
#include "ICamera3D.h"
#include "Core/ErrorLogger.h"
#include "Geometry/AnnotationGeometryData.h"
#include "Geometry/AnnotationCircleGeometry.h"
#include "Geometry/AnnotationRectangleGeometry.h"
#include "Geometry/AnnotationConeGeometry.h"
#include "Geometry/AnnotationDonutGeometry.h"
#include <cmath>
#include <cstring>
#include <algorithm>

namespace {

// The cone mesh is built at this angle; buildModelMatrix scales it to the requested one.
constexpr float kConeMeshAngleDeg = 60.0f;

constexpr VkShaderStageFlags kPushConstantStages = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

// Layout shared with annotation.vert/.frag
struct PushConstants {
    float model[16];              // 64 bytes
    float color[4];               // 16 bytes
    float elapsedTime_s;          // 4 bytes - Time since encounter start (for animation)
    uint32_t visualStyle;         // 4 bytes - 0=solid, 1=striped, 2=hatched, 3=ripple, 4=swirl
    float centerX;                // 4 bytes - Annotation center X (for ripple distance calc)
    float centerY;                // 4 bytes - Annotation center Y
    // Edge effect parameters (16 bytes)
    uint32_t edgeFlags;           // 4 bytes - bit0=fade, bit1=glow, bit2=outline, bit3=gradient, bit4=useSecondary, bit5=useInnerZone
    float edgeFadeWidth;          // 4 bytes - Normalized fade width
    float outlineWidth;           // 4 bytes - Normalized outline width
    float glowIntensity;          // 4 bytes - Glow strength multiplier
    // Dual color support (8 bytes)
    uint32_t secondaryColorPacked;  // 4 bytes - RGBA8 for gradients/outlines/patterns
    uint32_t innerZoneColorPacked;  // 4 bytes - RGBA8 for center region (safe zones)
    // Packed parameters (8 bytes) - 4 floats packed into 2 uint32s using 16-bit fixed-point
    uint32_t packedRipple;        // 4 bytes - upper16: rippleSpeed*100, lower16: rippleCount*100
    uint32_t packedPattern;       // 4 bytes - upper16: stripeSpacing*100, lower16: innerRadiusRatio*65535
};
// 128 bytes is the minimum push constant size Vulkan guarantees
static_assert(sizeof(PushConstants) == 128, "PushConstants must match the pipeline layout");

uint32_t packRgba8(const std::array<float, 4>& color) {
    uint8_t r = static_cast<uint8_t>(std::clamp(color[0] * 255.0f, 0.0f, 255.0f));
    uint8_t g = static_cast<uint8_t>(std::clamp(color[1] * 255.0f, 0.0f, 255.0f));
    uint8_t b = static_cast<uint8_t>(std::clamp(color[2] * 255.0f, 0.0f, 255.0f));
    uint8_t a = static_cast<uint8_t>(std::clamp(color[3] * 255.0f, 0.0f, 255.0f));
    return (static_cast<uint32_t>(a) << 24) | (static_cast<uint32_t>(b) << 16) |
           (static_cast<uint32_t>(g) << 8) | static_cast<uint32_t>(r);
}

// Copies data into a new host-visible buffer.
bool uploadBuffer(VkDevice device, VkPhysicalDevice physicalDevice, VkBufferUsageFlags usage,
                  const void* data, VkDeviceSize size, VkBuffer& buffer, VkDeviceMemory& memory) {
    if (!annotation_vulkan::createBuffer(device, physicalDevice, size, usage,
                                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                         buffer, memory)) {
        return false;
    }

    void* mapped = nullptr;
    if (vkMapMemory(device, memory, 0, size, 0, &mapped) != VK_SUCCESS) {
        return false;
    }
    memcpy(mapped, data, static_cast<size_t>(size));
    vkUnmapMemory(device, memory);
    return true;
}

template <typename Handle, typename Destroy>
void destroyHandle(VkDevice device, Handle& handle, Destroy destroy) {
    if (handle != VK_NULL_HANDLE) {
        destroy(device, handle, nullptr);
        handle = VK_NULL_HANDLE;
    }
}

} // namespace

AnnotationRenderer::AnnotationRenderer() = default;

AnnotationRenderer::~AnnotationRenderer() {
    // cleanup() must be called explicitly before destruction; it needs the device.
}

bool AnnotationRenderer::initialize(VkDevice device, VkPhysicalDevice physicalDevice,
                                     VkRenderPass renderPass, uint32_t subpass) {
    device_ = device;
    physicalDevice_ = physicalDevice;

    if (!createDescriptorSetLayout(device)) {
        AWOW_LOG_ERROR("ANNOTATION", "Failed to create descriptor set layout");
        return false;
    }

    if (!createPipelineLayout(device)) {
        AWOW_LOG_ERROR("ANNOTATION", "Failed to create pipeline layout");
        return false;
    }

    if (!loadShaders(device)) {
        AWOW_LOG_ERROR("ANNOTATION", "Failed to load annotation shaders (annotation_vert.spv, annotation_frag.spv)");
        return false;
    }

    if (!createGraphicsPipeline(device, renderPass, subpass)) {
        AWOW_LOG_ERROR("ANNOTATION", "Failed to create graphics pipeline");
        return false;
    }

    if (!createDescriptorPool(device)) {
        AWOW_LOG_ERROR("ANNOTATION", "Failed to create descriptor pool");
        return false;
    }

    if (!createDescriptorSet(device)) {
        AWOW_LOG_ERROR("ANNOTATION", "Failed to create descriptor set");
        return false;
    }

    if (!uploadMesh(device, physicalDevice, annotation_geometry::createCircleGeometry(32), circleMesh_)) {
        AWOW_LOG_ERROR("ANNOTATION", "Failed to create circle geometry");
        return false;
    }

    if (!uploadMesh(device, physicalDevice, annotation_geometry::createRectangleGeometry(), rectMesh_)) {
        AWOW_LOG_ERROR("ANNOTATION", "Failed to create rectangle geometry");
        return false;
    }

    if (!uploadMesh(device, physicalDevice, annotation_geometry::createConeGeometry(16, kConeMeshAngleDeg), coneMesh_)) {
        AWOW_LOG_ERROR("ANNOTATION", "Failed to create cone geometry");
        return false;
    }

    if (!uploadMesh(device, physicalDevice, annotation_geometry::createDonutGeometry(32, 0.5f), donutMesh_)) {
        AWOW_LOG_ERROR("ANNOTATION", "Failed to create donut geometry");
        return false;
    }

    if (!worldMarkerRenderer_.initialize(device, physicalDevice)) {
        AWOW_LOG_ERROR("ANNOTATION", "Failed to initialize world marker renderer");
        return false;
    }

    isInitialized_ = true;
    return true;
}

void AnnotationRenderer::cleanup(VkDevice device) {
    vkDeviceWaitIdle(device);

    destroyMesh(device, circleMesh_);
    destroyMesh(device, rectMesh_);
    destroyMesh(device, coneMesh_);
    destroyMesh(device, donutMesh_);

    worldMarkerRenderer_.cleanup(device);

    destroyHandle(device, descriptorPool_, vkDestroyDescriptorPool);
    destroyHandle(device, pipeline_, vkDestroyPipeline);
    destroyHandle(device, pipelineLayout_, vkDestroyPipelineLayout);
    destroyHandle(device, descriptorSetLayout_, vkDestroyDescriptorSetLayout);
    destroyHandle(device, vertShaderModule_, vkDestroyShaderModule);
    destroyHandle(device, fragShaderModule_, vkDestroyShaderModule);

    isInitialized_ = false;
}

void AnnotationRenderer::setCameraUBO(VkBuffer cameraUBO) {
    cameraUBO_ = cameraUBO;

    if (device_ != VK_NULL_HANDLE && descriptorSet_ != VK_NULL_HANDLE && cameraUBO_ != VK_NULL_HANDLE) {
        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = cameraUBO_;
        bufferInfo.offset = 0;
        bufferInfo.range = sizeof(float) * 32;  // 2 mat4 = 32 floats

        VkWriteDescriptorSet descriptorWrite{};
        descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrite.dstSet = descriptorSet_;
        descriptorWrite.dstBinding = 0;
        descriptorWrite.dstArrayElement = 0;
        descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptorWrite.descriptorCount = 1;
        descriptorWrite.pBufferInfo = &bufferInfo;

        vkUpdateDescriptorSets(device_, 1, &descriptorWrite, 0, nullptr);
    }
}

void AnnotationRenderer::render([[maybe_unused]] VkDevice device, VkCommandBuffer commandBuffer, VkExtent2D extent,
                                 const AnnotationManager* manager,
                                 const ICamera3D* camera,
                                 int32_t currentTime_ms) {
    if (!isInitialized_) {
        return;
    }
    if (!manager) {
        return;
    }
    if (!camera) {
        return;
    }
    if (cameraUBO_ == VK_NULL_HANDLE) {
        AWOW_LOG_WARNING("ANNOTATION", "cameraUBO_ is NULL - skipping annotation render");
        return;
    }

    auto visibleAnnotations = manager->getVisibleAnnotations(currentTime_ms);
    if (visibleAnnotations.empty()) {
        return;
    }

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);

    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            pipelineLayout_, 0, 1, &descriptorSet_, 0, nullptr);

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

    float aspectRatio = static_cast<float>(extent.width) / static_cast<float>(extent.height);

    float elapsedTime_s = static_cast<float>(currentTime_ms) / 1000.0f;

    // World marker symbols are billboards drawn in a second pass
    std::vector<const Annotation*> worldMarkers;

    for (const auto* annotation : visibleAnnotations) {
        float fadeAlpha = annotation->getFadeAtTime(currentTime_ms);
        renderAnnotation(commandBuffer, *annotation, aspectRatio, fadeAlpha, elapsedTime_s, currentTime_ms);


        if (annotation->worldMarkerType != WorldMarkerType::None) {
            worldMarkers.push_back(annotation);
        }
    }

    if (!worldMarkers.empty()) {
        worldMarkerRenderer_.render(commandBuffer, pipelineLayout_, worldMarkers, camera);
    }
}

void AnnotationRenderer::renderAnnotation(VkCommandBuffer commandBuffer, const Annotation& annotation,
                                           [[maybe_unused]] float aspectRatio, float fadeAlpha,
                                           float elapsedTime_s, int32_t currentTime_ms) {
    MotionState motionState = AnnotationMotionCalculator::calculate(annotation, currentTime_ms);

    // e.g. a projectile that has landed with the Disappear end behaviour
    if (!motionState.visible) {
        return;
    }

    PushConstants pushConstants;

    pushConstants.color[0] = annotation.color[0];
    pushConstants.color[1] = annotation.color[1];
    pushConstants.color[2] = annotation.color[2];
    // With raindrops the base shape is dimmed to 10% so the drops read as the effect
    float baseAlphaMultiplier = (annotation.motion.type == AnnotationMotionType::Raindrops) ? 0.1f : 1.0f;
    pushConstants.color[3] = annotation.color[3] * fadeAlpha * baseAlphaMultiplier;

    pushConstants.elapsedTime_s = elapsedTime_s;
    pushConstants.visualStyle = static_cast<uint32_t>(annotation.visualStyle);

    float innerRadiusRatio = 0.0f;
    if (annotation.shape == AnnotationShape::Donut) {
        float outerRadius = annotation.dimensions[0];
        float innerRadius = annotation.dimensions[1];
        innerRadiusRatio = (outerRadius > 0.0f) ? (innerRadius / outerRadius) : 0.5f;
    }

    // rippleSpeed in the upper 16 bits, rippleCount in the lower, each x100
    // for 0.01 precision (max ~655)
    uint32_t rippleSpeedPacked = static_cast<uint32_t>(std::clamp(annotation.rippleSpeed * 100.0f, 0.0f, 65535.0f));
    uint32_t rippleCountPacked = static_cast<uint32_t>(std::clamp(annotation.rippleCount * 100.0f, 0.0f, 65535.0f));
    pushConstants.packedRipple = (rippleSpeedPacked << 16) | rippleCountPacked;

    // stripeSpacing x100 in the upper 16 bits, innerRadiusRatio (0-1) x65535 in the lower
    uint32_t stripeSpacingPacked = static_cast<uint32_t>(std::clamp(annotation.stripeSpacing * 100.0f, 0.0f, 65535.0f));
    uint32_t innerRatioPacked = static_cast<uint32_t>(std::clamp(innerRadiusRatio * 65535.0f, 0.0f, 65535.0f));
    pushConstants.packedPattern = (stripeSpacingPacked << 16) | innerRatioPacked;

    // Ripple distance is measured from here; a sourced line is centred on its midpoint
    if (annotation.shape == AnnotationShape::Line && annotation.hasSourcePosition) {
        pushConstants.centerX = (annotation.sourceX + motionState.posX) * 0.5f;
        pushConstants.centerY = (annotation.sourceY + motionState.posY) * 0.5f;
    } else {
        pushConstants.centerX = motionState.posX;
        pushConstants.centerY = motionState.posY;
    }

    uint32_t edgeFlags = 0;
    if (annotation.enableEdgeFade)   edgeFlags |= 0x1;
    if (annotation.enableGlow)       edgeFlags |= 0x2;
    if (annotation.enableOutline)    edgeFlags |= 0x4;
    if (annotation.enableGradient)   edgeFlags |= 0x8;
    if (annotation.useSecondaryColor) edgeFlags |= 0x10;
    if (annotation.useInnerZoneColor) edgeFlags |= 0x20;
    pushConstants.edgeFlags = edgeFlags;
    pushConstants.edgeFadeWidth = annotation.edgeFadeWidth;
    pushConstants.outlineWidth = annotation.outlineWidth;
    pushConstants.glowIntensity = annotation.glowIntensity;

    pushConstants.secondaryColorPacked = packRgba8(annotation.secondaryColor);
    pushConstants.innerZoneColorPacked = packRgba8(annotation.innerZoneColor);

    const GpuMesh* mesh = &circleMesh_;
    switch (annotation.shape) {
        case AnnotationShape::Rectangle:
        case AnnotationShape::Line:
        case AnnotationShape::Star:
            mesh = &rectMesh_;
            break;
        case AnnotationShape::Cone:
            mesh = &coneMesh_;
            break;
        case AnnotationShape::Donut:
            mesh = &donutMesh_;
            break;
        default:
            break;
    }
    bindMesh(commandBuffer, *mesh);

    // A star is one rectangle draw per ray
    if (annotation.shape == AnnotationShape::Star) {
        constexpr float TWO_PI = 6.28318530718f;
        float baseRotation = motionState.rotation;
        int numRays = std::max(3, std::min(12, static_cast<int>(annotation.pointCount)));

        for (int i = 0; i < numRays; ++i) {
            float rayAngle = baseRotation + (TWO_PI * i / numRays);
            buildStarRayModelMatrix(annotation, rayAngle, pushConstants.model, motionState.posX, motionState.posY);

            vkCmdPushConstants(commandBuffer, pipelineLayout_, kPushConstantStages,
                               0, sizeof(PushConstants), &pushConstants);
            vkCmdDrawIndexed(commandBuffer, mesh->indexCount, 1, 0, 0, 0);
        }
    } else {
        buildModelMatrix(annotation, pushConstants.model, motionState.posX, motionState.posY, motionState.rotation);

        vkCmdPushConstants(commandBuffer, pipelineLayout_, kPushConstantStages,
                           0, sizeof(PushConstants), &pushConstants);
        vkCmdDrawIndexed(commandBuffer, mesh->indexCount, 1, 0, 0, 0);
    }

    // Preview of a projectile: draw its travel path with an arrowhead
    if (annotation.isPreview && annotation.motion.type == AnnotationMotionType::Projectile) {
        float startX = annotation.worldX;
        float startY = annotation.worldY;
        float endX = startX + annotation.motion.endOffsetX;
        float endY = startY + annotation.motion.endOffsetY;

        float dx = endX - startX;
        float dy = endY - startY;
        float distance = std::sqrt(dx * dx + dy * dy);

        if (distance > 0.1f) {
            float midX = (startX + endX) * 0.5f;
            float midY = (startY + endY) * 0.5f;

            float lineRotation = std::atan2(dx, dy);

            float lineModel[16];
            memset(lineModel, 0, sizeof(float) * 16);

            float lineWidth = 0.3f;
            float lineHeight = 0.1f;
            float cosR = std::cos(lineRotation);
            float sinR = std::sin(lineRotation);

            lineModel[0] = lineWidth * cosR;
            lineModel[2] = lineWidth * sinR;

            lineModel[5] = lineHeight;

            lineModel[8] = -distance * sinR;
            lineModel[10] = distance * cosR;

            lineModel[12] = midX;
            lineModel[13] = midY;
            lineModel[14] = annotation.dimensions[1] + 0.05f;
            lineModel[15] = 1.0f;

            // Plain solid white: everything not set here stays zero
            PushConstants linePush{};
            memcpy(linePush.model, lineModel, sizeof(float) * 16);
            linePush.color[0] = 1.0f;
            linePush.color[1] = 1.0f;
            linePush.color[2] = 1.0f;
            linePush.color[3] = 0.8f;
            linePush.centerX = midX;
            linePush.centerY = midY;

            bindMesh(commandBuffer, rectMesh_);
            vkCmdPushConstants(commandBuffer, pipelineLayout_, kPushConstantStages,
                               0, sizeof(PushConstants), &linePush);
            vkCmdDrawIndexed(commandBuffer, rectMesh_.indexCount, 1, 0, 0, 0);

            // Arrowhead at the end point, drawn with the cone geometry
            float arrowModel[16];
            memset(arrowModel, 0, sizeof(float) * 16);

            float arrowSize = 0.8f;
            float arrowHeight = 0.15f;

            arrowModel[0] = arrowSize * cosR;
            arrowModel[2] = arrowSize * sinR;
            arrowModel[5] = arrowHeight;
            arrowModel[8] = -arrowSize * sinR;
            arrowModel[10] = arrowSize * cosR;
            arrowModel[12] = endX;
            arrowModel[13] = endY;
            arrowModel[14] = annotation.dimensions[1] + 0.05f;
            arrowModel[15] = 1.0f;

            memcpy(linePush.model, arrowModel, sizeof(float) * 16);

            bindMesh(commandBuffer, coneMesh_);
            vkCmdPushConstants(commandBuffer, pipelineLayout_, kPushConstantStages,
                               0, sizeof(PushConstants), &linePush);
            vkCmdDrawIndexed(commandBuffer, coneMesh_.indexCount, 1, 0, 0, 0);
        }
    }

    if (annotation.motion.type == AnnotationMotionType::Raindrops) {
        auto drops = AnnotationMotionCalculator::calculateRaindrops(annotation, currentTime_ms);

        if (!drops.empty()) {
            bindMesh(commandBuffer, circleMesh_);

            float dropHeight = 0.05f;

            for (const auto& drop : drops) {
                float dropModel[16];
                memset(dropModel, 0, sizeof(float) * 16);

                float dropRadius = annotation.motion.dropRadius;

                // Geometry is Y-up: local X -> world X, local Y -> world Z,
                // local Z -> world Y. Drops are never rotated.
                dropModel[0] = dropRadius;
                dropModel[1] = 0.0f;
                dropModel[2] = 0.0f;
                dropModel[3] = 0.0f;

                dropModel[4] = 0.0f;
                dropModel[5] = 0.0f;
                dropModel[6] = dropHeight;
                dropModel[7] = 0.0f;

                dropModel[8] = 0.0f;
                dropModel[9] = dropRadius;
                dropModel[10] = 0.0f;
                dropModel[11] = 0.0f;

                dropModel[12] = drop.x;
                dropModel[13] = drop.y;
                dropModel[14] = dropHeight * 0.5f;
                dropModel[15] = 1.0f;

                PushConstants dropPush{};
                memcpy(dropPush.model, dropModel, sizeof(float) * 16);

                // Drops use the inverted colour so they stand out against the base shape
                dropPush.color[0] = 1.0f - annotation.color[0];
                dropPush.color[1] = 1.0f - annotation.color[1];
                dropPush.color[2] = 1.0f - annotation.color[2];
                dropPush.color[3] = annotation.color[3] * drop.alpha * fadeAlpha;

                dropPush.elapsedTime_s = elapsedTime_s;
                dropPush.visualStyle = static_cast<uint32_t>(AnnotationVisualStyle::Swirl);
                dropPush.centerX = drop.x;
                dropPush.centerY = drop.y;
                dropPush.packedPattern = (static_cast<uint32_t>(0.4f * 100.0f) << 16) | 0;  // stripeSpacing=0.4

                vkCmdPushConstants(commandBuffer, pipelineLayout_, kPushConstantStages,
                                   0, sizeof(PushConstants), &dropPush);
                vkCmdDrawIndexed(commandBuffer, circleMesh_.indexCount, 1, 0, 0, 0);
            }
        }
    }
}

void AnnotationRenderer::buildModelMatrix(const Annotation& annotation, float* modelMatrix,
                                           float motionPosX, float motionPosY, float motionRotation) {
    memset(modelMatrix, 0, sizeof(float) * 16);

    // The world is Z-up with X-Y as the ground, but the geometry is built Y-up,
    // so local Y maps to world Z and local Z to world Y.
    float scaleX = 1.0f;
    float scaleY = 1.0f;   // height, becomes world Z
    float scaleZ = 1.0f;   // becomes world Y
    float rotation = motionRotation;

    switch (annotation.shape) {
        case AnnotationShape::Circle:
            // Unit-radius geometry, scaled uniformly in X-Z
            scaleX = annotation.dimensions[0];
            scaleY = annotation.dimensions[1];
            scaleZ = annotation.dimensions[0];
            break;

        case AnnotationShape::Rectangle:
            // Unit cube geometry (-0.5 to 0.5)
            scaleX = annotation.dimensions[0];
            scaleY = annotation.dimensions[2];

            if (annotation.hasSourcePosition) {
                // Source-to-target: the rectangle spans the gap between the two
                float dx = motionPosX - annotation.sourceX;
                float dy = motionPosY - annotation.sourceY;
                float distance = std::sqrt(dx * dx + dy * dy);

                scaleZ = distance;
                rotation = -std::atan2(dx, dy);
            } else {
                scaleZ = annotation.dimensions[1];
            }
            break;

        case AnnotationShape::Line:
            // Drawn with the rectangle geometry
            scaleX = annotation.dimensions[1];
            scaleY = annotation.dimensions[2];

            if (annotation.hasSourcePosition) {
                // For projectile motion motionPosX/Y is the animated position
                float dx = motionPosX - annotation.sourceX;
                float dy = motionPosY - annotation.sourceY;
                float distance = std::sqrt(dx * dx + dy * dy);

                scaleZ = distance;

                // At rotation 0 local Z points at world +Y. The matrix puts -sin(R)
                // in the X component, so atan2(dx, dy) has to be negated.
                rotation = -std::atan2(dx, dy);
            } else {
                scaleZ = annotation.dimensions[0];
            }
            break;

        case AnnotationShape::Cone: {
            float length = annotation.dimensions[0];
            float targetAngleDeg = annotation.dimensions[1];

            // Widen or narrow the mesh in X to reach the requested angle.
            constexpr float meshHalfAngleRad = (kConeMeshAngleDeg / 2.0f) * kAnnotationDegToRad;
            float targetHalfAngleRad = (targetAngleDeg / 2.0f) * kAnnotationDegToRad;
            float widthScale = std::tan(targetHalfAngleRad) / std::tan(meshHalfAngleRad);

            scaleX = length * widthScale;
            scaleY = annotation.dimensions[2];
            scaleZ = length;

            // Fixate: aim from the source NPC at the target player, same
            // convention as Line
            if (annotation.hasSourcePosition) {
                float dx = motionPosX - annotation.sourceX;
                float dy = motionPosY - annotation.sourceY;
                rotation = -std::atan2(dx, dy);
            }
            break;
        }

        case AnnotationShape::Donut:
            // Unit outer radius; the shader cuts the hole from innerRadiusRatio
            scaleX = annotation.dimensions[0];
            scaleY = annotation.dimensions[2];
            scaleZ = annotation.dimensions[0];
            break;

        default:
            break;
    }

    float cosR = std::cos(rotation);
    float sinR = std::sin(rotation);

    // Rz * SwapYZ * Scale, column-major
    modelMatrix[0] = scaleX * cosR;   // local X -> world X
    modelMatrix[1] = scaleX * sinR;   // local X -> world Y
    modelMatrix[2] = 0.0f;            // local X -> world Z
    modelMatrix[3] = 0.0f;

    modelMatrix[4] = 0.0f;            // local Y -> world X
    modelMatrix[5] = 0.0f;            // local Y -> world Y
    modelMatrix[6] = scaleY;          // local Y -> world Z (height)
    modelMatrix[7] = 0.0f;

    modelMatrix[8] = -scaleZ * sinR;  // local Z -> world X
    modelMatrix[9] = scaleZ * cosR;   // local Z -> world Y
    modelMatrix[10] = 0.0f;           // local Z -> world Z
    modelMatrix[11] = 0.0f;

    float posX = motionPosX;
    float posY = motionPosY;

    // A sourced line or rectangle is centred between source and target
    if ((annotation.shape == AnnotationShape::Line || annotation.shape == AnnotationShape::Rectangle) &&
        annotation.hasSourcePosition) {
        posX = (annotation.sourceX + motionPosX) * 0.5f;
        posY = (annotation.sourceY + motionPosY) * 0.5f;
    }

    // A sourced cone (Fixate) puts its apex on the source NPC
    if (annotation.shape == AnnotationShape::Cone && annotation.hasSourcePosition) {
        posX = annotation.sourceX;
        posY = annotation.sourceY;
    }

    modelMatrix[12] = posX;
    modelMatrix[13] = posY;
    modelMatrix[14] = scaleY * 0.5f + 0.1f;        // sit on the ground, lifted slightly
    modelMatrix[15] = 1.0f;
}

void AnnotationRenderer::buildStarRayModelMatrix(const Annotation& annotation, float rayAngle, float* modelMatrix,
                                                  float motionPosX, float motionPosY) {
    memset(modelMatrix, 0, sizeof(float) * 16);

    // dimensions[3] (base rotation) is already folded into rayAngle
    float rayLength = annotation.dimensions[0];
    float rayWidth = annotation.dimensions[1];
    float rayHeight = annotation.dimensions[2];

    float scaleX = rayWidth;
    float scaleY = rayHeight;
    float scaleZ = rayLength;

    float cosR = std::cos(rayAngle);
    float sinR = std::sin(rayAngle);

    // Same Rz * SwapYZ * Scale layout as buildModelMatrix
    modelMatrix[0] = scaleX * cosR;   // local X -> world X
    modelMatrix[1] = scaleX * sinR;   // local X -> world Y
    modelMatrix[2] = 0.0f;            // local X -> world Z
    modelMatrix[3] = 0.0f;

    modelMatrix[4] = 0.0f;            // local Y -> world X
    modelMatrix[5] = 0.0f;            // local Y -> world Y
    modelMatrix[6] = scaleY;          // local Y -> world Z (height)
    modelMatrix[7] = 0.0f;

    modelMatrix[8] = -scaleZ * sinR;  // local Z -> world X
    modelMatrix[9] = scaleZ * cosR;   // local Z -> world Y
    modelMatrix[10] = 0.0f;           // local Z -> world Z
    modelMatrix[11] = 0.0f;

    // The geometry is centred on its origin, so shift it half a ray length
    // outward so the ray starts at the annotation centre.
    float offsetDist = rayLength * 0.5f;
    float posX = motionPosX - offsetDist * sinR;
    float posY = motionPosY + offsetDist * cosR;

    modelMatrix[12] = posX;
    modelMatrix[13] = posY;
    modelMatrix[14] = scaleY * 0.5f + 0.1f;
    modelMatrix[15] = 1.0f;
}

bool AnnotationRenderer::createDescriptorSetLayout(VkDevice device) {
    return annotation_vulkan::createDescriptorSetLayout(device, descriptorSetLayout_);
}

bool AnnotationRenderer::createPipelineLayout(VkDevice device) {
    return annotation_vulkan::createPipelineLayout(device, descriptorSetLayout_, pipelineLayout_);
}

bool AnnotationRenderer::loadShaders(VkDevice device) {
    return annotation_vulkan::loadShaders(device, "shaders/annotation_vert.spv", "shaders/annotation_frag.spv",
                                          vertShaderModule_, fragShaderModule_);
}

bool AnnotationRenderer::createGraphicsPipeline(VkDevice device, VkRenderPass renderPass, uint32_t subpass) {
    return annotation_vulkan::createGraphicsPipeline(device, renderPass, subpass, pipelineLayout_,
                                                     vertShaderModule_, fragShaderModule_, pipeline_);
}

bool AnnotationRenderer::createDescriptorPool(VkDevice device) {
    return annotation_vulkan::createDescriptorPool(device, descriptorPool_);
}

bool AnnotationRenderer::createDescriptorSet(VkDevice device) {
    return annotation_vulkan::createDescriptorSet(device, descriptorPool_, descriptorSetLayout_, descriptorSet_);
}

bool AnnotationRenderer::uploadMesh(VkDevice device, VkPhysicalDevice physicalDevice,
                                    const AnnotationGeometryData& geometry, GpuMesh& mesh) {
    if (!geometry.isValid()) {
        return false;
    }

    mesh.indexCount = static_cast<uint32_t>(geometry.indices.size());

    return uploadBuffer(device, physicalDevice, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                        geometry.vertices.data(), sizeof(AnnotationVertex) * geometry.vertices.size(),
                        mesh.vertexBuffer, mesh.vertexMemory) &&
           uploadBuffer(device, physicalDevice, VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                        geometry.indices.data(), sizeof(uint32_t) * geometry.indices.size(),
                        mesh.indexBuffer, mesh.indexMemory);
}

void AnnotationRenderer::destroyMesh(VkDevice device, GpuMesh& mesh) {
    destroyHandle(device, mesh.vertexBuffer, vkDestroyBuffer);
    destroyHandle(device, mesh.vertexMemory, vkFreeMemory);
    destroyHandle(device, mesh.indexBuffer, vkDestroyBuffer);
    destroyHandle(device, mesh.indexMemory, vkFreeMemory);
}

void AnnotationRenderer::bindMesh(VkCommandBuffer commandBuffer, const GpuMesh& mesh) {
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, &mesh.vertexBuffer, offsets);
    vkCmdBindIndexBuffer(commandBuffer, mesh.indexBuffer, 0, VK_INDEX_TYPE_UINT32);
}
