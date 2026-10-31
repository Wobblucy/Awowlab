#include "UIAutoAnnotationPanel.h"
#include "Core/LocalizationManager.h"
#include "Core/StringInterner.h"
#include "Database/CombatDatabase.h"
#include "Database/SpellCastDatabase.h"
#include "Database/AuraDatabase.h"
#include <imgui.h>
#include <set>
#include <cstdio>

// Format timestamp as M:SS (e.g., "0:15", "1:30", "12:05")
static std::string formatTimestamp(int32_t ms) {
    if (ms < 0) return "";
    int32_t totalSeconds = ms / 1000;
    int32_t minutes = totalSeconds / 60;
    int32_t seconds = totalSeconds % 60;
    char buf[16];
    snprintf(buf, sizeof(buf), "%d:%02d", minutes, seconds);
    return buf;
}

// Find first cast time for a spell_id across all actors
static int32_t getFirstCastTime(const SpellCastDatabase* db, uint32_t spellId) {
    if (!db) return -1;
    int32_t firstTime = INT32_MAX;
    for (const auto& [actorGuid, casts] : db->getAllActorCasts()) {
        for (const auto& cast : casts) {
            if (cast.spell_id == spellId && cast.end_timestamp_ms < firstTime) {
                firstTime = cast.end_timestamp_ms;
            }
        }
    }
    return (firstTime == INT32_MAX) ? -1 : firstTime;
}

// Find first aura application time for a spell_id (debuff or buff)
static int32_t getFirstAuraTime(const AuraDatabase* db, uint32_t spellId) {
    if (!db) return -1;
    int32_t firstTime = INT32_MAX;
    for (const auto& [targetGuid, instances] : db->getAllTargetAuras()) {
        for (const auto& aura : instances) {
            if (aura.spell_id == spellId && aura.applied_at_ms < firstTime) {
                firstTime = aura.applied_at_ms;
            }
        }
    }
    return (firstTime == INT32_MAX) ? -1 : firstTime;
}

// Find first damage event time for a spell_id (from damage taken index)
static int32_t getFirstDamageTakenTime(const CombatDatabase* db, uint32_t spellId) {
    if (!db) return -1;
    int32_t firstTime = INT32_MAX;
    const auto& index = db->getDamageTakenIndex();
    for (const auto& [targetGuidId, records] : index) {
        for (const CombatRecord* rec : records) {
            if (rec->spell_id == spellId && rec->timestamp_ms < firstTime) {
                firstTime = rec->timestamp_ms;
            }
        }
    }
    return (firstTime == INT32_MAX) ? -1 : firstTime;
}

void UIAutoAnnotationPanel::renderCreatureList() {
    ImGui::Text(L("Hostile Creatures"));
    ImGui::Separator();

    if (hostileSpells_.empty()) {
        ImGui::TextDisabled(L("No hostile spells found"));
        return;
    }

    for (const auto& creature : hostileSpells_) {
        // Scopes the "Spells"/"Debuffs"/"Damage" node IDs per creature. Creatures are grouped
        // by name, so creature_guid is empty here.
        ImGui::PushID(creature.creature_name.c_str());

        bool nodeOpen = ImGui::TreeNode(creature.creature_name.c_str());

        if (nodeOpen) {
            if (!creature.spell_ids.empty()) {
                if (ImGui::TreeNode("Spells")) {
                    for (uint32_t spellId : creature.spell_ids) {
                        ImGui::PushID(static_cast<int>(spellId));

                        if (iconLoader_) {
                            if (iconLoader_->renderSpellIcon(spellId, ImVec2(16, 16))) {
                                ImGui::SameLine();
                            }
                        }

                        std::string spellName = (spellId == 1) ? "Melee" : getSpellName(spellId);
                        if (spellName.empty()) spellName = "Unknown";

                        bool isInterruptible = creature.isInterruptible(spellId);

                        int32_t firstTime = getFirstCastTime(spellCastDb_, spellId);
                        std::string displayName;
                        if (isInterruptible) {
                            displayName = "[=] ";  // Cast bar indicator
                        }
                        displayName += spellName;
                        if (firstTime >= 0) {
                            displayName += " @ " + formatTimestamp(firstTime);
                        }

                        bool isSelected = (selectedSpellId_ == spellId &&
                                          selectedCreatureName_ == creature.creature_name &&
                                          selectedCategory_ == SelectionCategory::Cast);

                        if (ImGui::Selectable(displayName.c_str(), isSelected)) {
                            selectedSpellId_ = spellId;
                            selectedCreatureName_ = creature.creature_name;
                            selectedCategory_ = SelectionCategory::Cast;
                            configTiming_ = isInterruptible ? TriggerTiming::OnCastStart : TriggerTiming::OnCastSuccess;
                            configCategory_ = TriggerCategory::Spell;
                        }
                        if (ImGui::IsItemHovered()) {
                            if (isInterruptible) {
                                ImGui::SetTooltip("Spell ID: %u\n[=] Interruptible cast", spellId);
                            } else {
                                ImGui::SetTooltip("Spell ID: %u", spellId);
                            }
                        }

                        ImGui::PopID();
                    }
                    ImGui::TreePop();
                }
            }

            if (!creature.debuff_ids.empty()) {
                if (ImGui::TreeNode("Debuffs")) {
                    for (uint32_t spellId : creature.debuff_ids) {
                        ImGui::PushID(static_cast<int>(spellId) + 100000);  // Offset to avoid collision with Spells

                        if (iconLoader_) {
                            if (iconLoader_->renderSpellIcon(spellId, ImVec2(16, 16))) {
                                ImGui::SameLine();
                            }
                        }

                        std::string spellName = (spellId == 1) ? "Melee" : getSpellName(spellId);
                        if (spellName.empty()) spellName = "Unknown";

                        int32_t firstTime = getFirstAuraTime(auraDb_, spellId);
                        std::string displayName = (firstTime >= 0)
                            ? spellName + " @ " + formatTimestamp(firstTime)
                            : spellName;

                        bool isSelected = (selectedSpellId_ == spellId &&
                                          selectedCreatureName_ == creature.creature_name &&
                                          selectedCategory_ == SelectionCategory::Debuff);

                        if (ImGui::Selectable(displayName.c_str(), isSelected)) {
                            selectedSpellId_ = spellId;
                            selectedCreatureName_ = creature.creature_name;
                            selectedCategory_ = SelectionCategory::Debuff;
                            configTiming_ = TriggerTiming::OnDebuffApplied;
                            configCategory_ = TriggerCategory::Spell;
                        }
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("Spell ID: %u", spellId);
                        }

                        ImGui::PopID();
                    }
                    ImGui::TreePop();
                }
            }

            if (!creature.buff_ids.empty()) {
                if (ImGui::TreeNode(L("Buffs"))) {
                    for (uint32_t spellId : creature.buff_ids) {
                        ImGui::PushID(static_cast<int>(spellId) + 400000);  // Offset to avoid collision with other categories

                        if (iconLoader_) {
                            if (iconLoader_->renderSpellIcon(spellId, ImVec2(16, 16))) {
                                ImGui::SameLine();
                            }
                        }

                        std::string spellName = (spellId == 1) ? "Melee" : getSpellName(spellId);
                        if (spellName.empty()) spellName = "Unknown";

                        int32_t firstTime = getFirstAuraTime(auraDb_, spellId);
                        std::string displayName = (firstTime >= 0)
                            ? spellName + " @ " + formatTimestamp(firstTime)
                            : spellName;

                        bool isSelected = (selectedSpellId_ == spellId &&
                                          selectedCreatureName_ == creature.creature_name &&
                                          selectedCategory_ == SelectionCategory::Buff);

                        if (ImGui::Selectable(displayName.c_str(), isSelected)) {
                            selectedSpellId_ = spellId;
                            selectedCreatureName_ = creature.creature_name;
                            selectedCategory_ = SelectionCategory::Buff;
                            configTiming_ = TriggerTiming::OnBuffApplied;
                            configCategory_ = TriggerCategory::Spell;
                            // Enemy buffs are mostly self-buffs.
                            configAnchor_ = TriggerAnchor::Caster;
                        }
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("Spell ID: %u", spellId);
                        }

                        ImGui::PopID();
                    }
                    ImGui::TreePop();
                }
            }

            if (!creature.damage_ids.empty()) {
                if (ImGui::TreeNode("Damage")) {
                    for (uint32_t spellId : creature.damage_ids) {
                        ImGui::PushID(static_cast<int>(spellId) + 200000);  // Offset to avoid collision with Spells/Debuffs

                        if (iconLoader_) {
                            if (iconLoader_->renderSpellIcon(spellId, ImVec2(16, 16))) {
                                ImGui::SameLine();
                            }
                        }

                        std::string spellName = (spellId == 1) ? "Melee" : getSpellName(spellId);
                        if (spellName.empty()) spellName = "Unknown";

                        int32_t firstTime = getFirstDamageTakenTime(combatDb_, spellId);
                        std::string displayName = (firstTime >= 0)
                            ? spellName + " @ " + formatTimestamp(firstTime)
                            : spellName;

                        bool isSelected = (selectedSpellId_ == spellId &&
                                          selectedCreatureName_ == creature.creature_name &&
                                          selectedCategory_ == SelectionCategory::Damage);

                        if (ImGui::Selectable(displayName.c_str(), isSelected)) {
                            selectedSpellId_ = spellId;
                            selectedCreatureName_ = creature.creature_name;
                            selectedCategory_ = SelectionCategory::Damage;
                            configTiming_ = TriggerTiming::OnDamage;
                            configCategory_ = TriggerCategory::Spell;
                        }
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("Spell ID: %u", spellId);
                        }

                        ImGui::PopID();
                    }
                    ImGui::TreePop();
                }
            }

            if (ImGui::TreeNode(L("Actor"))) {
                struct ActorModeInfo {
                    TriggerTiming timing;
                    const char* label;
                    const char* tooltip;
                };

                const ActorModeInfo actorModes[] = {
                    { TriggerTiming::ActorFollow, "Follow", "Annotation tracks actor position as timeline progresses" },
                    { TriggerTiming::ActorTrail, "Trail", "Leave persistent annotations at intervals (void zone trail)" },
                    { TriggerTiming::ActorSpawn, "Spawn", "Single annotation when actor first appears" },
                    { TriggerTiming::ActorDeath, "Death", "Single annotation when actor dies" }
                };

                for (const auto& mode : actorModes) {
                    ImGui::PushID(static_cast<int>(mode.timing) + 300000);

                    bool isSelected = (selectedCategory_ == SelectionCategory::Actor &&
                                      selectedCreatureName_ == creature.creature_name &&
                                      configTiming_ == mode.timing);

                    if (ImGui::Selectable(L(mode.label), isSelected)) {
                        selectedSpellId_ = 0;
                        selectedCreatureName_ = creature.creature_name;
                        selectedCategory_ = SelectionCategory::Actor;
                        configCategory_ = TriggerCategory::Actor;
                        configTiming_ = mode.timing;
                        configActorNameFilter_ = creature.creature_name;
                    }

                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("%s", mode.tooltip);
                    }

                    ImGui::PopID();
                }

                ImGui::TreePop();
            }

            ImGui::TreePop();
        }

        ImGui::PopID();
    }

    // Spells that hit players, listed by victim. The fallback for abilities with no
    // cast or debuff data.
    if (combatDb_) {
        std::set<uint32_t> damageTakenSpellIds;
        const auto& index = combatDb_->getDamageTakenIndex();
        for (const auto& [targetGuidId, recordPtrs] : index) {
            std::string_view victimGuid = guidInterner().lookup(targetGuidId);
            if (!victimGuid.starts_with("Player-")) continue;
            for (const CombatRecord* rec : recordPtrs) {
                if (rec->spell_id > 1) {  // Skip melee (0 and 1)
                    damageTakenSpellIds.insert(rec->spell_id);
                }
            }
        }

        if (!damageTakenSpellIds.empty()) {
            if (ImGui::TreeNode(L("wizard.category.damage_taken"))) {
                for (uint32_t spellId : damageTakenSpellIds) {
                    ImGui::PushID(static_cast<int>(spellId) + 700000);

                    if (iconLoader_) {
                        if (iconLoader_->renderSpellIcon(spellId, ImVec2(16, 16))) {
                            ImGui::SameLine();
                        }
                    }

                    std::string spellName = getSpellName(spellId);
                    if (spellName.empty()) spellName = "Unknown";

                    int32_t firstTime = getFirstDamageTakenTime(combatDb_, spellId);
                    std::string displayName = (firstTime >= 0)
                        ? spellName + " @ " + formatTimestamp(firstTime)
                        : spellName;

                    bool isSelected = (selectedSpellId_ == spellId &&
                                      selectedCategory_ == SelectionCategory::DamageTaken);

                    if (ImGui::Selectable(displayName.c_str(), isSelected)) {
                        selectedSpellId_ = spellId;
                        selectedCreatureName_.clear();
                        selectedCategory_ = SelectionCategory::DamageTaken;
                        configTiming_ = TriggerTiming::OnDamageTaken;
                        configCategory_ = TriggerCategory::Spell;
                        configAnchor_ = TriggerAnchor::Target;  // the victim
                    }

                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("%s\nSpell ID: %u", L("wizard.tooltip.damage_taken"), spellId);
                    }

                    ImGui::PopID();
                }
                ImGui::TreePop();
            }
        }
    }
}
