#include "UI/Panels/Settings/UIEnemyCastFilterPanel.h"
#include "UI/AwlUI/Widgets.h"
#include "UI/BossTimerSettings.h"
#include "UI/SpellIconLoader.h"
#include "UI/Panels/Navigation/BossTimerStrip.h"
#include "UI/Panels/Navigation/UIUpcomingBossCasts.h"
#include "AnnotationManager/include/AutoAnnotationTypes.h"
#include "Core/SpellNameDatabase.h"
#include <imgui.h>
#include <algorithm>
#include <cctype>

UIEnemyCastFilterPanel::UIEnemyCastFilterPanel() = default;
UIEnemyCastFilterPanel::~UIEnemyCastFilterPanel() = default;

void UIEnemyCastFilterPanel::setHostileCreatureSpells(const std::vector<HostileCreatureSpells>& creatureSpells) {
    creatureSpells_.clear();
    creatureSpells_.reserve(creatureSpells.size());

    // Convert from HostileCreatureSpells (vector<uint32_t>) to FilterableCreatureSpells (unordered_set<uint32_t>)
    for (const auto& src : creatureSpells) {
        FilterableCreatureSpells dst;
        dst.creature_name = src.creature_name;
        dst.cast_ids.insert(src.spell_ids.begin(), src.spell_ids.end());
        dst.debuff_ids.insert(src.debuff_ids.begin(), src.debuff_ids.end());
        dst.damage_ids.insert(src.damage_ids.begin(), src.damage_ids.end());
        creatureSpells_.push_back(std::move(dst));
    }

    if (selectedCreatureIndex_ >= static_cast<int>(creatureSpells_.size())) {
        selectedCreatureIndex_ = -1;
    }

    // Enable all cast spells by default so users see the timer bar initially
    if (settings_) {
        for (const auto& creature : creatureSpells_) {
            for (uint32_t spellId : creature.cast_ids) {
                settings_->enableSpell(spellId);
            }
        }
    }
}

void UIEnemyCastFilterPanel::render() {
    if (!visible_) return;

    ImGui::SetNextWindowSize(ImVec2(650, 550), ImGuiCond_FirstUseEver);

    if (ImGui::Begin("Enemy Cast Filters", &visible_)) {
        // Timer settings at top
        renderTimerSettings();

        ImGui::Separator();
        ImGui::Spacing();

        // Two-column layout
        float availWidth = ImGui::GetContentRegionAvail().x;

        // Left column: Creature list
        ImGui::BeginChild("CreatureList", ImVec2(availWidth * 0.35f, 0), true);
        renderCreatureList();
        ImGui::EndChild();

        ImGui::SameLine();

        // Right column: Spell list for selected creature
        ImGui::BeginChild("SpellList", ImVec2(0, 0), true);
        renderSpellList();
        ImGui::EndChild();
    }
    ImGui::End();
}

void UIEnemyCastFilterPanel::renderTimerSettings() {
    if (!settings_) return;

    if (ImGui::CollapsingHeader("Timeline Strip Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Indent();

        // Event type toggles
        ImGui::Text("Show Event Types:");
        ImGui::SameLine();

        bool changed = false;
        if (awlui::Checkbox("Casts", &settings_->showCasts)) changed = true;
        ImGui::SameLine();
        if (awlui::Checkbox("Damage", &settings_->showDamage)) changed = true;
        ImGui::SameLine();
        if (awlui::Checkbox("Debuffs", &settings_->showDebuffs)) changed = true;

        ImGui::Spacing();

        // Icon size slider
        ImGui::PushItemWidth(180);
        if (awlui::SliderFloat("Icon Size##strip", &settings_->iconSize, 16.0f, 48.0f, "%.0f px")) {
            changed = true;
        }

        // Grouping window slider (convert ms to seconds for UI)
        float groupingSeconds = settings_->groupingWindowMs / 1000.0f;
        if (awlui::SliderFloat("Grouping Window", &groupingSeconds, 0.5f, 10.0f, "%.1f s")) {
            settings_->groupingWindowMs = static_cast<uint32_t>(groupingSeconds * 1000.0f);
            changed = true;
        }
        ImGui::PopItemWidth();

        // Note about grouping
        ImGui::TextDisabled("Identical spells within %.1fs are grouped", groupingSeconds);

        if (changed) {
            if (timerStrip_) timerStrip_->markDirty();
            if (upcomingPanel_) upcomingPanel_->markDirty();
        }

        ImGui::Unindent();
    }

    // Upcoming Boss Casts Panel Settings
    if (ImGui::CollapsingHeader("Upcoming Casts Panel", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Indent();

        bool changed = false;

        // Master toggle for the panel
        if (awlui::Checkbox("Show Panel", &settings_->upcomingCastsPanelEnabled)) {
            changed = true;
        }

        if (settings_->upcomingCastsPanelEnabled) {
            ImGui::Spacing();
            ImGui::PushItemWidth(180);

            // Lookahead window
            if (awlui::SliderFloat("Lookahead", &settings_->upcomingWindowSeconds, 10.0f, 60.0f, "%.0f s")) {
                changed = true;
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("How far ahead to show upcoming abilities");
            }

            // Icon size
            if (awlui::SliderFloat("Icon Size##upcoming", &settings_->upcomingIconSize, 20.0f, 48.0f, "%.0f px")) {
                changed = true;
            }

            ImGui::PopItemWidth();
        }

        if (changed && upcomingPanel_) {
            upcomingPanel_->markDirty();
        }

        ImGui::Unindent();
    }
}

void UIEnemyCastFilterPanel::renderCreatureList() {
    ImGui::Text("Creatures");
    ImGui::Separator();

    // Search filter
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##CreatureSearch", "Search...", searchBuffer_, sizeof(searchBuffer_));

    std::string searchLower;
    if (searchBuffer_[0] != '\0') {
        searchLower = searchBuffer_;
        std::transform(searchLower.begin(), searchLower.end(), searchLower.begin(),
            [](unsigned char c) { return std::tolower(c); });
    }

    ImGui::Spacing();

    for (size_t i = 0; i < creatureSpells_.size(); i++) {
        const auto& creature = creatureSpells_[i];

        // Filter by search
        if (!searchLower.empty()) {
            std::string nameLower = creature.creature_name;
            std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(),
                [](unsigned char c) { return std::tolower(c); });
            if (nameLower.find(searchLower) == std::string::npos) {
                continue;
            }
        }

        // Count visible/total spells
        size_t totalSpells = creature.cast_ids.size() +
                            creature.damage_ids.size() +
                            creature.debuff_ids.size();
        size_t enabledSpells = 0;

        if (settings_) {
            for (uint32_t id : creature.cast_ids) {
                if (settings_->isSpellEnabled(id)) enabledSpells++;
            }
            for (uint32_t id : creature.damage_ids) {
                if (settings_->isSpellEnabled(id)) enabledSpells++;
            }
            for (uint32_t id : creature.debuff_ids) {
                if (settings_->isSpellEnabled(id)) enabledSpells++;
            }
        }

        char label[256];
        snprintf(label, sizeof(label), "%s (%zu/%zu)##%zu",
            creature.creature_name.c_str(),
            enabledSpells, totalSpells, i);

        if (ImGui::Selectable(label, selectedCreatureIndex_ == static_cast<int>(i))) {
            selectedCreatureIndex_ = static_cast<int>(i);
        }
    }
}

void UIEnemyCastFilterPanel::renderSpellList() {
    if (selectedCreatureIndex_ < 0 ||
        selectedCreatureIndex_ >= static_cast<int>(creatureSpells_.size())) {
        ImGui::TextDisabled("Select a creature to view spells");
        return;
    }

    const auto& creature = creatureSpells_[selectedCreatureIndex_];
    ImGui::Text("%s", creature.creature_name.c_str());
    ImGui::Separator();
    ImGui::Spacing();

    [[maybe_unused]] bool typeEnabled = settings_ ? settings_->showCasts : true;

    // Casts section
    if (!creature.cast_ids.empty()) {
        bool castEnabled = settings_ ? settings_->showCasts : true;
        ImGui::PushStyleColor(ImGuiCol_Text, castEnabled ? ImVec4(0.4f, 0.6f, 0.9f, 1.0f) : ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
        if (ImGui::TreeNodeEx("Casts", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::PopStyleColor();

            std::vector<uint32_t> sorted(creature.cast_ids.begin(), creature.cast_ids.end());
            std::sort(sorted.begin(), sorted.end());

            for (uint32_t spellId : sorted) {
                renderSpellRow(spellId, "Cast", castEnabled);
            }
            ImGui::TreePop();
        } else {
            ImGui::PopStyleColor();
        }
    }

    // Damage section
    if (!creature.damage_ids.empty()) {
        bool damageEnabled = settings_ ? settings_->showDamage : false;
        ImGui::PushStyleColor(ImGuiCol_Text, damageEnabled ? ImVec4(0.9f, 0.3f, 0.3f, 1.0f) : ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
        if (ImGui::TreeNodeEx("Damage", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::PopStyleColor();

            std::vector<uint32_t> sorted(creature.damage_ids.begin(), creature.damage_ids.end());
            std::sort(sorted.begin(), sorted.end());

            for (uint32_t spellId : sorted) {
                renderSpellRow(spellId, "Damage", damageEnabled);
            }
            ImGui::TreePop();
        } else {
            ImGui::PopStyleColor();
        }
    }

    // Debuffs section
    if (!creature.debuff_ids.empty()) {
        bool debuffEnabled = settings_ ? settings_->showDebuffs : false;
        ImGui::PushStyleColor(ImGuiCol_Text, debuffEnabled ? ImVec4(0.7f, 0.3f, 0.9f, 1.0f) : ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
        if (ImGui::TreeNodeEx("Debuffs", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::PopStyleColor();

            std::vector<uint32_t> sorted(creature.debuff_ids.begin(), creature.debuff_ids.end());
            std::sort(sorted.begin(), sorted.end());

            for (uint32_t spellId : sorted) {
                renderSpellRow(spellId, "Debuff", debuffEnabled);
            }
            ImGui::TreePop();
        } else {
            ImGui::PopStyleColor();
        }
    }
}

void UIEnemyCastFilterPanel::renderSpellRow(uint32_t spellId, const char* category, bool isTypeEnabled) {
    if (!settings_) return;

    bool isEnabled = settings_->isSpellEnabled(spellId);

    // Dim the row if the type is disabled
    if (!isTypeEnabled) {
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.5f);
    }

    // Checkbox
    char checkboxId[64];
    snprintf(checkboxId, sizeof(checkboxId), "##spell_%u_%s", spellId, category);
    if (awlui::Checkbox(checkboxId, &isEnabled)) {
        settings_->toggleSpell(spellId);
        if (timerStrip_) timerStrip_->markDirty();
        if (upcomingPanel_) upcomingPanel_->markDirty();
    }

    ImGui::SameLine();

    // Spell icon
    if (iconLoader_) {
        if (iconLoader_->renderSpellIcon(spellId, ImVec2(20, 20))) {
            ImGui::SameLine();
        }
    }

    // Spell name and ID
    const char* spellName = SPELL_NAME(spellId);
    ImGui::Text("%s", spellName);
    ImGui::SameLine();
    ImGui::TextDisabled("(%u)", spellId);

    if (!isTypeEnabled) {
        ImGui::PopStyleVar();
    }
}

void UIEnemyCastFilterPanel::navigateToSpell(uint32_t spellId) {
    // Make panel visible
    visible_ = true;

    for (size_t i = 0; i < creatureSpells_.size(); ++i) {
        const auto& creature = creatureSpells_[i];
        if (creature.cast_ids.count(spellId) ||
            creature.damage_ids.count(spellId) ||
            creature.debuff_ids.count(spellId)) {
            selectedCreatureIndex_ = static_cast<int>(i);

            // Set search buffer to spell name for quick visual reference
            const char* spellName = SPELL_NAME(spellId);
            snprintf(searchBuffer_, sizeof(searchBuffer_), "%s", spellName);
            return;
        }
    }

    // Spell not found in any creature, just clear selection and set search to spell ID
    selectedCreatureIndex_ = -1;
    snprintf(searchBuffer_, sizeof(searchBuffer_), "%u", spellId);
}
