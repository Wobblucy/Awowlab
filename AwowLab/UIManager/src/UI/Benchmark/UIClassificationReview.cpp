#include "UI/Benchmark/UIClassificationReview.h"

#include "Core/Benchmark/BenchmarkStore.h"
#include "Core/LocalizationManager.h"
#include "Core/UnifiedSettings.h"
#include "Parser/SpecializationColors.h"
#include "UI/AwlUI/Theme.h"
#include "UI/AwlUI/Widgets.h"

#include <algorithm>
#include <cctype>
#include <cstdio>

namespace benchmark_ui {

namespace {

using namespace awlui;
using namespace benchmark;

// Dropdown order. indexOfCastRole maps between this and the enum.
const CastRole kCastRoles[] = {
    CastRole::Filler,
    CastRole::ProcConsumer,
    CastRole::ResourceSpender,
    CastRole::MajorCooldown,
    CastRole::MinorCooldown,
    CastRole::Trinket,
    CastRole::Cantrip,
    CastRole::Melee,
    CastRole::Defensive,
    CastRole::Utility,
    CastRole::Ignored,
};

const char* castRoleKey(CastRole role) {
    switch (role) {
        case CastRole::Filler:          return "classify.role.filler";
        case CastRole::ProcConsumer:    return "classify.role.proc";
        case CastRole::ResourceSpender: return "classify.role.spender";
        case CastRole::MajorCooldown:   return "classify.role.major";
        case CastRole::MinorCooldown:   return "classify.role.minor";
        case CastRole::Defensive:       return "classify.role.defensive";
        case CastRole::Utility:         return "classify.role.utility";
        case CastRole::Trinket:         return "classify.role.trinket";
        case CastRole::Cantrip:         return "classify.role.cantrip";
        case CastRole::Melee:           return "classify.role.melee";
        case CastRole::Ignored:         return "classify.role.ignored";
        case CastRole::Unclassified:
        default:                        return "classify.role.none";
    }
}

const AuraRole kAuraRoles[] = {
    AuraRole::Proc,
    AuraRole::Maintenance,
    AuraRole::CooldownBuff,
    AuraRole::MajorCooldown,
    AuraRole::Channel,
    AuraRole::Unimportant,
};

const char* auraRoleKey(AuraRole role) {
    switch (role) {
        case AuraRole::Proc:         return "classify.aura.proc";
        case AuraRole::Maintenance:  return "classify.aura.maintenance";
        case AuraRole::CooldownBuff: return "classify.aura.cdbuff";
        case AuraRole::MajorCooldown: return "classify.aura.major";
        case AuraRole::Channel:       return "classify.aura.channel";
        case AuraRole::Unimportant:  return "classify.aura.unimportant";
        case AuraRole::Unclassified:
        default:                     return "classify.aura.none";
    }
}

int indexOfCastRole(CastRole role) {
    for (int i = 0; i < IM_ARRAYSIZE(kCastRoles); ++i) {
        if (kCastRoles[i] == role) return i;
    }
    return 0;
}

int indexOfAuraRole(AuraRole role) {
    for (int i = 0; i < IM_ARRAYSIZE(kAuraRoles); ++i) {
        if (kAuraRoles[i] == role) return i;
    }
    return 0;
}


} // namespace

void UIClassificationReview::setVisible(bool visible) {
    visible_ = visible;
    if (visible) justOpened_ = true;
}

bool UIClassificationReview::hasPendingWork(uint16_t specId) {
    const auto* spec = ClassificationStore::instance().find(specId);
    return spec && !spec->fullyConfirmed();
}

std::string UIClassificationReview::describeEvidence(const SpellClass& spell) const {
    switch (spell.basis) {
        case SuggestionBasis::CadenceMajor:
        case SuggestionBasis::CadenceMinor:
        case SuggestionBasis::CadenceFiller:
            return LF("classify.evidence.cadence",
                      spell.observedCasts,
                      spell.castsPerMinute,
                      spell.medianGapMs / 1000.0);
        case SuggestionBasis::Irregular:
            return LF("classify.evidence.irregular", spell.observedCasts);
        case SuggestionBasis::SingleUse:
            return L("classify.evidence.single");
        case SuggestionBasis::FasterThanGcd:
            return LF("classify.evidence.faster_than_gcd",
                      spell.observedCasts,
                      spell.medianGapMs / 1000.0);
        case SuggestionBasis::AutoAttack:
            return LF("classify.evidence.auto_attack", spell.observedCasts);
        default:
            return LF("classify.evidence.plain", spell.observedCasts);
    }
}

std::string UIClassificationReview::describeEvidence(const AuraClass& aura) const {
    // Average duration as well as uptime: two buffs from one ability (a short
    // one on cast and a long one) have the same application count.
    const double averageSec = aura.applications > 0
        ? (aura.totalUptimeMs / 1000.0) / aura.applications
        : 0.0;
    return LF("classify.evidence.aura",
              aura.applications,
              aura.uptimeShare * 100.0f,
              averageSec);
}

namespace {

std::string displayName(const ClassificationReviewContext& context, uint32_t id) {
    if (context.spellNames) {
        if (auto it = context.spellNames->find(id);
            it != context.spellNames->end() && !it->second.empty()) {
            return it->second;
        }
    }
    return {};
}

std::string toLower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

} // namespace

bool UIClassificationReview::pairedWithAura(const ClassificationReviewContext& context,
                                            uint32_t spellId) const {
    const auto* spec = ClassificationStore::instance().find(context.specId);
    return spec && spec->auras.count(spellId) > 0;
}

bool UIClassificationReview::pairedWithCast(const ClassificationReviewContext& context,
                                            uint32_t spellId) const {
    const auto* spec = ClassificationStore::instance().find(context.specId);
    return spec && spec->spells.count(spellId) > 0;
}

bool UIClassificationReview::nameIsAmbiguous(const ClassificationReviewContext& context,
                                             uint32_t spellId,
                                             const std::string& name) const {
    if (name.empty() || !context.spellNames) return false;
    for (const auto& [otherId, otherName] : *context.spellNames) {
        if (otherId != spellId && otherName == name) return true;
    }
    return false;
}

bool UIClassificationReview::matchesFilter(const std::string& name, uint32_t id) const {
    if (filter_[0] == '\0') return true;
    const std::string needle = toLower(filter_);
    if (toLower(name).find(needle) != std::string::npos) return true;
    // Also match on id, for spells whose name never resolved.
    return std::to_string(id).find(needle) != std::string::npos;
}

std::vector<SpellClass*> UIClassificationReview::sortedConfirmedSpells(
    const ClassificationReviewContext& context,
    SpecClassification& spec) const {
    std::vector<SpellClass*> rows;
    for (auto& [id, spell] : spec.spells) {
        if (!spell.confirmed) continue;
        if (!matchesFilter(displayName(context, id), id)) continue;
        rows.push_back(&spell);
    }
    std::sort(rows.begin(), rows.end(),
              [&](const SpellClass* a, const SpellClass* b) {
                  const std::string an = displayName(context, a->spellId);
                  const std::string bn = displayName(context, b->spellId);
                  // Unnamed spells sort last.
                  if (an.empty() != bn.empty()) return bn.empty();
                  if (an != bn) return an < bn;
                  return a->spellId < b->spellId;
              });
    return rows;
}

std::vector<AuraClass*> UIClassificationReview::sortedConfirmedAuras(
    const ClassificationReviewContext& context,
    SpecClassification& spec) const {
    std::vector<AuraClass*> rows;
    for (auto& [id, aura] : spec.auras) {
        if (!aura.confirmed) continue;
        if (!matchesFilter(displayName(context, id), id)) continue;
        rows.push_back(&aura);
    }
    std::sort(rows.begin(), rows.end(),
              [&](const AuraClass* a, const AuraClass* b) {
                  const std::string an = displayName(context, a->spellId);
                  const std::string bn = displayName(context, b->spellId);
                  if (an.empty() != bn.empty()) return bn.empty();
                  if (an != bn) return an < bn;
                  return a->spellId < b->spellId;
              });
    return rows;
}

void UIClassificationReview::renderSpellRow(const ClassificationReviewContext& context,
                                            SpellClass& spell,
                                            bool& changed) {
    ImGui::PushID(static_cast<int>(spell.spellId));

    const ImVec2 rowStart = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;

    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(rowStart,
                        ImVec2(rowStart.x + width, rowStart.y + 66.0f),
                        ImGui::ColorConvertFloat4ToU32(theme::Surface1),
                        theme::RadiusCard);

    ImGui::Dummy(ImVec2(0, theme::SpaceXS));
    ImGui::Indent(theme::SpaceM);

    std::string name = displayName(context, spell.spellId);
    if (name.empty()) name = LF("benchmark.unknown_spell", spell.spellId);

    ImGui::PushFont(theme::gFontMedium);
    ImGui::TextUnformatted(name.c_str());
    ImGui::PopFont();

    // Two spell ids can share a name and still be different abilities (the
    // in-cooldown version of a spender is its own id), so show the id when the
    // name is ambiguous.
    if (nameIsAmbiguous(context, spell.spellId, name)) {
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, theme::TextMuted);
        ImGui::TextUnformatted(LF("classify.spell_id", spell.spellId).c_str());
        ImGui::PopStyleColor();
    }

    ImGui::PushStyleColor(ImGuiCol_Text, theme::TextMuted);
    ImGui::TextUnformatted(describeEvidence(spell).c_str());
    ImGui::PopStyleColor();

    // A cast that grants a buff with the same id defines a cooldown window:
    // the cast opens it and the buff falling off closes it.
    const bool pairsWithBuff = pairedWithAura(context, spell.spellId);
    if (pairsWithBuff) {
        ImGui::PushStyleColor(ImGuiCol_Text, theme::Accent);
        const bool definesWindow = (spell.role == CastRole::MajorCooldown ||
                                    spell.role == CastRole::MinorCooldown);
        ImGui::TextUnformatted(L(definesWindow ? "classify.window_pair"
                                               : "classify.window_pair_idle"));
        ImGui::PopStyleColor();
    }

    const char* roleLabels[IM_ARRAYSIZE(kCastRoles)];
    for (int i = 0; i < IM_ARRAYSIZE(kCastRoles); ++i) {
        roleLabels[i] = L(castRoleKey(kCastRoles[i]));
    }

    int roleIndex = indexOfCastRole(spell.role);
    ImGui::SetNextItemWidth(190.0f);
    if (awlui::Combo("##role", &roleIndex, roleLabels, IM_ARRAYSIZE(roleLabels))) {
        spell.role = kCastRoles[roleIndex];
        // Picking a role counts as confirming it.
        spell.confirmed = true;
        changed = true;
    }

    ImGui::SameLine();
    if (!spell.confirmed) {
        if (awlui::Button(L("classify.confirm"), awlui::ButtonVariant::Primary,
                          awlui::ButtonSize::Sm)) {
            spell.confirmed = true;
            changed = true;
        }
    } else {
        ImGui::PushStyleColor(ImGuiCol_Text, theme::Success);
        ImGui::TextUnformatted(L("classify.confirmed"));
        ImGui::PopStyleColor();
    }

    ImGui::Unindent(theme::SpaceM);
    ImGui::Dummy(ImVec2(0, theme::SpaceS));
    ImGui::PopID();
}

void UIClassificationReview::renderAuraRow(const ClassificationReviewContext& context,
                                           AuraClass& aura,
                                           bool& changed) {
    ImGui::PushID(static_cast<int>(aura.spellId) + 0x40000000);

    const ImVec2 rowStart = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;

    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(rowStart,
                        ImVec2(rowStart.x + width, rowStart.y + 66.0f),
                        ImGui::ColorConvertFloat4ToU32(theme::Surface1),
                        theme::RadiusCard);

    ImGui::Dummy(ImVec2(0, theme::SpaceXS));
    ImGui::Indent(theme::SpaceM);

    std::string name = displayName(context, aura.spellId);
    if (name.empty()) name = LF("benchmark.unknown_spell", aura.spellId);

    ImGui::PushFont(theme::gFontMedium);
    ImGui::TextUnformatted(name.c_str());
    ImGui::PopFont();

    ImGui::PushStyleColor(ImGuiCol_Text, theme::TextMuted);
    ImGui::TextUnformatted(describeEvidence(aura).c_str());
    ImGui::PopStyleColor();

    // The buff side of a cast/buff pair (see renderSpellRow).
    if (pairedWithCast(context, aura.spellId)) {
        ImGui::PushStyleColor(ImGuiCol_Text, theme::Accent);
        ImGui::TextUnformatted(L("classify.window_pair_buff"));
        ImGui::PopStyleColor();
    }

    const char* roleLabels[IM_ARRAYSIZE(kAuraRoles)];
    for (int i = 0; i < IM_ARRAYSIZE(kAuraRoles); ++i) {
        roleLabels[i] = L(auraRoleKey(kAuraRoles[i]));
    }

    int roleIndex = indexOfAuraRole(aura.role);
    ImGui::SetNextItemWidth(190.0f);
    if (awlui::Combo("##aurarole", &roleIndex, roleLabels, IM_ARRAYSIZE(roleLabels))) {
        aura.role = kAuraRoles[roleIndex];
        aura.confirmed = true;
        changed = true;
    }

    ImGui::SameLine();
    if (!aura.confirmed) {
        if (awlui::Button(L("classify.confirm"), awlui::ButtonVariant::Primary,
                          awlui::ButtonSize::Sm)) {
            aura.confirmed = true;
            changed = true;
        }
    } else {
        ImGui::PushStyleColor(ImGuiCol_Text, theme::Success);
        ImGui::TextUnformatted(L("classify.confirmed"));
        ImGui::PopStyleColor();
    }

    ImGui::Unindent(theme::SpaceM);
    ImGui::Dummy(ImVec2(0, theme::SpaceS));
    ImGui::PopID();
}

bool UIClassificationReview::render(const ClassificationReviewContext& context) {
    if (!visible_) return false;

    if (justOpened_) {
        ImGui::SetNextWindowFocus();
        ImGui::SetNextWindowSize(ImVec2(620, 560), ImGuiCond_FirstUseEver);
        // Centred, since a window at the origin can run off the screen.
        const ImVec2 display = ImGui::GetIO().DisplaySize;
        ImGui::SetNextWindowPos(ImVec2(display.x * 0.5f, display.y * 0.5f),
                                ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
        justOpened_ = false;

        // With nothing pending, show the existing rulings instead of an empty
        // queue.
        hadPendingOnOpen_ = hasPendingWork(context.specId);
        if (!hadPendingOnOpen_) showConfirmed_ = true;
    }

    // Rows size to the content region, so cap the window width.
    ImGui::SetNextWindowSizeConstraints(ImVec2(520, 400), ImVec2(900, FLT_MAX));

    // Only report "finished" when the panel opened with pending work, so
    // editing one ruling later does not close it.
    bool finishedNow = false;
    const bool startedWithQueue = hadPendingOnOpen_;

    if (ImGui::Begin(L("classify.title"), &visible_)) {
        auto& store = ClassificationStore::instance();
        SpecClassification* spec = store.find(context.specId);

        if (!spec) {
            awlui::EmptyState("##nospec", "?", L("classify.empty"),
                              L("classify.empty_hint"));
            ImGui::End();
            return false;
        }

        // Imported reports carry only a spec id, so fall back to the shared
        // name table.
        std::string specLabel = spec->specName;
        if (specLabel.empty()) specLabel = context.specName;
        if (specLabel.empty() && spec->specId != 0) {
            specLabel = parser::getSpecName(spec->specId);
        }
        if (specLabel.empty()) specLabel = L("classify.unknown_spec");

        ImGui::TextUnformatted(LF("classify.subtitle", specLabel.c_str()).c_str());

        const size_t pending = spec->pendingSpellIds.size() + spec->pendingAuraIds.size();
        ImGui::PushStyleColor(ImGuiCol_Text,
                              pending > 0 ? theme::Warn : theme::Success);
        ImGui::TextUnformatted(pending > 0
            ? LF("classify.pending", static_cast<int>(pending)).c_str()
            : L("classify.all_done"));
        ImGui::PopStyleColor();

        ImGui::Dummy(ImVec2(0, theme::SpaceS));
        ImGui::PushStyleColor(ImGuiCol_Text, theme::TextMuted);
        ImGui::TextWrapped("%s", L("classify.explainer"));
        ImGui::PopStyleColor();
        ImGui::Dummy(ImVec2(0, theme::SpaceM));

        // One list: casts first, then buffs, each alphabetical.
        awlui::Checkbox(L("classify.show_confirmed"), &showConfirmed_);
        // Always visible because the filter applies to pending rows too.
        ImGui::SameLine();
        ImGui::SetNextItemWidth(220.0f);
        ImGui::InputTextWithHint("##classifyfilter", L("classify.filter_hint"),
                                 filter_, IM_ARRAYSIZE(filter_));
        ImGui::Dummy(ImVec2(0, theme::SpaceS));

        bool changed = false;

        ImGui::BeginChild("##classifybody", ImVec2(0, -44.0f));

        const auto sectionHeading = [](const char* key) {
            ImGui::Dummy(ImVec2(0, theme::SpaceXS));
            ImGui::PushStyleColor(ImGuiCol_Text, theme::TextMuted);
            ImGui::PushFont(theme::gFontMedium);
            ImGui::TextUnformatted(L(key));
            ImGui::PopFont();
            ImGui::PopStyleColor();
            ImGui::Dummy(ImVec2(0, theme::SpaceXS));
        };

        // Alphabetical within each group, pending and confirmed together.
        const auto byName = [&](const auto* a, const auto* b) {
            const std::string an = displayName(context, a->spellId);
            const std::string bn = displayName(context, b->spellId);
            if (an.empty() != bn.empty()) return bn.empty();
            const std::string al = toLower(an);
            const std::string bl = toLower(bn);
            if (al != bl) return al < bl;
            return a->spellId < b->spellId;
        };

        std::vector<SpellClass*> spellRows;
        for (uint32_t id : spec->pendingSpellIds) {
            auto it = spec->spells.find(id);
            if (it != spec->spells.end() &&
                matchesFilter(displayName(context, id), id)) {
                spellRows.push_back(&it->second);
            }
        }
        if (showConfirmed_) {
            for (SpellClass* spell : sortedConfirmedSpells(context, *spec)) {
                spellRows.push_back(spell);
            }
        }

        std::vector<AuraClass*> auraRows;
        for (uint32_t id : spec->pendingAuraIds) {
            auto it = spec->auras.find(id);
            if (it != spec->auras.end() &&
                matchesFilter(displayName(context, id), id)) {
                auraRows.push_back(&it->second);
            }
        }
        if (showConfirmed_) {
            for (AuraClass* aura : sortedConfirmedAuras(context, *spec)) {
                auraRows.push_back(aura);
            }
        }

        std::sort(spellRows.begin(), spellRows.end(), byName);
        std::sort(auraRows.begin(), auraRows.end(), byName);

        if (!spellRows.empty()) {
            sectionHeading("classify.section.casts");
            for (SpellClass* spell : spellRows) {
                renderSpellRow(context, *spell, changed);
            }
        }
        if (!auraRows.empty()) {
            sectionHeading("classify.section.buffs");
            for (AuraClass* aura : auraRows) {
                renderAuraRow(context, *aura, changed);
            }
        }
        if (spellRows.empty() && auraRows.empty()) {
            awlui::EmptyState("##nothing", "=", L("classify.no_pending_spells"),
                              L("classify.no_pending_hint"));
        }
        ImGui::EndChild();

        ImGui::Separator();

        if (pending > 0) {
            if (awlui::Button(L("classify.accept_all"), awlui::ButtonVariant::Primary)) {
                store.confirmAllSuggested(context.specId);
                changed = true;
            }
            ImGui::SameLine();
        }
        if (awlui::Button(L("classify.close"), awlui::ButtonVariant::Secondary)) {
            visible_ = false;
        }

        if (changed) {
            // Rulings go back through the store so it rebuilds its pending
            // lists, which also drive the comparison's blocking check. They
            // are collected first because confirmSpell rebuilds the maps being
            // read here.
            std::vector<std::pair<uint32_t, CastRole>> spellRulings;
            for (const auto& [id, spell] : spec->spells) {
                if (spell.confirmed) spellRulings.emplace_back(id, spell.role);
            }
            std::vector<std::pair<uint32_t, AuraRole>> auraRulings;
            for (const auto& [id, aura] : spec->auras) {
                if (aura.confirmed) auraRulings.emplace_back(id, aura.role);
            }
            spec = nullptr;   // Invalidated by the calls below.

            // One write for the whole pass rather than one per ruling.
            {
                ClassificationStore::BatchGuard batch(store);
                for (const auto& [id, role] : spellRulings) {
                    store.confirmSpell(context.specId, id, role);
                }
                for (const auto& [id, role] : auraRulings) {
                    store.confirmAura(context.specId, id, role);
                }
            }

            const auto* refreshed = store.find(context.specId);
            if (startedWithQueue && refreshed && refreshed->fullyConfirmed()) {
                finishedNow = true;
            }
        }
    }
    ImGui::End();

    return finishedNow;
}

} // namespace benchmark_ui
