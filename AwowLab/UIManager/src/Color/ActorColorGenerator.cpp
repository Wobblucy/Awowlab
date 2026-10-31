#include "Color/ActorColorGenerator.h"
#include "Core/UnifiedSettings.h"
#include "Parser/SpecializationColors.h"
#include "Parser/WowClass.h"
#include <cmath>
#include <functional>

namespace {

// Class colors are a display setting shared by the app and the overlay. Reading
// it from the settings cache here means every color consumer (meter bars,
// breakdowns, death recap) follows the toggle.
bool classColorsEnabled() {
    return SettingsCache::instance().get().meterClassColors;
}

} // namespace

ActorColor ActorColorGenerator::getColor(std::string_view guid, [[maybe_unused]] ActorType type, [[maybe_unused]] const UnitFlags& flags) {
    // Colors are computed during extraction; type and flags are not used
    return getCachedColor(guid);
}

ActorColor ActorColorGenerator::getCachedColor(std::string_view guid) const {
    // With class colors on, players with a known spec (from COMBATANT_INFO) get
    // their class color. Everyone else, and specs the table doesn't know, use
    // the cached color.
    if (classColorsEnabled() && !specIdCache_.empty()) {
        auto specIt = specIdCache_.find(guid);
        if (specIt != specIdCache_.end() &&
            parser::getClassFromSpecId(specIt->second) != parser::WowClass::Unknown) {
            parser::ClassColor classColor = parser::getColorFromSpecId(specIt->second);
            return ActorColor{classColor.rf(), classColor.gf(), classColor.bf()};
        }
    }

    auto it = colorCache_.find(guid);
    if (it != colorCache_.end()) {
        return it->second;
    }
    return ActorColor{0.5f, 0.5f, 0.5f};
}

bool ActorColorGenerator::hasColor(std::string_view guid) const {
    return colorCache_.find(guid) != colorCache_.end();
}

void ActorColorGenerator::clear() {
    colorCache_.clear();
    actorTypes_.clear();
    actorFlags_.clear();
    specIdCache_.clear();
    nameCache_.clear();
    nonFriendlyNameToGuids_.clear();
}

void ActorColorGenerator::cacheActorInfo(std::string_view guid, ActorType type, const UnitFlags& flags) {
    actorTypes_[guid] = type;
    actorFlags_[guid] = flags;
}

void ActorColorGenerator::cacheColor(std::string_view guid, const ActorColor& color) {
    colorCache_[guid] = color;
}

const ActorType* ActorColorGenerator::getActorType(std::string_view guid) const {
    auto it = actorTypes_.find(guid);
    if (it != actorTypes_.end()) {
        return &it->second;
    }
    return nullptr;
}

const UnitFlags* ActorColorGenerator::getActorFlags(std::string_view guid) const {
    auto it = actorFlags_.find(guid);
    if (it != actorFlags_.end()) {
        return &it->second;
    }
    return nullptr;
}

void ActorColorGenerator::cacheSpecId(std::string_view guid, uint16_t specId) {
    if (specId > 0) {
        specIdCache_[guid] = specId;
    }
}

uint16_t ActorColorGenerator::getSpecId(std::string_view guid) const {
    auto it = specIdCache_.find(guid);
    if (it != specIdCache_.end()) {
        return it->second;
    }
    return 0;
}

void ActorColorGenerator::cacheActorName(std::string_view guid, std::string_view name) {
    if (!name.empty()) {
        nameCache_[guid] = name;
    }
}

std::string_view ActorColorGenerator::getActorName(std::string_view guid) const {
    auto it = nameCache_.find(guid);
    if (it != nameCache_.end()) {
        return it->second;
    }
    return std::string_view{};
}

void ActorColorGenerator::cacheNonFriendlyActor(std::string_view guid, std::string_view name, const UnitFlags& flags) {
    if (!flags.isHostile() && !flags.isNeutral()) {
        return;
    }

    auto& guids = nonFriendlyNameToGuids_[name];
    for (const auto& existingGuid : guids) {
        if (existingGuid == guid) {
            return;
        }
    }
    guids.push_back(guid);
}

const std::vector<std::string_view>* ActorColorGenerator::getGuidsForName(std::string_view name) const {
    auto it = nonFriendlyNameToGuids_.find(name);
    if (it != nonFriendlyNameToGuids_.end()) {
        return &it->second;
    }
    return nullptr;
}

ActorColor ActorColorGenerator::getColorForName(std::string_view name) const {
    size_t hash = std::hash<std::string_view>{}(name);

    // Stable color per name, kept in the red-to-yellow range used for enemies
    float hue = static_cast<float>(hash % 60) / 360.0f;  // 0-60 degrees
    float sat = 0.6f + static_cast<float>((hash >> 8) % 30) / 100.0f;  // 60-90%
    float val = 0.7f + static_cast<float>((hash >> 16) % 20) / 100.0f; // 70-90%

    float c = val * sat;
    float x = c * (1.0f - std::fabs(std::fmod(hue * 6.0f, 2.0f) - 1.0f));
    float m = val - c;

    float r, g, b;
    int sector = static_cast<int>(hue * 6.0f);
    switch (sector) {
        case 0: r = c; g = x; b = 0; break;
        case 1: r = x; g = c; b = 0; break;
        case 2: r = 0; g = c; b = x; break;
        case 3: r = 0; g = x; b = c; break;
        case 4: r = x; g = 0; b = c; break;
        default: r = c; g = 0; b = x; break;
    }

    return ActorColor{r + m, g + m, b + m};
}
