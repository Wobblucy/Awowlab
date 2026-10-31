#include "UI/SpecIconLoader.h"
#include "Core/ErrorLogger.h"
#include "Core/Utils/ExecutablePath.h"

SpecIconLoader::SpecIconLoader()
    : textureManager_(nullptr)
    , textureCache_(nullptr) {
}

SpecIconLoader::~SpecIconLoader() {
    cleanup();
}

void SpecIconLoader::initializeSpecMappings() {
    // Death Knight
    specToFilename_[250] = "spell_deathknight_bloodpresence.png";      // Blood
    specToFilename_[251] = "spell_deathknight_frostpresence.png";      // Frost
    specToFilename_[252] = "spell_deathknight_unholypresence.png";     // Unholy

    // Demon Hunter
    specToFilename_[577] = "ability_demonhunter_specdps.png";          // Havoc
    specToFilename_[581] = "ability_demonhunter_spectank.png";         // Vengeance
    specToFilename_[1480] = "classicon_demonhunter_void.png";          // Devourer

    // Druid
    specToFilename_[102] = "spell_nature_starfall.png";                // Balance
    specToFilename_[103] = "ability_druid_catform.png";                // Feral
    specToFilename_[104] = "ability_racial_bearform.png";              // Guardian
    specToFilename_[105] = "spell_nature_healingtouch.png";            // Restoration

    // Evoker
    specToFilename_[1467] = "classicon_evoker_devastation.png";        // Devastation
    specToFilename_[1468] = "classicon_evoker_preservation.png";       // Preservation
    specToFilename_[1473] = "classicon_evoker_augmentation.png";       // Augmentation

    // Hunter
    specToFilename_[253] = "ability_hunter_bestialdiscipline.png";     // Beast Mastery
    specToFilename_[254] = "ability_hunter_focusedaim.png";            // Marksmanship
    specToFilename_[255] = "ability_hunter_camouflage.png";            // Survival

    // Mage
    specToFilename_[62] = "spell_holy_magicalsentry.png";              // Arcane
    specToFilename_[63] = "spell_fire_firebolt02.png";                 // Fire
    specToFilename_[64] = "spell_frost_frostbolt02.png";               // Frost

    // Monk
    specToFilename_[268] = "monk_stance_drunkenox.png";                // Brewmaster
    specToFilename_[269] = "monk_stance_whitetiger.png";               // Windwalker
    specToFilename_[270] = "monk_stance_wiseserpent.png";              // Mistweaver

    // Paladin
    specToFilename_[65] = "spell_holy_holybolt.png";                   // Holy
    specToFilename_[66] = "ability_paladin_shieldofthetemplar.png";    // Protection
    specToFilename_[70] = "spell_holy_auraoflight.png";                // Retribution

    // Priest
    specToFilename_[256] = "spell_holy_powerwordshield.png";           // Discipline
    specToFilename_[257] = "spell_holy_guardianspirit.png";            // Holy
    specToFilename_[258] = "spell_shadow_shadowwordpain.png";          // Shadow

    // Rogue
    specToFilename_[259] = "ability_rogue_eviscerate.png";             // Assassination
    specToFilename_[260] = "ability_rogue_waylay.png";                 // Outlaw
    specToFilename_[261] = "ability_stealth.png";                      // Subtlety

    // Shaman
    specToFilename_[262] = "spell_nature_lightning.png";               // Elemental
    specToFilename_[263] = "spell_nature_lightningshield.png";         // Enhancement
    specToFilename_[264] = "spell_nature_magicimmunity.png";           // Restoration

    // Warlock
    specToFilename_[265] = "spell_shadow_deathcoil.png";               // Affliction
    specToFilename_[266] = "spell_shadow_metamorphosis.png";           // Demonology
    specToFilename_[267] = "spell_shadow_rainoffire.png";              // Destruction

    // Warrior
    specToFilename_[71] = "ability_warrior_savageblow.png";            // Arms
    specToFilename_[72] = "ability_warrior_innerrage.png";             // Fury
    specToFilename_[73] = "ability_warrior_defensivestance.png";       // Protection
}

bool SpecIconLoader::initialize(VkDevice device, VkPhysicalDevice physicalDevice,
                                 VkQueue graphicsQueue, VkCommandPool commandPool,
                                 VkDescriptorPool descriptorPool) {
    initializeSpecMappings();

    textureManager_ = std::make_unique<VulkanTextureManager>();
    if (!textureManager_->initialize(device, physicalDevice, graphicsQueue,
                                      commandPool, descriptorPool)) {
        AWOW_LOG_ERROR("TEXTURE", "Failed to initialize spec icon texture manager");
        return false;
    }

    textureCache_ = std::make_unique<LRUCache<uint16_t, TextureHandle>>(
        MAX_CACHED_TEXTURES,
        [this](const uint16_t& /*specId*/, TextureHandle& texture) {
            if (textureManager_) {
                textureManager_->destroyTexture(texture);
            }
        });

    // Set path to external spell icon files (reuse same folder)
    iconBasePath_ = awow::getExecutableDirectory() + "PNG\\Spell Icons\\";

    // Create a default "unknown spec" texture (solid gray square)
    auto defaultOpt = textureManager_->createSolidColorTexture(64, 64, 100, 100, 100, 255,
                                                                "SpecIcon_Default");
    if (!defaultOpt.has_value()) {
        AWOW_LOG_ERROR("TEXTURE", "Failed to create default spec icon texture");
        return false;
    }
    defaultTexture_ = defaultOpt.value();

    return true;
}

void SpecIconLoader::cleanup() {
    // Cleanup all cached textures (LRU cache calls destroyTexture via eviction callback)
    if (textureCache_) {
        textureCache_->clear();
        textureCache_.reset();
    }

    // Cleanup default texture
    if (textureManager_ && defaultTexture_.isValid()) {
        textureManager_->destroyTexture(defaultTexture_);
    }

    // Flush any pending texture deletions before resetting the manager
    if (textureManager_) {
        textureManager_->flushDeletionQueue(true);
    }

    textureManager_.reset();
    specToFilename_.clear();
}

void SpecIconLoader::processTextureCleanup() {
    if (textureManager_) {
        textureManager_->processDeletionQueue();
    }
}

TextureHandle SpecIconLoader::getSpecIcon(uint16_t specId) {
    if (!textureManager_ || !textureCache_) {
        return defaultTexture_;
    }

    // Check if already loaded (updates LRU order)
    TextureHandle* cached = textureCache_->get(specId);
    if (cached) {
        return *cached;
    }

    auto mappingIt = specToFilename_.find(specId);
    if (mappingIt == specToFilename_.end()) {
        return defaultTexture_;
    }

    std::string filepath = iconBasePath_ + mappingIt->second;
    std::string debugName = "SpecIcon_" + std::to_string(specId);
    auto textureOpt = textureManager_->loadTextureFromFile(filepath, debugName.c_str());

    if (!textureOpt.has_value()) {
        return defaultTexture_;
    }

    TextureHandle result = textureOpt.value();
    textureCache_->put(specId, std::move(textureOpt.value()));

    return result;
}

ImTextureID SpecIconLoader::getSpecIconForImGui(uint16_t specId) {
    TextureHandle handle = getSpecIcon(specId);
    return VulkanTextureManager::getImGuiTextureID(handle);
}

TextureHandle SpecIconLoader::getDefaultIcon() {
    return defaultTexture_;
}

ImTextureID SpecIconLoader::getDefaultIconForImGui() {
    return VulkanTextureManager::getImGuiTextureID(defaultTexture_);
}

VkImageView SpecIconLoader::getSpecIconImageView(uint16_t specId) {
    TextureHandle handle = getSpecIcon(specId);
    return handle.imageView;
}

VkSampler SpecIconLoader::getSpecIconSampler(uint16_t specId) {
    TextureHandle handle = getSpecIcon(specId);
    return handle.sampler;
}

bool SpecIconLoader::hasIconMapping(uint16_t specId) const {
    return specToFilename_.find(specId) != specToFilename_.end();
}

std::string SpecIconLoader::getSpecIconFilename(uint16_t specId) const {
    auto it = specToFilename_.find(specId);
    if (it != specToFilename_.end()) {
        return it->second;
    }
    return "[No mapping]";
}
