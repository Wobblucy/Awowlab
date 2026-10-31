#include "UI/SpellAssignmentSettings.h"
#include "Core/UnifiedSettings.h"
#include "Core/LocalizationManager.h"
#include <rapidjson/document.h>
#include <rapidjson/writer.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/error/en.h>
#include <algorithm>
#include <charconv>
#include <cstring>
#include "Core/ErrorLogger.h"

namespace spell_assignment {

namespace {

// Spec and spell ids are stored as JSON object keys. A key that is not a
// number (hand-edited or damaged settings) skips that one entry.
bool parseIdKey(const char* text, uint32_t& out) {
    const char* end = text + std::strlen(text);
    auto [ptr, ec] = std::from_chars(text, end, out);
    return ec == std::errc() && ptr != text;
}

// Version 5 added the player buff/debuff exclusions
constexpr int kJsonVersion = 5;

// Calls fn(specId, value) for each member of doc[section] keyed by a valid,
// non-zero spec id. A missing or mistyped section is skipped.
template <typename Fn>
void forEachSpecEntry(const rapidjson::Value& doc, const char* section, Fn&& fn) {
    if (!doc.HasMember(section) || !doc[section].IsObject()) return;
    const auto& sectionObj = doc[section];
    for (auto it = sectionObj.MemberBegin(); it != sectionObj.MemberEnd(); ++it) {
        uint32_t specKey = 0;
        if (!parseIdKey(it->name.GetString(), specKey)) continue;
        const uint16_t specId = static_cast<uint16_t>(specKey);
        if (specId == 0) continue;
        fn(specId, it->value);
    }
}

// Exclusions are stored per spec as a plain array of spell ids
void readSpellIdArray(const rapidjson::Value& array, std::unordered_set<uint32_t>& out) {
    for (const auto& spellIdVal : array.GetArray()) {
        if (spellIdVal.IsUint()) {
            out.insert(spellIdVal.GetUint());
        }
    }
}

void writeSpellIdArray(rapidjson::Value& sectionObj, const std::string& specKey,
                       const std::unordered_set<uint32_t>& spellIds,
                       rapidjson::Document::AllocatorType& alloc) {
    if (spellIds.empty()) return;
    rapidjson::Value array(rapidjson::kArrayType);
    for (uint32_t spellId : spellIds) {
        array.PushBack(spellId, alloc);
    }
    rapidjson::Value key(specKey.c_str(), alloc);
    sectionObj.AddMember(key, array, alloc);
}

} // namespace


const char* getLocationName(SpellDisplayLocation location) {
    switch (location) {
        case SpellDisplayLocation::CDGrouping1:        return L("location.cd_group_1");
        case SpellDisplayLocation::RaidFrameAura:      return L("location.raid_frame");
        case SpellDisplayLocation::PlayerHUDBigCD:     return L("location.hud_major_cd");
        case SpellDisplayLocation::GCDTracker:         return L("location.gcd_tracker");
        case SpellDisplayLocation::Nameplate:          return L("location.nameplate");
        case SpellDisplayLocation::PlayerBuffs:        return L("location.player_buffs");
        case SpellDisplayLocation::PlayerDebuffs:      return L("location.player_debuffs");
        default:                                       return L("location.none");
    }
}

// Get short column header for location (for compact table display)
const char* getLocationShortName(SpellDisplayLocation location) {
    switch (location) {
        case SpellDisplayLocation::CDGrouping1:        return L("location.short.cd1");
        case SpellDisplayLocation::RaidFrameAura:      return L("location.short.raid");
        case SpellDisplayLocation::PlayerHUDBigCD:     return L("location.short.hud_cd");
        case SpellDisplayLocation::GCDTracker:         return L("location.short.gcd");
        case SpellDisplayLocation::Nameplate:          return L("location.short.plate");
        case SpellDisplayLocation::PlayerBuffs:        return L("location.short.buffs");
        case SpellDisplayLocation::PlayerDebuffs:      return L("location.short.debuffs");
        default:                                       return L("location.none");
    }
}


void SpecSpellAssignments::setSpellLocations(uint32_t spell_id, uint16_t locationMask) {
    if (locationMask == 0) {
        spellAssignments.erase(spell_id);
    } else {
        auto& assignment = spellAssignments[spell_id];
        assignment.spell_id = spell_id;
        assignment.locations = locationMask;
    }
}

uint16_t SpecSpellAssignments::getSpellLocations(uint32_t spell_id) const {
    auto it = spellAssignments.find(spell_id);
    if (it != spellAssignments.end()) {
        return it->second.locations;
    }
    return 0;
}

bool SpecSpellAssignments::hasSpellAssignment(uint32_t spell_id) const {
    return spellAssignments.find(spell_id) != spellAssignments.end();
}

void SpecSpellAssignments::removeSpellAssignment(uint32_t spell_id) {
    spellAssignments.erase(spell_id);
}

std::vector<uint32_t> SpecSpellAssignments::getSpellsForLocation(SpellDisplayLocation loc) const {
    std::vector<uint32_t> result;
    for (const auto& [spellId, assignment] : spellAssignments) {
        if (assignment.hasLocation(loc)) {
            result.push_back(spellId);
        }
    }
    return result;
}

void SpecSpellAssignments::setGCDExcluded(uint32_t spell_id, bool excluded) {
    if (excluded) {
        gcdOnlyExclusions.insert(spell_id);
    } else {
        gcdOnlyExclusions.erase(spell_id);
    }
}

bool SpecSpellAssignments::isGCDExcluded(uint32_t spell_id) const {
    // Excluded if in gcdOnlyExclusions OR has assignment with GCD bit OFF
    if (gcdOnlyExclusions.count(spell_id) > 0) {
        return true;
    }
    auto it = spellAssignments.find(spell_id);
    if (it != spellAssignments.end() && it->second.locations != 0) {
        return !hasFlag(it->second.locations, SpellDisplayLocation::GCDTracker);
    }
    return false;
}

// ============== SpecSpellAssignments - Aura Methods ==============

void SpecSpellAssignments::setAuraLocations(uint32_t spell_id, uint16_t locationMask, bool is_buff) {
    if (locationMask == 0) {
        auraAssignments.erase(spell_id);
    } else {
        auto& assignment = auraAssignments[spell_id];
        assignment.spell_id = spell_id;
        assignment.locations = locationMask;
        assignment.is_buff = is_buff;
    }
}

uint16_t SpecSpellAssignments::getAuraLocations(uint32_t spell_id) const {
    auto it = auraAssignments.find(spell_id);
    if (it != auraAssignments.end()) {
        return it->second.locations;
    }
    return 0;
}

bool SpecSpellAssignments::hasAuraAssignment(uint32_t spell_id) const {
    return auraAssignments.find(spell_id) != auraAssignments.end();
}

void SpecSpellAssignments::removeAuraAssignment(uint32_t spell_id) {
    auraAssignments.erase(spell_id);
}

std::vector<uint32_t> SpecSpellAssignments::getAurasForLocation(SpellDisplayLocation loc) const {
    std::vector<uint32_t> result;
    for (const auto& [spellId, assignment] : auraAssignments) {
        if (assignment.hasLocation(loc)) {
            result.push_back(spellId);
        }
    }
    return result;
}


SpellAssignmentSettings::SpellAssignmentSettings() {
    loadFromSettings();
}

SpellAssignmentSettings& SpellAssignmentSettings::instance() {
    static SpellAssignmentSettings instance;
    return instance;
}

SpecSpellAssignments& SpellAssignmentSettings::getOrCreateSpecAssignments(uint16_t specId) {
    auto it = specAssignments_.find(specId);
    if (it == specAssignments_.end()) {
        SpecSpellAssignments newAssignments;
        newAssignments.specId = specId;
        auto [inserted_it, _] = specAssignments_.emplace(specId, std::move(newAssignments));
        return inserted_it->second;
    }
    return it->second;
}

const SpecSpellAssignments* SpellAssignmentSettings::getSpecAssignments(uint16_t specId) const {
    auto it = specAssignments_.find(specId);
    if (it != specAssignments_.end()) {
        return &it->second;
    }
    return nullptr;
}

uint16_t SpellAssignmentSettings::getLocationsForSpell(uint16_t specId, uint32_t spell_id) const {
    const auto* assignments = getSpecAssignments(specId);
    if (assignments) {
        return assignments->getSpellLocations(spell_id);
    }
    return 0;
}

bool SpellAssignmentSettings::shouldShowInLocation(uint16_t specId, uint32_t spell_id, SpellDisplayLocation loc) const {
    uint16_t locations = getLocationsForSpell(specId, spell_id);
    return hasFlag(locations, loc);
}

std::vector<uint32_t> SpellAssignmentSettings::getSpellsForLocation(uint16_t specId, SpellDisplayLocation loc) const {
    const auto* assignments = getSpecAssignments(specId);
    if (assignments) {
        return assignments->getSpellsForLocation(loc);
    }
    return {};
}

void SpellAssignmentSettings::setSpellLocations(uint16_t specId, uint32_t spell_id, uint16_t locationMask) {
    auto& assignments = getOrCreateSpecAssignments(specId);
    assignments.setSpellLocations(spell_id, locationMask);
    // If spell now has a regular assignment, remove from GCD-only exclusions
    if (locationMask != 0) {
        assignments.gcdOnlyExclusions.erase(spell_id);
    }
    usingDefaults_ = false;
    invalidateExclusionCache();
}

void SpellAssignmentSettings::setSpellGCDExcluded(uint16_t specId, uint32_t spell_id, bool excluded) {
    auto& assignments = getOrCreateSpecAssignments(specId);

    auto it = assignments.spellAssignments.find(spell_id);
    bool hasRegularAssignment = (it != assignments.spellAssignments.end() && it->second.locations != 0);

    if (hasRegularAssignment) {
        // Modify the existing assignment's GCD bit
        if (excluded) {
            it->second.locations &= ~static_cast<uint16_t>(SpellDisplayLocation::GCDTracker);
        } else {
            it->second.locations |= static_cast<uint16_t>(SpellDisplayLocation::GCDTracker);
        }
        // Remove from GCD-only exclusions since we have a regular assignment
        assignments.gcdOnlyExclusions.erase(spell_id);
    } else {
        // No regular assignment - use GCD-only exclusion tracking
        assignments.setGCDExcluded(spell_id, excluded);
    }

    usingDefaults_ = false;
    invalidateExclusionCache();
}

bool SpellAssignmentSettings::isSpellGCDExcluded(uint16_t specId, uint32_t spell_id) const {
    const auto* assignments = getSpecAssignments(specId);
    if (!assignments) {
        return false;  // No assignments for this spec, not excluded
    }
    return assignments->isGCDExcluded(spell_id);
}

void SpellAssignmentSettings::clearAllForSpec(uint16_t specId) {
    specAssignments_.erase(specId);
    invalidateExclusionCache();
    invalidateNameplateCache();
}

// ============== SpellAssignmentSettings - Aura Methods ==============

uint16_t SpellAssignmentSettings::getLocationsForAura(uint16_t specId, uint32_t spell_id) const {
    const auto* assignments = getSpecAssignments(specId);
    if (assignments) {
        return assignments->getAuraLocations(spell_id);
    }
    return 0;
}

bool SpellAssignmentSettings::shouldShowAuraInLocation(uint16_t specId, uint32_t spell_id, SpellDisplayLocation loc) const {
    uint16_t locations = getLocationsForAura(specId, spell_id);
    return hasFlag(locations, loc);
}

std::vector<uint32_t> SpellAssignmentSettings::getAurasForLocation(uint16_t specId, SpellDisplayLocation loc) const {
    const auto* assignments = getSpecAssignments(specId);
    if (assignments) {
        return assignments->getAurasForLocation(loc);
    }
    return {};
}

void SpellAssignmentSettings::setAuraLocations(uint16_t specId, uint32_t spell_id, uint16_t locationMask, bool is_buff) {
    getOrCreateSpecAssignments(specId).setAuraLocations(spell_id, locationMask, is_buff);
    usingDefaults_ = false;
    invalidateNameplateCache();
}

void SpellAssignmentSettings::clearAuraAssignment(uint16_t specId, uint32_t spell_id) {
    auto it = specAssignments_.find(specId);
    if (it != specAssignments_.end()) {
        it->second.removeAuraAssignment(spell_id);
        invalidateNameplateCache();
    }
}

void SpellAssignmentSettings::resetToDefaults(uint16_t specId) {
    // For spec-based, resetting just clears the assignments (no built-in defaults per spec)
    specAssignments_.erase(specId);
    invalidateExclusionCache();
    invalidateNameplateCache();
}

void SpellAssignmentSettings::resetAllToDefaults() {
    specAssignments_.clear();
    usingDefaults_ = true;
    invalidateExclusionCache();
    invalidateNameplateCache();
}

void SpellAssignmentSettings::applyDefaults() {
    // For spec-based, defaults are empty - user configures per spec as they encounter them
    specAssignments_.clear();
    usingDefaults_ = true;
    invalidateExclusionCache();
    invalidateNameplateCache();
}


std::string SpellAssignmentSettings::serializeToJson() const {
    rapidjson::Document doc;
    doc.SetObject();
    auto& alloc = doc.GetAllocator();

    doc.AddMember("version", kJsonVersion, alloc);

    rapidjson::Value specsObj(rapidjson::kObjectType);
    rapidjson::Value aurasObj(rapidjson::kObjectType);
    rapidjson::Value gcdExclusionsObj(rapidjson::kObjectType);
    rapidjson::Value buffExclusionsObj(rapidjson::kObjectType);
    rapidjson::Value debuffExclusionsObj(rapidjson::kObjectType);

    for (const auto& [specId, assignments] : specAssignments_) {
        std::string specIdStr = std::to_string(specId);

        // Serialize spell assignments (cooldowns)
        if (!assignments.spellAssignments.empty()) {
            rapidjson::Value spellsObj(rapidjson::kObjectType);
            for (const auto& [spellId, assignment] : assignments.spellAssignments) {
                std::string spellIdStr = std::to_string(spellId);
                rapidjson::Value key(spellIdStr.c_str(), alloc);
                spellsObj.AddMember(key, assignment.locations, alloc);
            }
            rapidjson::Value specKey(specIdStr.c_str(), alloc);
            specsObj.AddMember(specKey, spellsObj, alloc);
        }

        // Serialize aura assignments (buffs/debuffs)
        if (!assignments.auraAssignments.empty()) {
            rapidjson::Value specAurasObj(rapidjson::kObjectType);
            for (const auto& [spellId, aura] : assignments.auraAssignments) {
                rapidjson::Value auraData(rapidjson::kObjectType);
                auraData.AddMember("loc", aura.locations, alloc);
                auraData.AddMember("buff", aura.is_buff, alloc);

                std::string spellIdStr = std::to_string(spellId);
                rapidjson::Value key(spellIdStr.c_str(), alloc);
                specAurasObj.AddMember(key, auraData, alloc);
            }
            rapidjson::Value auraSpecKey(specIdStr.c_str(), alloc);
            aurasObj.AddMember(auraSpecKey, specAurasObj, alloc);
        }

        writeSpellIdArray(gcdExclusionsObj, specIdStr, assignments.gcdOnlyExclusions, alloc);
        writeSpellIdArray(buffExclusionsObj, specIdStr, assignments.playerBuffExclusions, alloc);
        writeSpellIdArray(debuffExclusionsObj, specIdStr, assignments.playerDebuffExclusions, alloc);
    }

    doc.AddMember("specs", specsObj, alloc);
    doc.AddMember("auras", aurasObj, alloc);
    doc.AddMember("gcdExclusions", gcdExclusionsObj, alloc);
    doc.AddMember("buffExclusions", buffExclusionsObj, alloc);
    doc.AddMember("debuffExclusions", debuffExclusionsObj, alloc);

    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    doc.Accept(writer);

    return buffer.GetString();
}

bool SpellAssignmentSettings::deserializeFromJson(const std::string& json) {
    if (json.empty()) {
        applyDefaults();
        return true;
    }

    rapidjson::Document doc;
    doc.Parse(json.c_str());

    if (doc.HasParseError()) {
        AWOW_LOG_INFO("SETTINGS", std::string("Spell assignment settings JSON parse error: ") +
                                      rapidjson::GetParseError_En(doc.GetParseError()));
        applyDefaults();
        return false;
    }

    specAssignments_.clear();
    usingDefaults_ = false;

    // Check version - version 4+ uses spec IDs, older versions used class names
    int version = 1;
    if (doc.HasMember("version") && doc["version"].IsInt()) {
        version = doc["version"].GetInt();
    }

    if (version >= 4) {
        // New spec-based format
        forEachSpecEntry(doc, "specs", [this](uint16_t specId, const rapidjson::Value& spellsObj) {
            if (!spellsObj.IsObject()) return;
            auto& assignments = getOrCreateSpecAssignments(specId);

            for (auto spellIt = spellsObj.MemberBegin(); spellIt != spellsObj.MemberEnd(); ++spellIt) {
                uint32_t spellId = 0;
                if (!parseIdKey(spellIt->name.GetString(), spellId)) continue;
                if (spellIt->value.IsUint()) {
                    assignments.setSpellLocations(spellId, static_cast<uint16_t>(spellIt->value.GetUint()));
                }
            }
        });

        forEachSpecEntry(doc, "auras", [this](uint16_t specId, const rapidjson::Value& specAurasObj) {
            if (!specAurasObj.IsObject()) return;
            auto& assignments = getOrCreateSpecAssignments(specId);

            for (auto auraIt = specAurasObj.MemberBegin(); auraIt != specAurasObj.MemberEnd(); ++auraIt) {
                uint32_t spellId = 0;
                if (!parseIdKey(auraIt->name.GetString(), spellId)) continue;
                if (!auraIt->value.IsObject()) continue;

                uint16_t locations = 0;
                bool is_buff = true;
                if (auraIt->value.HasMember("loc") && auraIt->value["loc"].IsUint()) {
                    locations = static_cast<uint16_t>(auraIt->value["loc"].GetUint());
                }
                if (auraIt->value.HasMember("buff") && auraIt->value["buff"].IsBool()) {
                    is_buff = auraIt->value["buff"].GetBool();
                }
                assignments.setAuraLocations(spellId, locations, is_buff);
            }
        });

        // Exclusion lists; buff and debuff ones arrived in version 5
        const auto loadExclusions = [this, &doc](const char* section,
                                                  std::unordered_set<uint32_t> SpecSpellAssignments::*set) {
            forEachSpecEntry(doc, section, [this, set](uint16_t specId, const rapidjson::Value& array) {
                if (!array.IsArray()) return;
                readSpellIdArray(array, getOrCreateSpecAssignments(specId).*set);
            });
        };
        loadExclusions("gcdExclusions", &SpecSpellAssignments::gcdOnlyExclusions);
        loadExclusions("buffExclusions", &SpecSpellAssignments::playerBuffExclusions);
        loadExclusions("debuffExclusions", &SpecSpellAssignments::playerDebuffExclusions);
    }

    // Old class-based format (version < 4) is not migrated - users will reconfigure

    return true;
}

void SpellAssignmentSettings::loadFromSettings() {
    const auto& cache = SettingsCache::instance().get();
    if (!cache.spellAssignmentJson.empty()) {
        deserializeFromJson(cache.spellAssignmentJson);
    } else {
        applyDefaults();
    }
}

void SpellAssignmentSettings::saveToSettings() {
    auto& cache = SettingsCache::instance();
    cache.get().spellAssignmentJson = serializeToJson();
    cache.markDirty();
    ++version_;  // Increment version so consumers can detect changes
}


void SpellAssignmentSettings::rebuildGCDExclusionCache() const {
    gcdExclusionCache_.clear();

    // For each spec, find spells that are excluded from GCD tracker
    for (const auto& [specId, assignments] : specAssignments_) {
        std::unordered_set<uint32_t> excluded;

        // Include GCD-only exclusions (spells excluded from GCD with no other assignment)
        for (uint32_t spellId : assignments.gcdOnlyExclusions) {
            excluded.insert(spellId);
        }

        // Include spells with regular assignment but GCDTracker bit OFF
        for (const auto& [spellId, assignment] : assignments.spellAssignments) {
            if (assignment.locations != 0 &&
                !hasFlag(assignment.locations, SpellDisplayLocation::GCDTracker)) {
                excluded.insert(spellId);
            }
        }

        if (!excluded.empty()) {
            gcdExclusionCache_[specId] = std::move(excluded);
        }
    }

    gcdCacheDirty_ = false;
}

bool SpellAssignmentSettings::isSpellExcludedFromGCDTracker(uint16_t specId, uint32_t spell_id) {
    // Rebuild cache if dirty
    if (gcdCacheDirty_) {
        rebuildGCDExclusionCache();
    }

    auto it = gcdExclusionCache_.find(specId);
    if (it == gcdExclusionCache_.end()) {
        return false;  // No exclusions for this spec
    }

    return it->second.count(spell_id) > 0;
}


void SpellAssignmentSettings::rebuildNameplateCache() const {
    nameplateAuraCache_.clear();

    // Aggregate all auras with Nameplate bit set from all specs
    for (const auto& [specId, assignments] : specAssignments_) {
        for (const auto& [spellId, aura] : assignments.auraAssignments) {
            if (hasFlag(aura.locations, SpellDisplayLocation::Nameplate)) {
                nameplateAuraCache_.insert(spellId);
            }
        }
    }

    nameplateCacheDirty_ = false;
}

bool SpellAssignmentSettings::isAuraEnabledForNameplate(uint32_t spell_id) {
    // Rebuild cache if dirty
    if (nameplateCacheDirty_) {
        rebuildNameplateCache();
    }

    // Check if spell is in the inclusion set (default is OFF - not showing)
    return nameplateAuraCache_.count(spell_id) > 0;
}


bool SpellAssignmentSettings::isAuraExcludedFromPlayerBuffs(uint16_t specId, uint32_t spell_id) const {
    const auto* assignments = getSpecAssignments(specId);
    if (!assignments) {
        return false;  // No exclusions for this spec = show by default
    }
    return assignments->playerBuffExclusions.count(spell_id) > 0;
}

bool SpellAssignmentSettings::isAuraExcludedFromPlayerDebuffs(uint16_t specId, uint32_t spell_id) const {
    const auto* assignments = getSpecAssignments(specId);
    if (!assignments) {
        return false;  // No exclusions for this spec = show by default
    }
    return assignments->playerDebuffExclusions.count(spell_id) > 0;
}

void SpellAssignmentSettings::setAuraExcludedFromPlayerBuffs(uint16_t specId, uint32_t spell_id, bool excluded) {
    auto& assignments = getOrCreateSpecAssignments(specId);
    if (excluded) {
        assignments.playerBuffExclusions.insert(spell_id);
    } else {
        assignments.playerBuffExclusions.erase(spell_id);
    }
    usingDefaults_ = false;
}

void SpellAssignmentSettings::setAuraExcludedFromPlayerDebuffs(uint16_t specId, uint32_t spell_id, bool excluded) {
    auto& assignments = getOrCreateSpecAssignments(specId);
    if (excluded) {
        assignments.playerDebuffExclusions.insert(spell_id);
    } else {
        assignments.playerDebuffExclusions.erase(spell_id);
    }
    usingDefaults_ = false;
}

} // namespace spell_assignment
