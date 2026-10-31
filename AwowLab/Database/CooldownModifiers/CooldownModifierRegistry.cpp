#include "CooldownModifierRegistry.h"
#include "Core/ErrorLogger.h"
#include <fstream>
#include <rapidjson/document.h>
#include <rapidjson/istreamwrapper.h>

namespace awow {

    namespace {
        // rapidjson asserts on a Get of the wrong type, so every field is
        // checked before it is read and an entry with a bad one is skipped.
        bool hasUint(const rapidjson::Value& entry, const char* key) {
            return entry.HasMember(key) && entry[key].IsUint();
        }
        bool hasInt(const rapidjson::Value& entry, const char* key) {
            return entry.HasMember(key) && entry[key].IsInt();
        }
    }

    CooldownModifierRegistry& CooldownModifierRegistry::instance() {
        static CooldownModifierRegistry inst;
        return inst;
    }

    bool CooldownModifierRegistry::loadFromJson(const std::string& filepath) {
        std::ifstream file(filepath);
        if (!file.is_open()) {
            AWOW_LOG_INFO("DATA", "CooldownModifierRegistry: could not open " + filepath);
            return false;
        }

        rapidjson::IStreamWrapper isw(file);
        rapidjson::Document doc;
        doc.ParseStream(isw);

        if (doc.HasParseError() || !doc.IsObject()) {
            AWOW_LOG_INFO("DATA", "CooldownModifierRegistry: JSON parse error in " + filepath);
            return false;
        }

        // Callers reload this singleton on every WarcraftLogs import, so start
        // from empty or each talent's reduction is registered again on top.
        clear();

        if (doc.HasMember("resource_modifiers") && doc["resource_modifiers"].IsArray()) {
            const auto& arr = doc["resource_modifiers"];
            for (rapidjson::SizeType i = 0; i < arr.Size(); ++i) {
                const auto& entry = arr[i];

                if (!hasUint(entry, "talent_spell_id") || !hasUint(entry, "modified_spell_id") ||
                    !hasInt(entry, "power_type") || !hasInt(entry, "resource_per_second")) {
                    continue;  // Skip incomplete entries
                }
                // The reduction divides by this, so zero or less is unusable
                if (entry["resource_per_second"].GetInt() <= 0) {
                    continue;
                }

                // Parse WCL talent IDs (if present)
                std::vector<uint32_t> wcl_talent_ids;
                if (entry.HasMember("wcl_talent_ids") && entry["wcl_talent_ids"].IsArray()) {
                    const auto& ids_arr = entry["wcl_talent_ids"];
                    for (rapidjson::SizeType j = 0; j < ids_arr.Size(); ++j) {
                        if (ids_arr[j].IsUint()) {
                            wcl_talent_ids.push_back(ids_arr[j].GetUint());
                        }
                    }
                }

                // Fallback to talent_spell_id if no WCL talent IDs
                if (wcl_talent_ids.empty()) {
                    wcl_talent_ids.push_back(entry["talent_spell_id"].GetUint());
                }

                // Register modifier once for each WCL talent ID
                for (uint32_t wcl_talent_id : wcl_talent_ids) {
                    CooldownModifier modifier;
                    modifier.talent_spell_id = wcl_talent_id;  // Use WCL entry ID for matching
                    modifier.modified_spell_id = entry["modified_spell_id"].GetUint();
                    modifier.modifier_type = ModifierType::ResourceBased;
                    modifier.description = nullptr;

                    modifier.resource.power_type = static_cast<PowerType>(entry["power_type"].GetInt());
                    modifier.resource.resource_per_second = entry["resource_per_second"].GetInt();

                    registerModifier(modifier);
                }
            }
        }

        // Parse charge-based modifiers (talents that grant additional charges)
        if (doc.HasMember("charge_modifiers") && doc["charge_modifiers"].IsArray()) {
            const auto& arr = doc["charge_modifiers"];
            for (rapidjson::SizeType i = 0; i < arr.Size(); ++i) {
                const auto& entry = arr[i];

                if (!hasUint(entry, "talent_spell_id") || !hasUint(entry, "modified_spell_id") ||
                    !hasUint(entry, "additional_charges")) {
                    continue;  // Skip incomplete entries
                }

                // Parse WCL talent IDs (if present)
                std::vector<uint32_t> wcl_talent_ids;
                if (entry.HasMember("wcl_talent_ids") && entry["wcl_talent_ids"].IsArray()) {
                    const auto& ids_arr = entry["wcl_talent_ids"];
                    for (rapidjson::SizeType j = 0; j < ids_arr.Size(); ++j) {
                        if (ids_arr[j].IsUint()) {
                            wcl_talent_ids.push_back(ids_arr[j].GetUint());
                        }
                    }
                }

                // Fallback to talent_spell_id if no WCL talent IDs
                if (wcl_talent_ids.empty()) {
                    wcl_talent_ids.push_back(entry["talent_spell_id"].GetUint());
                }

                // Register modifier once for each WCL talent ID
                for (uint32_t wcl_talent_id : wcl_talent_ids) {
                    CooldownModifier modifier;
                    modifier.talent_spell_id = wcl_talent_id;  // Use WCL entry ID for matching
                    modifier.modified_spell_id = entry["modified_spell_id"].GetUint();
                    modifier.modifier_type = ModifierType::ChargeBased;
                    modifier.description = nullptr;

                    modifier.charge.additional_charges = static_cast<uint8_t>(entry["additional_charges"].GetUint());

                    registerModifier(modifier);
                }
            }
        }

        // Parse flat cooldown modifiers (type 219 - label-based targeting)
        if (doc.HasMember("flat_modifiers") && doc["flat_modifiers"].IsArray()) {
            const auto& arr = doc["flat_modifiers"];
            for (rapidjson::SizeType i = 0; i < arr.Size(); ++i) {
                const auto& entry = arr[i];

                if (!hasUint(entry, "talent_spell_id") || !hasUint(entry, "modified_spell_id") ||
                    !hasInt(entry, "reduction_ms")) {
                    continue;  // Skip incomplete entries
                }

                // Parse WCL talent IDs (if present)
                std::vector<uint32_t> wcl_talent_ids;
                if (entry.HasMember("wcl_talent_ids") && entry["wcl_talent_ids"].IsArray()) {
                    const auto& ids_arr = entry["wcl_talent_ids"];
                    for (rapidjson::SizeType j = 0; j < ids_arr.Size(); ++j) {
                        if (ids_arr[j].IsUint()) {
                            wcl_talent_ids.push_back(ids_arr[j].GetUint());
                        }
                    }
                }

                // Fallback to talent_spell_id if no WCL talent IDs
                if (wcl_talent_ids.empty()) {
                    wcl_talent_ids.push_back(entry["talent_spell_id"].GetUint());
                }

                // Register modifier once for each WCL talent ID
                for (uint32_t wcl_talent_id : wcl_talent_ids) {
                    CooldownModifier modifier;
                    modifier.talent_spell_id = wcl_talent_id;
                    modifier.modified_spell_id = entry["modified_spell_id"].GetUint();
                    modifier.modifier_type = ModifierType::FlatReduction;
                    modifier.description = nullptr;

                    modifier.flat.reduction_ms = entry["reduction_ms"].GetInt();

                    registerModifier(modifier);
                }
            }
        }

        // percent_modifiers in the JSON are not read yet

        return true;
    }

    const std::vector<CooldownModifier>* CooldownModifierRegistry::getModifiersForTalent(uint32_t talent_spell_id) const {
        auto it = talent_to_modifiers_.find(talent_spell_id);
        if (it != talent_to_modifiers_.end()) {
            return &it->second;
        }
        return nullptr;
    }

    std::vector<uint32_t> CooldownModifierRegistry::getTalentsAffectingSpell(uint32_t spell_id) const {
        std::vector<uint32_t> talents;

        auto range = spell_to_talents_.equal_range(spell_id);
        for (auto it = range.first; it != range.second; ++it) {
            talents.push_back(it->second);
        }

        return talents;
    }

    void CooldownModifierRegistry::clear() {
        talent_to_modifiers_.clear();
        spell_to_talents_.clear();
        all_modifiers_.clear();
    }

    void CooldownModifierRegistry::registerModifier(const CooldownModifier& modifier) {
        talent_to_modifiers_[modifier.talent_spell_id].push_back(modifier);

        spell_to_talents_.insert({modifier.modified_spell_id, modifier.talent_spell_id});

        all_modifiers_.push_back(modifier);
    }

} // namespace awow
