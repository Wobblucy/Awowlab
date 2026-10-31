#include "UI/Benchmark/UIMentorPreview.h"

#include "Core/Benchmark/MentorProfile.h"
#include "Core/Benchmark/SpellClassification.h"
#include "Core/LocalizationManager.h"
#include "Core/NumberFormatter.h"
#include "UI/AwlUI/Theme.h"
#include "UI/AwlUI/Widgets.h"
#include "UI/SpellIconLoader.h"

#include <imgui.h>

#include <algorithm>
#include <cstdio>
#include <unordered_map>

namespace benchmark_ui {

namespace {

using namespace benchmark;

std::string formatClock(int32_t ms) {
    if (ms < 0) ms = 0;
    const int32_t totalSeconds = ms / 1000;
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "%d:%02d",
                  totalSeconds / 60, totalSeconds % 60);
    return buffer;
}

// One labelled figure for the row across the top.
void statLine(const char* label, const std::string& value, ImVec4 valueColor) {
    ImGui::PushStyleColor(ImGuiCol_Text, theme::TextMuted);
    ImGui::TextUnformatted(label);
    ImGui::PopStyleColor();
    ImGui::SameLine(220.0f);
    ImGui::PushStyleColor(ImGuiCol_Text, valueColor);
    ImGui::PushFont(theme::gFontMedium);
    ImGui::TextUnformatted(value.c_str());
    ImGui::PopFont();
    ImGui::PopStyleColor();
}

void statLine(const char* label, const std::string& value) {
    statLine(label, value, theme::TextPrimary);
}

} // namespace

void UIMentorPreview::setVisible(bool visible) {
    visible_ = visible;
    if (visible) justOpened_ = true;
}

std::string UIMentorPreview::spellName(const MentorPreviewContext& context,
                                       uint32_t spellId) const {
    if (auto it = context.spellNames.find(spellId); it != context.spellNames.end()) {
        if (!it->second.empty()) return it->second;
    }
    return LF("benchmark.unknown_spell", spellId);
}

void UIMentorPreview::renderSummary(const MentorPreviewContext& context) {
    const auto& profile = *context.profile;

    // Totals come from the timeline, the same data the comparison reads.
    int64_t totalDamage = 0;
    int64_t totalTaken = 0;
    int32_t totalCasts = 0;
    for (const auto& bucket : profile.timeline) {
        totalDamage += bucket.damageDone;
        totalTaken += bucket.damageTaken;
        totalCasts += bucket.casts;
    }

    const double minutes = profile.pullDurationMs / 60000.0;
    const double dps = minutes > 0.0
        ? totalDamage / (profile.pullDurationMs / 1000.0) : 0.0;
    const double cpm = minutes > 0.0 ? totalCasts / minutes : 0.0;

    statLine(L("preview.player"), profile.playerName);
    statLine(L("preview.encounter"), context.encounterName);
    statLine(L("preview.duration"), formatClock(profile.pullDurationMs));
    statLine(L("preview.damage"), ui::format::formatAmount(totalDamage));
    statLine(L("preview.dps"), ui::format::formatAmount(static_cast<int64_t>(dps)));
    statLine(L("preview.taken"), ui::format::formatAmount(totalTaken));

    // Globals rather than raw casts: off-global and ignored spells are
    // already excluded, so a very high number means a ruling did not apply.
    statLine(L("preview.globals"), std::to_string(totalCasts));
    statLine(L("preview.globals_per_min"),
             LF("preview.per_minute", cpm),
             cpm > 60.0 ? theme::Warn : theme::TextPrimary);

    ImGui::Dummy(ImVec2(0, theme::SpaceM));

    statLine(L("preview.major_cooldowns"),
             std::to_string(profile.majorCooldownSpellIds.size()),
             profile.majorCooldownSpellIds.empty() ? theme::Warn
                                                   : theme::TextPrimary);
    statLine(L("preview.cd_uses"), std::to_string(profile.cooldownUses.size()));
    statLine(L("preview.ignored"), std::to_string(profile.ignoredSpellIds.size()));
    statLine(L("preview.off_global"), std::to_string(profile.offGlobalSpellIds.size()));

    // With no major cooldown ruled, the whole pull counts as filler and every
    // burst comparison is empty, which is rarely intended.
    if (profile.majorCooldownSpellIds.empty()) {
        ImGui::Dummy(ImVec2(0, theme::SpaceS));
        ImGui::PushStyleColor(ImGuiCol_Text, theme::Warn);
        ImGui::TextWrapped("%s", L("preview.no_cooldowns_warning"));
        ImGui::PopStyleColor();
    }

    if (!profile.unresolvedPhaseLabels.empty()) {
        ImGui::Dummy(ImVec2(0, theme::SpaceS));
        ImGui::PushStyleColor(ImGuiCol_Text, theme::TextMuted);
        std::string labels;
        for (const auto& label : profile.unresolvedPhaseLabels) {
            if (!labels.empty()) labels += ", ";
            labels += label;
        }
        ImGui::TextWrapped("%s", LF("preview.unresolved_phases",
                                    labels.c_str()).c_str());
        ImGui::PopStyleColor();
    }
}

void UIMentorPreview::renderCooldowns(const MentorPreviewContext& context) {
    const auto& profile = *context.profile;

    if (profile.cooldownUses.empty()) {
        awlui::EmptyState("##nocds", "=", L("preview.no_cd_uses"),
                          L("preview.no_cd_uses_hint"));
        return;
    }

    ImGui::PushStyleColor(ImGuiCol_Text, theme::TextMuted);
    ImGui::TextWrapped("%s", L("preview.cd_hint"));
    ImGui::PopStyleColor();
    ImGui::Dummy(ImVec2(0, theme::SpaceS));

    for (const auto& use : profile.cooldownUses) {
        ImGui::PushID(static_cast<int>(use.spellId * 100 + use.useIndex));

        const double seconds = use.windowMs / 1000.0;
        const double dps = seconds > 0.0 ? use.damageInWindow / seconds : 0.0;

        ImGui::PushFont(theme::gFontMedium);
        ImGui::TextUnformatted(LF("benchmark.cd_row",
                                  spellName(context, use.spellId).c_str(),
                                  use.useIndex).c_str());
        ImGui::PopFont();

        ImGui::PushStyleColor(ImGuiCol_Text, theme::TextSecondary);
        ImGui::TextUnformatted(LF("preview.cd_detail",
                                  formatClock(use.pullOffsetMs).c_str(),
                                  formatClock(use.pullOffsetMs + use.windowMs).c_str(),
                                  ui::format::formatAmount(use.damageInWindow).c_str(),
                                  ui::format::formatAmount(static_cast<int64_t>(dps)).c_str(),
                                  use.castsInWindow).c_str());
        ImGui::PopStyleColor();

        ImGui::Dummy(ImVec2(0, theme::SpaceS));
        ImGui::PopID();
    }
}

void UIMentorPreview::renderSpells(const MentorPreviewContext& context) {
    const auto& profile = *context.profile;

    // Cast counts across the whole pull, summed from the phases and sorted by
    // use. The top of the list should look like the spec's rotation.
    std::unordered_map<uint32_t, int32_t> castsBySpell;
    for (const auto& phase : profile.phases) {
        for (const auto& usage : phase.spells) {
            castsBySpell[usage.spellId] += usage.casts;
        }
    }

    if (castsBySpell.empty()) {
        awlui::EmptyState("##nospells", "=", L("preview.no_spells"), nullptr);
        return;
    }

    std::vector<std::pair<uint32_t, int32_t>> rows(castsBySpell.begin(),
                                                   castsBySpell.end());
    std::sort(rows.begin(), rows.end(),
              [](const auto& a, const auto& b) {
                  if (a.second != b.second) return a.second > b.second;
                  return a.first < b.first;
              });

    const double minutes = profile.pullDurationMs / 60000.0;

    for (const auto& [spellId, casts] : rows) {
        const double cpm = minutes > 0.0 ? casts / minutes : 0.0;
        ImGui::TextUnformatted(spellName(context, spellId).c_str());
        ImGui::SameLine(260.0f);
        ImGui::PushStyleColor(ImGuiCol_Text, theme::TextSecondary);
        ImGui::TextUnformatted(LF("preview.spell_row", casts, cpm).c_str());
        ImGui::PopStyleColor();
    }
}

PreviewAction UIMentorPreview::render(const MentorPreviewContext& context) {
    PreviewAction action = PreviewAction::None;
    if (!visible_ || !context.profile) return action;

    if (justOpened_) {
        ImGui::SetNextWindowFocus();
        ImGui::SetNextWindowSize(ImVec2(660, 580), ImGuiCond_FirstUseEver);
        const ImVec2 display = ImGui::GetIO().DisplaySize;
        ImGui::SetNextWindowPos(ImVec2(display.x * 0.5f, display.y * 0.5f),
                                ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
        justOpened_ = false;
    }

    if (ImGui::Begin(L("preview.title"), &visible_)) {
        ImGui::PushStyleColor(ImGuiCol_Text, theme::TextMuted);
        ImGui::TextWrapped("%s", L("preview.hint"));
        ImGui::PopStyleColor();
        ImGui::Dummy(ImVec2(0, theme::SpaceM));

        static const char* const kTabs[] = {
            "preview.tab.summary",
            "preview.tab.cooldowns",
            "preview.tab.spells",
        };
        const char* tabLabels[3];
        for (int i = 0; i < 3; ++i) tabLabels[i] = L(kTabs[i]);

        awlui::TabBar("##previewtabs", &activeTab_, tabLabels, 3);
        ImGui::Dummy(ImVec2(0, theme::SpaceS));

        ImGui::BeginChild("##previewbody", ImVec2(0, -44.0f));
        switch (activeTab_) {
            case 0: renderSummary(context); break;
            case 1: renderCooldowns(context); break;
            case 2: renderSpells(context); break;
            default: break;
        }
        ImGui::EndChild();

        ImGui::Separator();
        if (awlui::Button(L("preview.action.accept"),
                          awlui::ButtonVariant::Primary)) {
            action = PreviewAction::Accept;
            visible_ = false;
        }
        ImGui::SameLine();
        // Reopens the whole prepare flow, since a wrong number can come from a
        // spell ruling, a phase boundary or a mob weight. The reference is
        // rebuilt from the same log afterwards.
        if (awlui::Button(L("preview.action.reopen_prepare"),
                          awlui::ButtonVariant::Secondary)) {
            action = PreviewAction::ReopenPrepare;
        }
    }
    ImGui::End();

    return action;
}

} // namespace benchmark_ui
