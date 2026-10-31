#include "Rendering/SceneRenderer3D.h"
#include "Color/ActorColorGenerator.h"
#include "Vulkan/Pipeline/VulkanPipeline3D.h"
#include "Vulkan/Buffers/VulkanBufferManager3D.h"
#include "Geometry/CustomShapeRegistry.h"
#include "UI/SpecIconLoader.h"
#include "Camera/Camera3D.h"
#include "Animation/ActorPositionInterpolator.h"
#include "UI/ActorVisibilitySettings.h"
#include "UI/ActorAppearanceSettings.h"
#include <cstring>
#include <cmath>
#include <array>

static void multiplyRotation3x3(const float a[9], const float b[9], float out[9]) {
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            out[row * 3 + col] = a[row * 3 + 0] * b[0 * 3 + col] +
                                  a[row * 3 + 1] * b[1 * 3 + col] +
                                  a[row * 3 + 2] * b[2 * 3 + col];
        }
    }
}

// Rotation matrix from Euler angles in degrees
static void buildRotationFromEuler(float rotX, float rotY, float rotZ, float out[9]) {
    constexpr float DEG_TO_RAD = 3.14159265358979323846f / 180.0f;
    float rx = rotX * DEG_TO_RAD;
    float ry = rotY * DEG_TO_RAD;
    float rz = rotZ * DEG_TO_RAD;

    float cx = std::cos(rx), sx = std::sin(rx);
    float cy = std::cos(ry), sy = std::sin(ry);
    float cz = std::cos(rz), sz = std::sin(rz);

    // Z * Y * X: X is applied first
    out[0] = cy * cz;
    out[1] = cz * sx * sy - cx * sz;
    out[2] = cx * cz * sy + sx * sz;
    out[3] = cy * sz;
    out[4] = cx * cz + sx * sy * sz;
    out[5] = -cz * sx + cx * sy * sz;
    out[6] = -sy;
    out[7] = cy * sx;
    out[8] = cx * cy;
}

// Column-major model matrix: translation * rotation * uniform scale
static void composeModelMatrix(const float rotation[9], float scale,
                               float x, float y, float z, float out[16]) {
    for (int col = 0; col < 3; ++col) {
        out[col * 4 + 0] = scale * rotation[col * 3 + 0];
        out[col * 4 + 1] = scale * rotation[col * 3 + 1];
        out[col * 4 + 2] = scale * rotation[col * 3 + 2];
        out[col * 4 + 3] = 0.0f;
    }
    out[12] = x;
    out[13] = y;
    out[14] = z;
    out[15] = 1.0f;
}

// Custom models: the per-model user rotation turns the mesh to face +Y, then a
// rotation about Z points it where the actor is looking. The facing is negated
// to match the facing indicator cone.
static void buildCustomModelRotation(const ModelRotation& modelRotation, float facing, float out[9]) {
    float userRot[9];
    buildRotationFromEuler(modelRotation.x, modelRotation.y, modelRotation.z, userRot);

    float cosF = std::cos(-facing);
    float sinF = std::sin(-facing);
    float facingRot[9] = {
        cosF,  -sinF, 0.0f,
        sinF,   cosF, 0.0f,
        0.0f,   0.0f, 1.0f
    };

    multiplyRotation3x3(userRot, facingRot, out);
}

struct ActorKind {
    ActorType type = ActorType::Unknown;
    UnitReaction reaction = UnitReaction::Neutral;
};

static ActorKind lookUpActorKind(const ActorColorGenerator& colorGen, std::string_view guid) {
    ActorKind kind;
    if (const ActorType* type = colorGen.getActorType(guid)) {
        kind.type = *type;
    }
    if (const UnitFlags* flags = colorGen.getActorFlags(guid)) {
        kind.reaction = flags->reaction;
    }
    return kind;
}

// Hostile and neutral creatures are coloured by name so every actor with the same name matches
static ActorColor defaultActorColor(const ActorColorGenerator& colorGen, std::string_view guid,
                                    std::string_view actorName, const ActorKind& kind) {
    if (kind.type == ActorType::Creature &&
        (kind.reaction == UnitReaction::Hostile || kind.reaction == UnitReaction::Neutral) &&
        !actorName.empty()) {
        return colorGen.getColorForName(actorName);
    }
    return colorGen.getCachedColor(guid);
}

// Selected actors draw fully opaque; everyone else is very slightly see-through
static float actorAlpha(bool isSelected) {
    return isSelected ? 1.0f : 0.95f;
}

void SceneRenderer3D::renderActors(VkCommandBuffer commandBuffer,
                                    const std::unordered_map<std::string_view, InterpolatedPosition>& actorPositions,
                                    const std::shared_ptr<ActorColorGenerator>& colorGen,
                                    const std::shared_ptr<Camera3D>& camera,
                                    const ActorVisibilitySettings& visibilitySettings,
                                    std::string_view selectedPlayerGuid) {
    // Billboard from the transposed rotation part of the (column-major) view matrix
    float viewMatrix[16];
    camera->getViewMatrix(viewMatrix);

    float camRight[3] = { viewMatrix[0], viewMatrix[4], viewMatrix[8] };
    float camUp[3] = { viewMatrix[1], viewMatrix[5], viewMatrix[9] };
    float camFwd[3] = { viewMatrix[2], viewMatrix[6], viewMatrix[10] };

    // Meshes face +Y, so a 90 degree turn about X is folded in:
    // the columns become [right, -forward, up]
    float billboardRot[9] = {
        camRight[0], camRight[1], camRight[2],     // X axis = camera right
        -camFwd[0],  -camFwd[1],  -camFwd[2],      // Y axis = -camera forward (so +Y faces camera)
        camUp[0],    camUp[1],    camUp[2]         // Z axis = camera up
    };

    for (const auto& [guid, interpPos] : actorPositions) {
        if (!interpPos.hasPosition) {
            continue;
        }

        const auto [actorType, reaction] = lookUpActorKind(*colorGen, guid);
        if (!visibilitySettings.shouldRender(actorType, reaction)) {
            continue;
        }

        std::string_view actorName = colorGen->getActorName(guid);
        ActorColor defaultColor = defaultActorColor(*colorGen, guid, actorName, {actorType, reaction});

        ActorShape shape;
        std::string customShapeName;
        float colorR, colorG, colorB;
        float scale = 1.0f;

        if (appearanceSettings_) {
            uint16_t specId = colorGen->getSpecId(guid);
            ResolvedAppearance appearance = appearanceSettings_->resolveAppearance(
                std::string(guid), std::string(actorName), actorType, reaction, specId,
                defaultColor.r, defaultColor.g, defaultColor.b);
            shape = appearance.shape;
            customShapeName = appearance.customShapeName;
            colorR = appearance.r;
            colorG = appearance.g;
            colorB = appearance.b;
            scale = appearance.scale;
        } else {
            // No appearance settings: players are spheres, everything else cubes
            shape = (actorType == ActorType::Player) ? ActorShape::Sphere : ActorShape::Cube;
            colorR = defaultColor.r;
            colorG = defaultColor.g;
            colorB = defaultColor.b;
        }

        // Play Along clone/ghost draw larger (1.3)
        scale *= interpPos.scaleOverride;

        if (shape == ActorShape::ClassIcon) {
            continue;
        }

        const ShapeBuffer* shapeBufferPtr = nullptr;
        ModelRotation modelRotation;  // Default: no rotation
        if (!customShapeName.empty()) {
            // Textured models are drawn by renderTexturedModelActors
            CustomShapeRegistry* registry = bufferManager_->getCustomShapeRegistry();
            if (registry) {
                const CustomShape* customShape = registry->getShape(customShapeName);
                if (customShape && customShape->hasTexture) {
                    continue;
                }
            }

            shapeBufferPtr = bufferManager_->getCustomShapeBuffer(customShapeName);
            if (!shapeBufferPtr || !shapeBufferPtr->isValid()) {
                // Missing or not yet loaded: draw a sphere instead
                shapeBufferPtr = &bufferManager_->getShapeBuffer(ActorShape::Sphere);
            } else {
                if (registry) {
                    modelRotation = registry->getModelRotation(customShapeName);
                }
            }
        } else {
            shapeBufferPtr = &bufferManager_->getShapeBuffer(shape);
        }

        VkDeviceSize offsets[] = {0};
        VkBuffer vertexBuffers[] = {shapeBufferPtr->vertexBuffer};
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
        vkCmdBindIndexBuffer(commandBuffer, shapeBufferPtr->indexBuffer, 0, VK_INDEX_TYPE_UINT32);

        struct PushConstants {
            float model[16];
            float color[4];
        } pushConstants;

        // Model matrix = Translation * Rotation * Scale (column-major).
        // Spheres are symmetric and skip rotation. Custom models turn to the
        // actor's facing. Built-in shapes billboard to the camera, except ones
        // flagged as oriented (Play Along clone/ghost), which turn to the
        // actor's facing on the ground plane so a cone points where the
        // character is looking.
        bool orientToFacing = interpPos.orientedShape && customShapeName.empty() &&
                              shape != ActorShape::Sphere;

        float modelMatrix[16];
        if (shape == ActorShape::Sphere && customShapeName.empty()) {
            constexpr float identity[9] = {1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f};
            composeModelMatrix(identity, scale, interpPos.x, interpPos.y, 0.0f, modelMatrix);
        } else if (orientToFacing) {
            // Rotate the built-in shape around world +Z (up) so it points along
            // the actor's facing on the ground plane. The cone mesh points its
            // apex at model +Y, which corresponds to facing = +PI/2, so we offset
            // by -PI/2 to align mesh-forward with facing = 0 (+X). This matches
            // the flat facing-indicator convention (facing=0 -> +X, PI/2 -> +Y).
            constexpr float HALF_PI = 1.57079632679489661923f;
            float theta = interpPos.facing - HALF_PI;
            float cosT = std::cos(theta);
            float sinT = std::sin(theta);
            // CCW rotation about +Z: (+X,+Y) map into the world XY plane, leaving
            // the mesh lying flat (its axis stays at z=0).
            const float groundRot[9] = {
                cosT,  sinT, 0.0f,
                -sinT, cosT, 0.0f,
                0.0f,  0.0f, 1.0f
            };
            composeModelMatrix(groundRot, scale, interpPos.x, interpPos.y, 0.0f, modelMatrix);
        } else if (!customShapeName.empty()) {
            float modelRot[9];
            buildCustomModelRotation(modelRotation, interpPos.facing, modelRot);
            composeModelMatrix(modelRot, scale, interpPos.x, interpPos.y, modelRotation.zOffset, modelMatrix);
        } else {
            composeModelMatrix(billboardRot, scale, interpPos.x, interpPos.y, 0.0f, modelMatrix);
        }

        std::memcpy(pushConstants.model, modelMatrix, sizeof(float) * 16);
        pushConstants.color[0] = colorR;
        pushConstants.color[1] = colorG;
        pushConstants.color[2] = colorB;

        bool isSelected = !selectedPlayerGuid.empty() && guid == selectedPlayerGuid;
        pushConstants.color[3] = actorAlpha(isSelected) * interpPos.opacity;

        vkCmdPushConstants(commandBuffer, pipeline_->getPipelineLayout(),
                          VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushConstants), &pushConstants);

        vkCmdDrawIndexed(commandBuffer, shapeBufferPtr->indexCount, 1, 0, 0, 0);
    }
}

bool SceneRenderer3D::createClassIconDescriptorPool(VkDevice device) {
    // One set per spec; there are about 39 specs
    std::array<VkDescriptorPoolSize, 2> poolSizes{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[0].descriptorCount = 50;
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[1].descriptorCount = 50;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = 50;

    if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &classIconDescriptorPool_) != VK_SUCCESS) {
        return false;
    }

    return true;
}

VkDescriptorSet SceneRenderer3D::getOrCreateSpecDescriptorSet(uint16_t specId) {
    auto it = specDescriptorSets_.find(specId);
    if (it != specDescriptorSets_.end()) {
        return it->second;
    }

    if (classIconDescriptorPool_ == VK_NULL_HANDLE || !specIconLoader_) {
        return VK_NULL_HANDLE;
    }

    VkDescriptorSet descriptorSet = allocateTexturedDescriptorSet(
        classIconDescriptorPool_, pipeline_->getClassIconDescriptorSetLayout(),
        specIconLoader_->getSpecIconImageView(specId), specIconLoader_->getSpecIconSampler(specId));
    if (descriptorSet != VK_NULL_HANDLE) {
        specDescriptorSets_[specId] = descriptorSet;
    }
    return descriptorSet;
}

void SceneRenderer3D::renderClassIconActors(VkCommandBuffer commandBuffer,
                                             const std::unordered_map<std::string_view, InterpolatedPosition>& actorPositions,
                                             const std::shared_ptr<ActorColorGenerator>& colorGen,
                                             const std::shared_ptr<Camera3D>& camera,
                                             const ActorVisibilitySettings& visibilitySettings,
                                             std::string_view selectedPlayerGuid) {
    if (!pipeline_ || pipeline_->getClassIconPipeline() == VK_NULL_HANDLE) {
        return;
    }

    if (!specIconLoader_) {
        return;
    }

    struct ClassIconActor {
        std::string_view guid;
        float x, y;
        float scale;
        float r, g, b;
        uint16_t specId;
        bool isSelected;
    };
    std::vector<ClassIconActor> classIconActors;

    for (const auto& [guid, interpPos] : actorPositions) {
        if (!interpPos.hasPosition) {
            continue;
        }

        const auto [actorType, reaction] = lookUpActorKind(*colorGen, guid);
        if (!visibilitySettings.shouldRender(actorType, reaction)) {
            continue;
        }

        std::string_view actorName = colorGen->getActorName(guid);
        ActorColor defaultColor = defaultActorColor(*colorGen, guid, actorName, {actorType, reaction});

        ActorShape shape = ActorShape::Sphere;
        float colorR = defaultColor.r, colorG = defaultColor.g, colorB = defaultColor.b;
        float scale = 1.0f;
        uint16_t specId = 0;

        if (appearanceSettings_) {
            specId = colorGen->getSpecId(guid);
            ResolvedAppearance appearance = appearanceSettings_->resolveAppearance(
                std::string(guid), std::string(actorName), actorType, reaction, specId,
                defaultColor.r, defaultColor.g, defaultColor.b);
            shape = appearance.shape;
            colorR = appearance.r;
            colorG = appearance.g;
            colorB = appearance.b;
            scale = appearance.scale;
        }

        if (shape != ActorShape::ClassIcon) {
            continue;
        }

        scale *= interpPos.scaleOverride;

        classIconActors.push_back({
            guid,
            interpPos.x, interpPos.y,
            scale,
            colorR, colorG, colorB,
            specId,
            !selectedPlayerGuid.empty() && guid == selectedPlayerGuid
        });
    }

    if (classIconActors.empty()) {
        return;
    }

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_->getClassIconPipeline());

    const ShapeBuffer& shapeBuffer = bufferManager_->getShapeBuffer(ActorShape::ClassIcon);
    VkDeviceSize offsets[] = {0};
    VkBuffer vertexBuffers[] = {shapeBuffer.vertexBuffer};
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
    vkCmdBindIndexBuffer(commandBuffer, shapeBuffer.indexBuffer, 0, VK_INDEX_TYPE_UINT32);

    // Billboard: the transposed rotation part of the (column-major) view
    // matrix keeps the disc facing the camera at any angle
    float viewMatrix[16];
    camera->getViewMatrix(viewMatrix);

    float billboardRot[9] = {
        viewMatrix[0], viewMatrix[4], viewMatrix[8],   // Row 0 = camera right
        viewMatrix[1], viewMatrix[5], viewMatrix[9],   // Row 1 = camera up
        viewMatrix[2], viewMatrix[6], viewMatrix[10]   // Row 2 = camera forward
    };

    struct ClassIconPushConstants {
        float model[16];
        float color[4];
        float useTexture;
        float padding[3];
    };

    for (const auto& actor : classIconActors) {
        VkDescriptorSet specDescriptorSet = getOrCreateSpecDescriptorSet(actor.specId);
        if (specDescriptorSet == VK_NULL_HANDLE) {
            continue;
        }

        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                               pipeline_->getClassIconPipelineLayout(), 0, 1, &specDescriptorSet, 0, nullptr);

        ClassIconPushConstants pushConstants;
        composeModelMatrix(billboardRot, actor.scale, actor.x, actor.y, 0.0f, pushConstants.model);
        pushConstants.color[0] = actor.r;
        pushConstants.color[1] = actor.g;
        pushConstants.color[2] = actor.b;
        pushConstants.color[3] = actorAlpha(actor.isSelected);
        pushConstants.useTexture = 1.0f;
        pushConstants.padding[0] = 0.0f;
        pushConstants.padding[1] = 0.0f;
        pushConstants.padding[2] = 0.0f;

        vkCmdPushConstants(commandBuffer, pipeline_->getClassIconPipelineLayout(),
                          VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                          0, sizeof(ClassIconPushConstants), &pushConstants);

        vkCmdDrawIndexed(commandBuffer, shapeBuffer.indexCount, 1, 0, 0, 0);
    }

    // Re-bind sphere pipeline for any subsequent rendering
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_->getPipeline());
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                           pipeline_->getPipelineLayout(), 0, 1, &descriptorSet_, 0, nullptr);
}

void SceneRenderer3D::renderTexturedModelActors(VkCommandBuffer commandBuffer,
                                                 const std::unordered_map<std::string_view, InterpolatedPosition>& actorPositions,
                                                 const std::shared_ptr<ActorColorGenerator>& colorGen,
                                                 [[maybe_unused]] const std::shared_ptr<Camera3D>& camera,
                                                 const ActorVisibilitySettings& visibilitySettings,
                                                 std::string_view selectedPlayerGuid) {
    if (!pipeline_ || pipeline_->getTexturedModelPipeline() == VK_NULL_HANDLE) {
        return;
    }

    CustomShapeRegistry* registry = bufferManager_->getCustomShapeRegistry();
    if (!registry) {
        return;
    }

    struct TexturedModelActor {
        std::string_view guid;
        float x, y;
        float facing;  // Actor facing in radians
        float scale;
        std::string customShapeName;
        ModelRotation modelRotation;
        bool isSelected;
    };
    std::vector<TexturedModelActor> texturedActors;

    for (const auto& [guid, interpPos] : actorPositions) {
        if (!interpPos.hasPosition) {
            continue;
        }

        const auto [actorType, reaction] = lookUpActorKind(*colorGen, guid);
        if (!visibilitySettings.shouldRender(actorType, reaction)) {
            continue;
        }

        std::string customShapeName;
        float scale = 1.0f;

        if (appearanceSettings_) {
            std::string_view actorName = colorGen->getActorName(guid);
            uint16_t specId = colorGen->getSpecId(guid);
            ActorColor defaultColor = colorGen->getCachedColor(guid);
            ResolvedAppearance appearance = appearanceSettings_->resolveAppearance(
                std::string(guid), std::string(actorName), actorType, reaction, specId,
                defaultColor.r, defaultColor.g, defaultColor.b);

            if (appearance.customShapeName.empty()) {
                continue;
            }

            customShapeName = appearance.customShapeName;
            scale = appearance.scale;
        } else {
            continue;  // No custom shapes without appearance settings
        }

        const CustomShape* shape = registry->getShape(customShapeName);
        if (!shape || !shape->hasTexture) {
            continue;  // Not a textured model - handled by regular actor rendering
        }

        scale *= interpPos.scaleOverride;

        ModelRotation modelRotation = registry->getModelRotation(customShapeName);

        texturedActors.push_back({
            guid,
            interpPos.x, interpPos.y,
            interpPos.facing,
            scale,
            customShapeName,
            modelRotation,
            !selectedPlayerGuid.empty() && guid == selectedPlayerGuid
        });
    }

    if (texturedActors.empty()) {
        return;
    }

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_->getTexturedModelPipeline());

    struct TexturedModelPushConstants {
        float model[16];
        float color[4];
    };

    for (const auto& actor : texturedActors) {
        const CustomShape* shape = registry->getShape(actor.customShapeName);
        if (!shape || !shape->isValid) {
            continue;
        }

        VkDeviceSize offsets[] = {0};
        VkBuffer vertexBuffers[] = {shape->buffer.vertexBuffer};
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
        vkCmdBindIndexBuffer(commandBuffer, shape->buffer.indexBuffer, 0, VK_INDEX_TYPE_UINT32);

        float modelRot[9];
        buildCustomModelRotation(actor.modelRotation, actor.facing, modelRot);

        TexturedModelPushConstants pushConstants;
        composeModelMatrix(modelRot, actor.scale, actor.x, actor.y, actor.modelRotation.zOffset, pushConstants.model);
        pushConstants.color[0] = 1.0f;
        pushConstants.color[1] = 1.0f;
        pushConstants.color[2] = 1.0f;
        pushConstants.color[3] = actorAlpha(actor.isSelected);

        vkCmdPushConstants(commandBuffer, pipeline_->getTexturedModelPipelineLayout(),
                          VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(TexturedModelPushConstants), &pushConstants);

        // Each material is an index range with its own texture
        if (!shape->materials.empty()) {
            for (size_t matIdx = 0; matIdx < shape->materials.size(); matIdx++) {
                const auto& material = shape->materials[matIdx];
                if (!material.hasTexture) {
                    continue;
                }

                VkDescriptorSet matDescriptorSet = getOrCreateMaterialDescriptorSet(*shape, matIdx);
                if (matDescriptorSet == VK_NULL_HANDLE) {
                    continue;
                }

                vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                       pipeline_->getTexturedModelPipelineLayout(), 0, 1, &matDescriptorSet, 0, nullptr);

                vkCmdDrawIndexed(commandBuffer, material.indexCount, 1, material.startIndex, 0, 0);
            }
        } else {
            // Single-texture model
            VkDescriptorSet textureDescriptorSet = getOrCreateTexturedModelDescriptorSet(*shape);
            if (textureDescriptorSet == VK_NULL_HANDLE) {
                continue;
            }

            vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                   pipeline_->getTexturedModelPipelineLayout(), 0, 1, &textureDescriptorSet, 0, nullptr);

            vkCmdDrawIndexed(commandBuffer, shape->buffer.indexCount, 1, 0, 0, 0);
        }
    }

    // Re-bind sphere pipeline for any subsequent rendering
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_->getPipeline());
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                           pipeline_->getPipelineLayout(), 0, 1, &descriptorSet_, 0, nullptr);
}
