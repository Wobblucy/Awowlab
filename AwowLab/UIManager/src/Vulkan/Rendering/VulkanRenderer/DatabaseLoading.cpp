#include "Vulkan/Rendering/VulkanRenderer.h"
#include "../../Database/EventDatabase.h"
#include "AnnotationManager/include/TriggerEvaluators/TriggerEvaluatorCommon.h"

struct ExtractedData;
struct Annotation;
struct AutoAnnotationTrigger;

void VulkanRenderer::loadEventDatabase(const EventDatabase* db) {
    if (imguiManager) {
        imguiManager->loadEventDatabase(db);
    }
}

void VulkanRenderer::loadSpellCastDatabase(const SpellCastDatabase* db) {
    if (imguiManager) {
        imguiManager->loadSpellCastDatabase(db);
    }
}

bool VulkanRenderer::loadSpellGCDData(const std::string& csvPath) {
    if (imguiManager) {
        return imguiManager->loadSpellGCDData(csvPath);
    }
    return false;
}

void VulkanRenderer::loadResourceDatabase(const ResourceDatabase* db) {
    if (imguiManager) {
        imguiManager->loadResourceDatabase(db);
    }
}

void VulkanRenderer::loadCombatDatabase(const CombatDatabase* db) {
    if (imguiManager) {
        imguiManager->loadCombatDatabase(db);
    }
}

void VulkanRenderer::loadDeathDatabase(const DeathDatabase* db) {
    if (imguiManager) {
        imguiManager->loadDeathDatabase(db);
    }
}

void VulkanRenderer::loadAuraDatabase(const AuraDatabase* db) {
    if (imguiManager) {
        imguiManager->loadAuraDatabase(db);
    }
}

void VulkanRenderer::loadDispelDatabase(const DispelDatabase* db) {
    if (imguiManager) {
        imguiManager->loadDispelDatabase(db);
    }
}

void VulkanRenderer::loadAvoidanceDatabase(const AvoidanceDatabase* db) {
    if (imguiManager) {
        imguiManager->loadAvoidanceDatabase(db);
    }
}

void VulkanRenderer::loadEmoteDatabase(const EmoteDatabase* db) {
    if (imguiManager) {
        imguiManager->loadEmoteDatabase(db);
    }
}

void VulkanRenderer::loadCombatantInfo(const std::vector<CombatantInfoEvent>& combatantInfo) {
    if (imguiManager) {
        imguiManager->loadCombatantInfo(combatantInfo);
    }
}

void VulkanRenderer::generateFallbackColors() {
    if (imguiManager) {
        imguiManager->generateFallbackColors();
    }
}

void VulkanRenderer::loadMapChangeEvents(const std::vector<MapChangeEvent>& mapChanges, uint32_t encounterId) {
    if (imguiManager) {
        imguiManager->loadMapChangeEvents(mapChanges, encounterId);
    }
}

void VulkanRenderer::loadSpellNameMap(const std::unordered_map<uint32_t, std::string>& spellNameMap) {
    if (imguiManager) {
        imguiManager->loadSpellNameMap(spellNameMap);
    }
}

void VulkanRenderer::loadGuidToNameMap(const std::unordered_map<std::string, std::string>& guidToNameMap) {
    if (imguiManager) {
        imguiManager->loadGuidToNameMap(guidToNameMap);
    }
}

void VulkanRenderer::loadActorNpcIdMap(const std::unordered_map<int32_t, uint32_t>& actorIdToNpcId) {
    if (imguiManager) {
        imguiManager->loadActorNpcIdMap(actorIdToNpcId);
    }
}

void VulkanRenderer::loadHealthScalingData(uint64_t avgPlayerHealth,
                                            const std::unordered_map<std::string, uint64_t>& actorMaxHealth) {
    if (imguiManager) {
        imguiManager->loadHealthScalingData(avgPlayerHealth, actorMaxHealth);
    }
}

void VulkanRenderer::setExtractedData(const ExtractedData* data) {
    if (imguiManager) {
        imguiManager->setExtractedData(data);
    }
}

void VulkanRenderer::setExportCallback(std::function<void(
    const ExtractedData*,
    const std::vector<Annotation>&,
    const std::vector<AutoAnnotationTrigger>&,
    const std::vector<CachedAnnotationEvent>&,
    int32_t, int32_t)> callback) {
    if (imguiManager) {
        imguiManager->setExportCallback(std::move(callback));
    }
}

bool VulkanRenderer::shouldReturnToEncounterSelector() const {
    if (imguiManager) {
        return imguiManager->shouldReturnToEncounterSelector();
    }
    return false;
}

bool VulkanRenderer::shouldReturnToLandingPage() const {
    if (imguiManager) {
        return imguiManager->shouldReturnToLandingPage();
    }
    return false;
}

void VulkanRenderer::resetNavigationFlags() {
    if (imguiManager) {
        imguiManager->resetNavigationFlags();
    }
}

bool VulkanRenderer::shouldRefreshEncounters() const {
    if (imguiManager) {
        return imguiManager->shouldRefreshEncounters();
    }
    return false;
}

void VulkanRenderer::resetRefreshFlag() {
    if (imguiManager) {
        imguiManager->resetRefreshFlag();
    }
}

void VulkanRenderer::unloadDatabases() {
    // In-flight frames may still reference the data being unloaded
    if (vulkanDevice && vulkanDevice->getLogicalDevice()) {
        VkDevice device = vulkanDevice->getLogicalDevice();

        vkDeviceWaitIdle(device);

        if (vulkanSync) {
            vulkanSync->resetForNewSession(device);
        }

        currentFrame = 0;
    }

    if (imguiManager) {
        imguiManager->unloadDatabases();
    }
}

AutoAnnotationTriggerManager* VulkanRenderer::getAutoAnnotationTriggerManager() {
    return imguiManager ? imguiManager->getAutoAnnotationTriggerManager() : nullptr;
}
