#include "UI/ActorAppearanceSettings.h"
#include "../../Core/MobWeightSettings.h"  // npcIdFromGuid
#include <algorithm>
#include <filesystem>
#include <cstdio>
#include <unordered_set>

// Static utility: Shape name lookup
const char* ActorAppearanceSettings::getShapeName(ActorShape shape) {
    switch (shape) {
        case ActorShape::Sphere: return "Sphere";
        case ActorShape::Cube: return "Cube";
        case ActorShape::Shield: return "Shield";
        case ActorShape::MedicCross: return "MedicCross";
        case ActorShape::Sword: return "Sword";
        case ActorShape::Bow: return "Bow";
        case ActorShape::Cone: return "Cone";
        case ActorShape::Cylinder: return "Cylinder";
        case ActorShape::Diamond: return "Diamond";
        case ActorShape::Pyramid: return "Pyramid";
        case ActorShape::Star: return "Star";
        case ActorShape::ClassIcon: return "ClassIcon";
        default: return "Sphere";
    }
}

ActorShape ActorAppearanceSettings::getShapeFromName(const std::string& name) {
    if (name == "Cube") return ActorShape::Cube;
    if (name == "Shield") return ActorShape::Shield;
    if (name == "MedicCross") return ActorShape::MedicCross;
    if (name == "Sword") return ActorShape::Sword;
    if (name == "Bow") return ActorShape::Bow;
    if (name == "Cone") return ActorShape::Cone;
    if (name == "Cylinder") return ActorShape::Cylinder;
    if (name == "Diamond") return ActorShape::Diamond;
    if (name == "Pyramid") return ActorShape::Pyramid;
    if (name == "Star") return ActorShape::Star;
    if (name == "ClassIcon") return ActorShape::ClassIcon;
    return ActorShape::Sphere;
}

const char* ActorAppearanceSettings::getRoleName(ActorRole role) {
    switch (role) {
        case ActorRole::Tank: return "Tank";
        case ActorRole::Healer: return "Healer";
        case ActorRole::MeleeDPS: return "MeleeDPS";
        case ActorRole::RangedDPS: return "RangedDPS";
        default: return "Unknown";
    }
}

ActorRole ActorAppearanceSettings::getRoleFromName(const std::string& name) {
    if (name == "Tank") return ActorRole::Tank;
    if (name == "Healer") return ActorRole::Healer;
    if (name == "MeleeDPS") return ActorRole::MeleeDPS;
    if (name == "RangedDPS") return ActorRole::RangedDPS;
    return ActorRole::Unknown;
}

// Role detection from specialization ID
ActorRole ActorAppearanceSettings::getRoleFromSpecId(uint16_t specId) {
    switch (specId) {
        // Tanks
        case 250:  // Blood DK
        case 581:  // Vengeance DH
        case 104:  // Guardian Druid
        case 268:  // Brewmaster Monk
        case 66:   // Protection Paladin
        case 73:   // Protection Warrior
            return ActorRole::Tank;

        // Healers
        case 105:  // Restoration Druid
        case 1468: // Preservation Evoker
        case 270:  // Mistweaver Monk
        case 65:   // Holy Paladin
        case 256:  // Discipline Priest
        case 257:  // Holy Priest
        case 264:  // Restoration Shaman
            return ActorRole::Healer;

        // Melee DPS
        case 251:  // Frost DK
        case 252:  // Unholy DK
        case 577:  // Havoc DH
        case 103:  // Feral Druid
        case 269:  // Windwalker Monk
        case 70:   // Retribution Paladin
        case 259:  // Assassination Rogue
        case 260:  // Outlaw Rogue
        case 261:  // Subtlety Rogue
        case 263:  // Enhancement Shaman
        case 71:   // Arms Warrior
        case 72:   // Fury Warrior
        case 255:  // Survival Hunter
            return ActorRole::MeleeDPS;

        // Ranged DPS
        case 102:  // Balance Druid
        case 1467: // Devastation Evoker
        case 1473: // Augmentation Evoker
        case 253:  // Beast Mastery Hunter
        case 254:  // Marksmanship Hunter
        case 62:   // Arcane Mage
        case 63:   // Fire Mage
        case 64:   // Frost Mage
        case 258:  // Shadow Priest
        case 262:  // Elemental Shaman
        case 265:  // Affliction Warlock
        case 266:  // Demonology Warlock
        case 267:  // Destruction Warlock
        case 1480: // Devourer DH
            return ActorRole::RangedDPS;

        default:
            return ActorRole::Unknown;
    }
}

// Class detection from specialization ID
std::string ActorAppearanceSettings::getClassFromSpecId(uint16_t specId) {
    switch (specId) {
        case 250: case 251: case 252: return "DeathKnight";
        case 577: case 581: case 1480: return "DemonHunter";
        case 102: case 103: case 104: case 105: return "Druid";
        case 1467: case 1468: case 1473: return "Evoker";
        case 253: case 254: case 255: return "Hunter";
        case 62: case 63: case 64: return "Mage";
        case 268: case 269: case 270: return "Monk";
        case 65: case 66: case 70: return "Paladin";
        case 256: case 257: case 258: return "Priest";
        case 259: case 260: case 261: return "Rogue";
        case 262: case 263: case 264: return "Shaman";
        case 265: case 266: case 267: return "Warlock";
        case 71: case 72: case 73: return "Warrior";
        default: return "";
    }
}

// Resolve the default shape based on actor type and role
ActorShape ActorAppearanceSettings::resolveDefaultShape(ActorType actorType, ActorRole role) const {
    // Non-player actors use type-based defaults
    switch (actorType) {
        case ActorType::Creature:
            return roleDefaults_.creatureShape;
        case ActorType::Vehicle:
            return roleDefaults_.vehicleShape;
        case ActorType::Pet:
            return roleDefaults_.petShape;
        case ActorType::Player:
            // Players use role-based shapes if auto-assign is enabled
            if (roleDefaults_.autoAssignRoleShapes) {
                switch (role) {
                    case ActorRole::Tank: return roleDefaults_.tankShape;
                    case ActorRole::Healer: return roleDefaults_.healerShape;
                    case ActorRole::MeleeDPS: return roleDefaults_.meleeDPSShape;
                    case ActorRole::RangedDPS: return roleDefaults_.rangedDPSShape;
                    default: return roleDefaults_.unknownPlayerShape;
                }
            }
            return roleDefaults_.unknownPlayerShape;
        default:
            return ActorShape::Sphere;
    }
}

void ActorAppearanceSettings::applyOverride(ResolvedAppearance& result, const AppearanceOverride& override) const {
    // Custom shape takes precedence over built-in shape
    if (override.customShapeName.has_value() && !override.customShapeName.value().empty()) {
        result.customShapeName = override.customShapeName.value();
    } else if (override.shape.has_value()) {
        result.shape = override.shape.value();
        result.customShapeName.clear();  // Clear custom shape if using built-in
    }
    if (override.r.has_value()) {
        result.r = override.r.value();
    }
    if (override.g.has_value()) {
        result.g = override.g.value();
    }
    if (override.b.has_value()) {
        result.b = override.b.value();
    }
    if (override.scale.has_value()) {
        result.scale = override.scale.value();
    }
    if (override.rotationX.has_value()) {
        result.rotationX = override.rotationX.value();
    }
    if (override.rotationY.has_value()) {
        result.rotationY = override.rotationY.value();
    }
    if (override.rotationZ.has_value()) {
        result.rotationZ = override.rotationZ.value();
    }
}

// Main resolution function - applies hierarchy
// Priority: GUID > NameGroup > Spec > Class > Role > Default
ResolvedAppearance ActorAppearanceSettings::resolveAppearance(
    const std::string& guid,
    const std::string& actorName,
    ActorType actorType,
    UnitReaction reaction,
    uint16_t specId,
    float defaultR, float defaultG, float defaultB
) const {
    ActorRole role = (specId > 0) ? getRoleFromSpecId(specId) : ActorRole::Unknown;

    ResolvedAppearance result;
    result.shape = resolveDefaultShape(actorType, role);
    result.r = defaultR;
    result.g = defaultG;
    result.b = defaultB;
    result.scale = roleDefaults_.defaultScale;

    // Apply health-based scaling for hostile/neutral Creatures
    float healthScale = getHealthBasedScale(guid, actorType, reaction);
    result.scale *= healthScale;

    // Apply role override (lowest priority after defaults)
    auto roleIt = roleOverrides_.find(static_cast<uint8_t>(role));
    if (roleIt != roleOverrides_.end()) {
        applyOverride(result, roleIt->second);
    }

    if (specId > 0) {
        std::string className = getClassFromSpecId(specId);
        if (!className.empty()) {
            auto classIt = classOverrides_.find(className);
            if (classIt != classOverrides_.end()) {
                applyOverride(result, classIt->second);
            }
        }
    }

    if (specId > 0) {
        auto specIt = specOverrides_.find(specId);
        if (specIt != specOverrides_.end()) {
            applyOverride(result, specIt->second);
        }
    }

    // Baked default model, keyed by creature id (lowest priority of the name
    // layer). Not keyed by name: runtime names carry a per-spawn instance
    // suffix ("Venom Leech (12)") and are localized.
    if (!defaultNpcOverrides_.empty()) {
        const uint32_t npcId = npcIdForGuid(guid);
        if (npcId != 0) {
            auto defIt = defaultNpcOverrides_.find(npcId);
            if (defIt != defaultNpcOverrides_.end()) {
                applyOverride(result, defIt->second);
            }
        }
    }

    // Apply name group override (for hostile/neutral actors grouped by name)
    // on top (per-field), so user tweaks win. Validated: a saved override
    // pointing at a model that no longer exists must not shadow the baked
    // default with a dead reference.
    if (!actorName.empty()) {
        auto nameIt = nameGroupOverrides_.find(actorName);
        if (nameIt != nameGroupOverrides_.end()) {
            applyOverrideValidated(result, nameIt->second);
        }
    }

    // Apply GUID override (highest priority)
    auto guidIt = guidOverrides_.find(guid);
    if (guidIt != guidOverrides_.end()) {
        applyOverrideValidated(result, guidIt->second);
    }

    return result;
}

// Health-based scaling methods
void ActorAppearanceSettings::setAveragePlayerHealth(uint64_t avgHealth) {
    avgPlayerMaxHealth_ = avgHealth;
}

void ActorAppearanceSettings::cacheActorMaxHealth(const std::string& guid, uint64_t maxHealth) {
    if (maxHealth > 0) {
        actorMaxHealth_[guid] = maxHealth;
    }
}

void ActorAppearanceSettings::clearHealthData() {
    avgPlayerMaxHealth_ = 0;
    actorMaxHealth_.clear();
}

float ActorAppearanceSettings::getHealthBasedScale(const std::string& guid, ActorType actorType,
                                                    UnitReaction reaction) const {
    // Only apply to hostile/neutral Creatures (not players, pets, vehicles, or friendly NPCs)
    if (actorType != ActorType::Creature) {
        return 1.0f;
    }
    if (reaction == UnitReaction::Friendly) {
        return 1.0f;
    }
    if (avgPlayerMaxHealth_ == 0) {
        return 1.0f;
    }

    auto it = actorMaxHealth_.find(guid);
    if (it == actorMaxHealth_.end() || it->second == 0) {
        return 1.0f;
    }

    double healthRatio = static_cast<double>(it->second) / static_cast<double>(avgPlayerMaxHealth_);

    // log base 20: log₂₀(x) = log(x) / log(20)
    // healthRatio = 1: scale = 1.0 (base)
    // healthRatio = 20: scale = 2.0 (200%)
    // healthRatio = 400: scale = 3.0 (300%)
    // healthRatio = 0.05: scale = 0.0 (allows small mobs to be smaller)
    double logScale = std::log(healthRatio) / std::log(20.0);

    // Final scale: 1.0 + logScale, clamped to [0.0, 5.0]
    float finalScale = static_cast<float>(1.0 + logScale);
    return std::clamp(finalScale, 0.0f, 5.0f);
}

// GUID Override management
void ActorAppearanceSettings::setGuidOverride(const std::string& guid, const AppearanceOverride& override) {
    if (override.hasAnyOverride()) {
        guidOverrides_[guid] = override;
    } else {
        guidOverrides_.erase(guid);
    }
}

void ActorAppearanceSettings::clearGuidOverride(const std::string& guid) {
    guidOverrides_.erase(guid);
}

bool ActorAppearanceSettings::hasGuidOverride(const std::string& guid) const {
    return guidOverrides_.find(guid) != guidOverrides_.end();
}

const AppearanceOverride* ActorAppearanceSettings::getGuidOverride(const std::string& guid) const {
    auto it = guidOverrides_.find(guid);
    return (it != guidOverrides_.end()) ? &it->second : nullptr;
}

// Name Group Override management (for hostile actor grouping by name)
void ActorAppearanceSettings::applyOverrideValidated(ResolvedAppearance& result,
                                                     const AppearanceOverride& override) const {
    const bool hasCustom = override.customShapeName.has_value() && !override.customShapeName->empty();
    if (hasCustom && customShapeValidator_ && !customShapeValidator_(*override.customShapeName)) {
        // Stale model reference: apply the rest of the override (color, scale,
        // rotation) but leave the already-resolved shape/model untouched.
        AppearanceOverride cleaned = override;
        cleaned.customShapeName.reset();
        cleaned.shape.reset();
        applyOverride(result, cleaned);
        return;
    }
    applyOverride(result, override);
}

uint32_t ActorAppearanceSettings::npcIdForGuid(const std::string& guid) const {
    // npcIdFromGuid() reads the 6th dash-separated field. For a real combat log
    // that field IS the creature id. For a WarcraftLogs replay the GUID is
    // synthetic ("Creature-0-0-0-0-<wclActorId>-<spawn>") so the same field is a
    // WCL actor id, which the report's metadata maps to the real creature id.
    const uint32_t idField = MobWeightSettings::npcIdFromGuid(guid);
    if (idField == 0)
        return 0;

    if (!actorIdToNpcId_.empty()) {
        auto it = actorIdToNpcId_.find(static_cast<int32_t>(idField));
        if (it != actorIdToNpcId_.end())
            return it->second;
    }
    return idField;
}

void ActorAppearanceSettings::setDefaultModelAssignments(
    const std::vector<std::pair<uint32_t, std::string>>& assignments) {
    defaultNpcOverrides_.clear();
    defaultNpcOverrides_.reserve(assignments.size());
    for (const auto& [npcId, modelName] : assignments) {
        AppearanceOverride defaultOverride;
        defaultOverride.customShapeName = modelName;  // model only - color/scale stay resolved
        defaultNpcOverrides_.emplace(npcId, std::move(defaultOverride));
    }
}

void ActorAppearanceSettings::setNameGroupOverride(const std::string& name, const AppearanceOverride& override) {
    if (override.hasAnyOverride()) {
        nameGroupOverrides_[name] = override;
    } else {
        nameGroupOverrides_.erase(name);
    }
}

void ActorAppearanceSettings::clearNameGroupOverride(const std::string& name) {
    nameGroupOverrides_.erase(name);
}

bool ActorAppearanceSettings::hasNameGroupOverride(const std::string& name) const {
    return nameGroupOverrides_.find(name) != nameGroupOverrides_.end();
}

const AppearanceOverride* ActorAppearanceSettings::getNameGroupOverride(const std::string& name) const {
    auto it = nameGroupOverrides_.find(name);
    return (it != nameGroupOverrides_.end()) ? &it->second : nullptr;
}

// Spec Override management
void ActorAppearanceSettings::setSpecOverride(uint16_t specId, const AppearanceOverride& override) {
    if (override.hasAnyOverride()) {
        specOverrides_[specId] = override;
    } else {
        specOverrides_.erase(specId);
    }
}

void ActorAppearanceSettings::clearSpecOverride(uint16_t specId) {
    specOverrides_.erase(specId);
}

bool ActorAppearanceSettings::hasSpecOverride(uint16_t specId) const {
    return specOverrides_.find(specId) != specOverrides_.end();
}

const AppearanceOverride* ActorAppearanceSettings::getSpecOverride(uint16_t specId) const {
    auto it = specOverrides_.find(specId);
    return (it != specOverrides_.end()) ? &it->second : nullptr;
}

// Class Override management
void ActorAppearanceSettings::setClassOverride(const std::string& className, const AppearanceOverride& override) {
    if (override.hasAnyOverride()) {
        classOverrides_[className] = override;
    } else {
        classOverrides_.erase(className);
    }
}

void ActorAppearanceSettings::clearClassOverride(const std::string& className) {
    classOverrides_.erase(className);
}

bool ActorAppearanceSettings::hasClassOverride(const std::string& className) const {
    return classOverrides_.find(className) != classOverrides_.end();
}

const AppearanceOverride* ActorAppearanceSettings::getClassOverride(const std::string& className) const {
    auto it = classOverrides_.find(className);
    return (it != classOverrides_.end()) ? &it->second : nullptr;
}

// Role Override management
void ActorAppearanceSettings::setRoleOverride(ActorRole role, const AppearanceOverride& override) {
    uint8_t key = static_cast<uint8_t>(role);
    if (override.hasAnyOverride()) {
        roleOverrides_[key] = override;
    } else {
        roleOverrides_.erase(key);
    }
}

void ActorAppearanceSettings::clearRoleOverride(ActorRole role) {
    roleOverrides_.erase(static_cast<uint8_t>(role));
}

bool ActorAppearanceSettings::hasRoleOverride(ActorRole role) const {
    return roleOverrides_.find(static_cast<uint8_t>(role)) != roleOverrides_.end();
}

const AppearanceOverride* ActorAppearanceSettings::getRoleOverride(ActorRole role) const {
    auto it = roleOverrides_.find(static_cast<uint8_t>(role));
    return (it != roleOverrides_.end()) ? &it->second : nullptr;
}

// Helper to write an override section to stringstream
static void writeOverrideSectionToStream(std::ostringstream& ss, const std::string& sectionName, const AppearanceOverride& override) {
    ss << "[" << sectionName << "]\n";
    if (override.shape.has_value()) {
        ss << "shape=" << ActorAppearanceSettings::getShapeName(*override.shape) << "\n";
    }
    if (override.customShapeName.has_value() && !override.customShapeName->empty()) {
        ss << "customShape=" << *override.customShapeName << "\n";
    }
    if (override.r.has_value() && override.g.has_value() && override.b.has_value()) {
        ss << "color=" << *override.r << "," << *override.g << "," << *override.b << "\n";
    }
    if (override.scale.has_value()) {
        ss << "scale=" << *override.scale << "\n";
    }
    if (override.rotationX.has_value() || override.rotationY.has_value() || override.rotationZ.has_value()) {
        ss << "rotation=" << override.rotationX.value_or(0.0f) << ","
           << override.rotationY.value_or(0.0f) << ","
           << override.rotationZ.value_or(0.0f) << "\n";
    }
    ss << "\n";
}

std::string ActorAppearanceSettings::serializeToString() const {
    std::ostringstream ss;

    ss << "[RoleDefaults]\n";
    ss << "autoAssignRoleShapes=" << (roleDefaults_.autoAssignRoleShapes ? "1" : "0") << "\n";
    ss << "tankShape=" << getShapeName(roleDefaults_.tankShape) << "\n";
    ss << "healerShape=" << getShapeName(roleDefaults_.healerShape) << "\n";
    ss << "meleeDPSShape=" << getShapeName(roleDefaults_.meleeDPSShape) << "\n";
    ss << "rangedDPSShape=" << getShapeName(roleDefaults_.rangedDPSShape) << "\n";
    ss << "unknownPlayerShape=" << getShapeName(roleDefaults_.unknownPlayerShape) << "\n";
    ss << "creatureShape=" << getShapeName(roleDefaults_.creatureShape) << "\n";
    ss << "vehicleShape=" << getShapeName(roleDefaults_.vehicleShape) << "\n";
    ss << "petShape=" << getShapeName(roleDefaults_.petShape) << "\n";
    ss << "defaultScale=" << roleDefaults_.defaultScale << "\n\n";

    for (const auto& [roleKey, override] : roleOverrides_) {
        ActorRole role = static_cast<ActorRole>(roleKey);
        writeOverrideSectionToStream(ss, std::string("RoleOverride:") + getRoleName(role), override);
    }

    for (const auto& [className, override] : classOverrides_) {
        writeOverrideSectionToStream(ss, std::string("ClassOverride:") + className, override);
    }

    for (const auto& [specId, override] : specOverrides_) {
        writeOverrideSectionToStream(ss, std::string("SpecOverride:") + std::to_string(specId), override);
    }

    for (const auto& [guid, override] : guidOverrides_) {
        writeOverrideSectionToStream(ss, std::string("GuidOverride:") + guid, override);
    }

    // Write name group overrides (for hostile actors grouped by name)
    for (const auto& [name, override] : nameGroupOverrides_) {
        writeOverrideSectionToStream(ss, std::string("NameGroupOverride:") + name, override);
    }

    if (!favorites_.empty()) {
        ss << "[Favorites]\n";
        for (const auto& guid : favorites_) {
            ss << "guid=" << guid << "\n";
        }
        ss << "\n";
    }

    return ss.str();
}

bool ActorAppearanceSettings::saveToFile([[maybe_unused]] const std::string& filename) const {
    auto& cache = SettingsCache::instance();
    cache.get().actorAppearanceJson = serializeToString();
    cache.markDirty();
    return true;
}

bool ActorAppearanceSettings::deserializeFromString(const std::string& data) {
    if (data.empty()) {
        return false;
    }

    std::istringstream stream(data);
    std::string line;
    std::string currentSection;
    AppearanceOverride currentOverride;

    auto flushOverride = [&]() {
        if (currentSection.empty()) return;

        if (currentSection.starts_with("RoleOverride:")) {
            std::string roleName = currentSection.substr(13);
            ActorRole role = getRoleFromName(roleName);
            if (role != ActorRole::Unknown || roleName == "Unknown") {
                setRoleOverride(role, currentOverride);
            }
        } else if (currentSection.starts_with("ClassOverride:")) {
            std::string className = currentSection.substr(14);
            setClassOverride(className, currentOverride);
        } else if (currentSection.starts_with("SpecOverride:")) {
            // A hand-edited or damaged id just drops this one override
            unsigned int specId = 0;
            if (sscanf(currentSection.c_str() + 13, "%u", &specId) == 1 && specId <= 0xFFFF) {
                setSpecOverride(static_cast<uint16_t>(specId), currentOverride);
            }
        } else if (currentSection.starts_with("GuidOverride:")) {
            std::string guid = currentSection.substr(13);
            setGuidOverride(guid, currentOverride);
        } else if (currentSection.starts_with("NameGroupOverride:")) {
            std::string name = currentSection.substr(18);
            setNameGroupOverride(name, currentOverride);
        }

        currentOverride.clear();
    };

    while (std::getline(stream, line)) {
        // Skip empty lines
        if (line.empty()) continue;

        // Section header
        if (line[0] == '[' && line.back() == ']') {
            flushOverride();
            currentSection = line.substr(1, line.size() - 2);
            continue;
        }

        // Parse key=value
        size_t eqPos = line.find('=');
        if (eqPos == std::string::npos) continue;

        std::string key = line.substr(0, eqPos);
        std::string value = line.substr(eqPos + 1);

        if (currentSection == "RoleDefaults") {
            if (key == "autoAssignRoleShapes") {
                roleDefaults_.autoAssignRoleShapes = (value == "1");
            } else if (key == "tankShape") {
                roleDefaults_.tankShape = getShapeFromName(value);
            } else if (key == "healerShape") {
                roleDefaults_.healerShape = getShapeFromName(value);
            } else if (key == "meleeDPSShape") {
                roleDefaults_.meleeDPSShape = getShapeFromName(value);
            } else if (key == "rangedDPSShape") {
                roleDefaults_.rangedDPSShape = getShapeFromName(value);
            } else if (key == "unknownPlayerShape") {
                roleDefaults_.unknownPlayerShape = getShapeFromName(value);
            } else if (key == "creatureShape") {
                roleDefaults_.creatureShape = getShapeFromName(value);
            } else if (key == "vehicleShape") {
                roleDefaults_.vehicleShape = getShapeFromName(value);
            } else if (key == "petShape") {
                roleDefaults_.petShape = getShapeFromName(value);
            } else if (key == "defaultScale") {
                float scale;
                if (sscanf(value.c_str(), "%f", &scale) == 1) {
                    roleDefaults_.defaultScale = scale;
                }
            }
        } else if (currentSection == "Favorites") {
            if (key == "guid") {
                addFavorite(value);
            }
        } else {
            // Override section
            if (key == "shape") {
                currentOverride.shape = getShapeFromName(value);
            } else if (key == "customShape") {
                currentOverride.customShapeName = value;
            } else if (key == "color") {
                float r, g, b;
                if (sscanf(value.c_str(), "%f,%f,%f", &r, &g, &b) == 3) {
                    currentOverride.r = r;
                    currentOverride.g = g;
                    currentOverride.b = b;
                }
            } else if (key == "scale") {
                float scale;
                if (sscanf(value.c_str(), "%f", &scale) == 1) {
                    currentOverride.scale = scale;
                }
            } else if (key == "rotation") {
                float rx, ry, rz;
                if (sscanf(value.c_str(), "%f,%f,%f", &rx, &ry, &rz) == 3) {
                    currentOverride.rotationX = rx;
                    currentOverride.rotationY = ry;
                    currentOverride.rotationZ = rz;
                }
            }
        }
    }

    // Flush last override
    flushOverride();

    return true;
}

bool ActorAppearanceSettings::loadFromFile([[maybe_unused]] const std::string& filename) {
    const auto& settings = SettingsCache::instance().get();
    return deserializeFromString(settings.actorAppearanceJson);
}

// Favorites management
void ActorAppearanceSettings::addFavorite(const std::string& guid) {
    favorites_.insert(guid);
}

void ActorAppearanceSettings::removeFavorite(const std::string& guid) {
    favorites_.erase(guid);
}

void ActorAppearanceSettings::toggleFavorite(const std::string& guid) {
    if (favorites_.count(guid) > 0) {
        favorites_.erase(guid);
    } else {
        favorites_.insert(guid);
    }
}

bool ActorAppearanceSettings::isFavorite(const std::string& guid) const {
    return favorites_.count(guid) > 0;
}

void ActorAppearanceSettings::clearFavorites() {
    favorites_.clear();
}
