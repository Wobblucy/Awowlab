#include "UI/Benchmark/UIBenchmarkReportCard.h"

#include "Core/LocalizationManager.h"
#include "Core/MobWeightSettings.h"
#include "Core/NumberFormatter.h"
#include "UI/AwlUI/Theme.h"
#include "UI/AwlUI/Widgets.h"
#include "UI/Benchmark/BenchmarkCharts.h"
#include "UI/SpellIconLoader.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace benchmark_ui {

namespace {

using namespace awlui;
using namespace benchmark;

std::string formatDuration(int32_t ms) {
    if (ms < 0) ms = 0;
    const int32_t totalSeconds = ms / 1000;
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%d:%02d", totalSeconds / 60, totalSeconds % 60);
    return buffer;
}

std::string formatSigned(double value, const char* suffix) {
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%s%.1f%s", value >= 0 ? "+" : "", value, suffix);
    return buffer;
}

// The muted explanation drawn under a section heading.
void drawHint(const char* text) {
    ImGui::PushStyleColor(ImGuiCol_Text, theme::TextMuted);
    ImGui::TextWrapped("%s", text);
    ImGui::PopStyleColor();
}

// Icon lookup for the chart helpers. Returns nothing when the spell has no
// icon of its own, so the chart shows a name chip instead of the generic
// placeholder icon.
std::function<ImTextureID(uint32_t)> realIconLookup(SpellIconLoader* loader) {
    return [loader](uint32_t spellId) -> ImTextureID {
        if (!loader || !loader->hasRealIcon(spellId)) return ImTextureID{};
        return loader->getSpellIcon(spellId);
    };
}

// Three letters of a spell name, for the chip drawn in place of a missing
// icon on the narrow strips.
std::string chipLabel(std::string name) {
    if (name.size() > 4) name = name.substr(0, 3);
    return name;
}

// Per-key totals for the user (first) and the reference (second).
template <typename Value>
using PairTotals = std::unordered_map<uint32_t, std::pair<Value, Value>>;

template <typename Value>
using PairRows = std::vector<std::pair<uint32_t, std::pair<Value, Value>>>;

// The rows with the largest gap between the two players, biggest first.
template <typename Value>
PairRows<Value> largestGapsFirst(const PairTotals<Value>& totals, size_t limit) {
    PairRows<Value> rows(totals.begin(), totals.end());
    std::sort(rows.begin(), rows.end(),
              [](const auto& a, const auto& b) {
                  return std::abs(a.second.first - a.second.second) >
                         std::abs(b.second.first - b.second.second);
              });
    if (rows.size() > limit) rows.resize(limit);
    return rows;
}

// Shared axis for a group of pair bars.
template <typename Value>
double peakOf(const PairRows<Value>& rows) {
    double peak = 1.0;
    for (const auto& [key, pair] : rows) {
        peak = std::max({peak, static_cast<double>(pair.first),
                         static_cast<double>(pair.second)});
    }
    return peak;
}

constexpr size_t kMaxPairRows = 10;

} // namespace

void UIBenchmarkReportCard::setVisible(bool visible) {
    visible_ = visible;
    if (visible) {
        justOpened_ = true;
        requestedTimeMs_ = -1;
        iconsPreloaded_ = false;
    }
}

std::string UIBenchmarkReportCard::spellName(const ReportContext& context,
                                             uint32_t spellId) const {
    if (context.spellNames) {
        if (auto it = context.spellNames->find(spellId);
            it != context.spellNames->end() && !it->second.empty()) {
            return it->second;
        }
    }
    return LF("benchmark.unknown_spell", spellId);
}

std::string UIBenchmarkReportCard::npcName(const ReportContext& context,
                                           uint32_t npcId) const {
    if (context.npcNames) {
        if (auto it = context.npcNames->find(npcId);
            it != context.npcNames->end() && !it->second.empty()) {
            return it->second;
        }
    }
    return LF("benchmark.unknown_npc", npcId);
}

void UIBenchmarkReportCard::renderHeadline(const ReportContext& context) {
    const auto& summary = context.result->summary;

    // Shown first because every figure below was computed from the old
    // rulings.
    if (context.profilesStale) {
        const ImVec2 start = ImGui::GetCursorScreenPos();
        const float width = ImGui::GetContentRegionAvail().x;
        ImDrawList* draw = ImGui::GetWindowDrawList();
        draw->AddRectFilled(start, ImVec2(start.x + width, start.y + 34.0f),
                            ImGui::ColorConvertFloat4ToU32(theme::Surface1),
                            theme::RadiusCard);
        draw->AddRectFilled(start, ImVec2(start.x + 3.0f, start.y + 34.0f),
                            ImGui::ColorConvertFloat4ToU32(theme::Warn),
                            theme::RadiusControl);

        ImGui::Dummy(ImVec2(0, theme::SpaceXS));
        ImGui::Indent(theme::SpaceM);
        ImGui::PushStyleColor(ImGuiCol_Text, theme::Warn);
        ImGui::TextUnformatted(L("benchmark.stale_rulings"));
        ImGui::PopStyleColor();

        if (context.canRebuild) {
            ImGui::SameLine();
            if (awlui::Button(L("benchmark.action.rebuild"),
                              awlui::ButtonVariant::Primary,
                              awlui::ButtonSize::Sm)) {
                pendingAction_ = ReportAction::RebuildMentor;
            }
        }

        ImGui::Unindent(theme::SpaceM);
        ImGui::Dummy(ImVec2(0, theme::SpaceS));
    }

    // One-line summary first; the rest of the card supports it.
    const bool behind = summary.dpsDelta < 0.0;
    const double magnitude = summary.mentorDps > 0.0
        ? std::abs(summary.dpsDelta) / summary.mentorDps * 100.0
        : 0.0;

    ImGui::PushFont(theme::gFontMedium);
    ImGui::PushStyleColor(ImGuiCol_Text,
                          behind ? theme::TextPrimary : theme::Success);
    ImGui::TextUnformatted(
        LF(behind ? "benchmark.verdict_behind" : "benchmark.verdict_ahead",
           magnitude, context.mentorName.c_str()).c_str());
    ImGui::PopStyleColor();
    ImGui::PopFont();

    // Which span of the pull is being compared.
    ImGui::PushStyleColor(ImGuiCol_Text, theme::TextMuted);
    if (summary.userDied) {
        ImGui::TextUnformatted(LF("benchmark.window_died",
                                  formatDuration(summary.comparedDurationMs).c_str(),
                                  context.userName.c_str()).c_str());
    } else if (summary.cappedByMentor) {
        // The reference ended first, so the comparison stops where it did.
        ImGui::TextUnformatted(LF("benchmark.window_mentor_end",
                                  formatDuration(summary.comparedDurationMs).c_str()).c_str());
    } else {
        ImGui::TextUnformatted(LF("benchmark.window_survived",
                                  formatDuration(summary.comparedDurationMs).c_str()).c_str());
    }
    ImGui::PopStyleColor();

    ImGui::Dummy(ImVec2(0, theme::SpaceM));

    const float avail = ImGui::GetContentRegionAvail().x;
    const float tileWidth = std::max(150.0f, (avail - theme::SpaceS * 3.0f) / 4.0f);
    const ImVec2 tileSize(tileWidth, 108.0f);

    const auto amount = [](double value) {
        return ui::format::formatAmount(static_cast<int64_t>(value));
    };
    const auto oneDecimal = [](double value) {
        char buffer[32];
        std::snprintf(buffer, sizeof(buffer), "%.1f", value);
        return std::string(buffer);
    };

    const auto drawTile = [&](const char* label, double userValue, double mentorValue,
                              std::string userText, std::string mentorText,
                              double delta) {
        VersusStat stat;
        stat.label = label;
        stat.userText = std::move(userText);
        stat.mentorText = std::move(mentorText);
        stat.userValue = userValue;
        stat.mentorValue = mentorValue;
        stat.deltaText = formatSigned(delta, "");
        DrawVersusStat(stat, tileSize);
    };

    drawTile(L("benchmark.stat.dps"), summary.userDps, summary.mentorDps,
             amount(summary.userDps), amount(summary.mentorDps),
             summary.dpsDelta);
    ImGui::SameLine(0.0f, theme::SpaceS);

    drawTile(L("benchmark.stat.cpm"),
             summary.userCastsPerMinute, summary.mentorCastsPerMinute,
             oneDecimal(summary.userCastsPerMinute),
             oneDecimal(summary.mentorCastsPerMinute),
             summary.castsPerMinuteDelta);
    ImGui::SameLine(0.0f, theme::SpaceS);

    drawTile(L("benchmark.stat.burst"), summary.userBurstDps, summary.mentorBurstDps,
             amount(summary.userBurstDps), amount(summary.mentorBurstDps),
             summary.userBurstDps - summary.mentorBurstDps);
    ImGui::SameLine(0.0f, theme::SpaceS);

    drawTile(L("benchmark.stat.filler"), summary.userFillerDps, summary.mentorFillerDps,
             amount(summary.userFillerDps), amount(summary.mentorFillerDps),
             summary.userFillerDps - summary.mentorFillerDps);

    // Splits the shortfall between burst and filler.
    if (summary.burstDamageDelta < 0 || summary.fillerDamageDelta < 0) {
        ImGui::Dummy(ImVec2(0, theme::SpaceM));

        ImGui::PushStyleColor(ImGuiCol_Text, theme::TextSecondary);
        ImGui::TextUnformatted(L("benchmark.shortfall_heading"));
        ImGui::PopStyleColor();
        ImGui::Dummy(ImVec2(0, theme::SpaceXS));

        // Only the side that lost damage gets a label; a surplus printed
        // under the bar would read as a loss.
        const std::string burstLabel = summary.burstDamageDelta < 0
            ? LF("benchmark.shortfall_burst",
                 ui::format::formatAmount(std::abs(summary.burstDamageDelta)).c_str())
            : std::string();
        const std::string fillerLabel = summary.fillerDamageDelta < 0
            ? LF("benchmark.shortfall_filler",
                 ui::format::formatAmount(std::abs(summary.fillerDamageDelta)).c_str())
            : std::string();

        DrawShortfallSplit(summary.burstDamageDelta, summary.fillerDamageDelta,
                           burstLabel.c_str(), fillerLabel.c_str(),
                           ImVec2(ImGui::GetContentRegionAvail().x, 0));
    }
}

void UIBenchmarkReportCard::renderBlocked(const ReportContext& context,
                                          ReportAction& action) {
    const auto& result = *context.result;

    const char* headline = L("benchmark.blocked.generic");
    const char* hint = nullptr;
    const char* actionLabel = nullptr;

    if (result.blockingReason == "unconfirmed_spells") {
        headline = L("benchmark.blocked.unconfirmed");
        hint = L("benchmark.blocked.unconfirmed_hint");
        actionLabel = L("benchmark.action.review");
    } else if (result.blockingReason == "spec_mismatch") {
        headline = L("benchmark.blocked.spec");
        hint = L("benchmark.blocked.spec_hint");
        actionLabel = L("benchmark.action.change_mentor");
    } else if (result.blockingReason == "encounter_mismatch") {
        headline = L("benchmark.blocked.encounter");
        actionLabel = L("benchmark.action.change_mentor");
    } else if (result.blockingReason == "phase_rules_changed") {
        headline = L("benchmark.blocked.phases");
        hint = L("benchmark.blocked.phases_hint");
    } else if (result.blockingReason == "no_overlap") {
        headline = L("benchmark.blocked.no_overlap");
    }

    if (awlui::EmptyState("##blocked", "!", headline, hint, actionLabel)) {
        action = (result.blockingReason == "unconfirmed_spells")
                     ? ReportAction::ReviewClasses
                     : ReportAction::ChangeMentor;
    }

    if (!result.unclassifiedSpellIds.empty()) {
        ImGui::Dummy(ImVec2(0, theme::SpaceS));
        ImGui::PushStyleColor(ImGuiCol_Text, theme::TextMuted);
        ImGui::TextUnformatted(LF("benchmark.blocked.pending_count",
                                  static_cast<int>(result.unclassifiedSpellIds.size())).c_str());
        ImGui::PopStyleColor();
    }
}

void UIBenchmarkReportCard::renderPairBar(const char* label,
                                          double userValue,
                                          double mentorValue,
                                          const std::string& userText,
                                          const std::string& mentorText,
                                          bool higherIsBetter,
                                          float width,
                                          double scaleMax) {
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float labelWidth = 190.0f;
    const float barHeight = 11.0f;
    const float trackX = origin.x + labelWidth;
    const float trackWidth = std::max(width - labelWidth - 130.0f, 60.0f);

    draw->AddText(ImVec2(origin.x, origin.y + 2.0f),
                  ImGui::ColorConvertFloat4ToU32(theme::TextPrimary), label);

    const double peak = scaleMax > 0.0
        ? std::max(scaleMax, 1.0)
        : std::max({userValue, mentorValue, 1.0});
    const auto bar = [&](float y, double value, ImVec4 colour) {
        draw->AddRectFilled(ImVec2(trackX, y),
                            ImVec2(trackX + trackWidth, y + barHeight),
                            ImGui::ColorConvertFloat4ToU32(theme::Surface2), 2.0f);
        const float w = trackWidth * static_cast<float>(std::clamp(value / peak, 0.0, 1.0));
        if (w > 0.0f) {
            draw->AddRectFilled(ImVec2(trackX, y), ImVec2(trackX + w, y + barHeight),
                                ImGui::ColorConvertFloat4ToU32(colour), 2.0f);
        }
    };

    bar(origin.y, userValue, theme::Accent);
    bar(origin.y + barHeight + 3.0f, mentorValue, theme::TextSecondary);

    const float textX = trackX + trackWidth + theme::SpaceS;
    draw->AddText(ImVec2(textX, origin.y - 1.0f),
                  ImGui::ColorConvertFloat4ToU32(theme::TextPrimary),
                  userText.c_str());
    draw->AddText(ImVec2(textX, origin.y + barHeight + 2.0f),
                  ImGui::ColorConvertFloat4ToU32(theme::TextSecondary),
                  mentorText.c_str());

    const bool ahead = higherIsBetter ? userValue >= mentorValue
                                      : userValue <= mentorValue;
    draw->AddRectFilled(ImVec2(origin.x - 4.0f, origin.y),
                        ImVec2(origin.x - 1.0f, origin.y + barHeight * 2.0f + 3.0f),
                        ImGui::ColorConvertFloat4ToU32(ahead ? theme::Success
                                                             : theme::Warn));

    ImGui::Dummy(ImVec2(width, barHeight * 2.0f + 14.0f));
}

void UIBenchmarkReportCard::renderFindings(const ReportContext& context,
                                           ReportAction& action) {
    if (!context.result->valid) {
        renderBlocked(context, action);
        return;
    }
    if (!context.userProfile || !context.mentorProfile) return;

    const auto& user = *context.userProfile;
    const auto& mentor = *context.mentorProfile;
    const auto& summary = context.result->summary;

    drawHint(L("benchmark.differences_hint"));
    ImGui::Dummy(ImVec2(0, theme::SpaceM));

    const float width = ImGui::GetContentRegionAvail().x;
    const auto amount = [](int64_t v) { return ui::format::formatAmount(v); };

    if (awlui::BeginSection(L("benchmark.diff.output"))) {
        // Weighted first so damage into low-priority adds does not decide the
        // comparison. Raw is shown too, to match other tools.
        renderPairBar(L("benchmark.diff.weighted"),
                      static_cast<double>(user.weightedDamageDone),
                      static_cast<double>(mentor.weightedDamageDone),
                      amount(user.weightedDamageDone),
                      amount(mentor.weightedDamageDone), true, width);

        if (user.rawDamageDone != user.weightedDamageDone ||
            mentor.rawDamageDone != mentor.weightedDamageDone) {
            renderPairBar(L("benchmark.diff.raw"),
                          static_cast<double>(user.rawDamageDone),
                          static_cast<double>(mentor.rawDamageDone),
                          amount(user.rawDamageDone),
                          amount(mentor.rawDamageDone), true, width);
        }

        renderPairBar(L("benchmark.diff.burst"),
                      summary.userBurstDps, summary.mentorBurstDps,
                      amount(static_cast<int64_t>(summary.userBurstDps)),
                      amount(static_cast<int64_t>(summary.mentorBurstDps)),
                      true, width);

        renderPairBar(L("benchmark.diff.filler"),
                      summary.userFillerDps, summary.mentorFillerDps,
                      amount(static_cast<int64_t>(summary.userFillerDps)),
                      amount(static_cast<int64_t>(summary.mentorFillerDps)),
                      true, width);
    }
    awlui::EndSection();
    ImGui::Dummy(ImVec2(0, theme::SpaceM));

    if (awlui::BeginSection(L("benchmark.diff.cooldowns"))) {
        renderPairBar(L("benchmark.diff.cd_count"),
                      static_cast<double>(user.cooldownUses.size()),
                      static_cast<double>(mentor.cooldownUses.size()),
                      std::to_string(user.cooldownUses.size()),
                      std::to_string(mentor.cooldownUses.size()), true, width);

        const auto windowTime = [](const benchmark::MentorProfile& p) {
            int64_t total = 0;
            for (const auto& use : p.cooldownUses) total += use.windowMs;
            return total;
        };
        const int64_t userWindow = windowTime(user);
        const int64_t mentorWindow = windowTime(mentor);
        renderPairBar(L("benchmark.diff.cd_uptime"),
                      static_cast<double>(userWindow),
                      static_cast<double>(mentorWindow),
                      formatDuration(static_cast<int32_t>(userWindow)),
                      formatDuration(static_cast<int32_t>(mentorWindow)),
                      true, width);
    }
    awlui::EndSection();
    ImGui::Dummy(ImVec2(0, theme::SpaceM));

    if (awlui::BeginSection(L("benchmark.diff.survival"))) {
        renderPairBar(L("benchmark.diff.taken"),
                      static_cast<double>(summary.userDamageTaken),
                      static_cast<double>(summary.mentorDamageTaken),
                      amount(summary.userDamageTaken),
                      amount(summary.mentorDamageTaken), false, width);

        renderPairBar(L("benchmark.diff.defensives"),
                      static_cast<double>(user.defensiveUses.size()),
                      static_cast<double>(mentor.defensiveUses.size()),
                      std::to_string(user.defensiveUses.size()),
                      std::to_string(mentor.defensiveUses.size()), true, width);

        // Damage taken while each defensive was up, since a count alone does
        // not show whether it covered a hit.
        const auto metDamage = [](const benchmark::MentorProfile& p) {
            int64_t total = 0;
            for (const auto& use : p.defensiveUses) total += use.damageTakenInWindow;
            return total;
        };
        renderPairBar(L("benchmark.diff.mitigated"),
                      static_cast<double>(metDamage(user)),
                      static_cast<double>(metDamage(mentor)),
                      amount(metDamage(user)), amount(metDamage(mentor)),
                      true, width);
    }
    awlui::EndSection();
    ImGui::Dummy(ImVec2(0, theme::SpaceM));

    // Casts per spell, largest difference first.
    if (awlui::BeginSection(L("benchmark.diff.casts"))) {
        PairTotals<int32_t> casts;
        for (const auto& phase : user.phases) {
            for (const auto& spell : phase.spells) casts[spell.spellId].first += spell.casts;
        }
        for (const auto& phase : mentor.phases) {
            for (const auto& spell : phase.spells) casts[spell.spellId].second += spell.casts;
        }

        const auto rows = largestGapsFirst(casts, kMaxPairRows);

        // All rows are cast counts, so they share one axis.
        const double castPeak = peakOf(rows);

        for (const auto& [spellId, pair] : rows) {
            if (pair.first == 0 && pair.second == 0) continue;
            renderPairBar(spellName(context, spellId).c_str(),
                          static_cast<double>(pair.first),
                          static_cast<double>(pair.second),
                          std::to_string(pair.first),
                          std::to_string(pair.second), true, width, castPeak);
        }
    }
    awlui::EndSection();
}

void UIBenchmarkReportCard::renderCooldownSection(const ReportContext& context) {
    if (!context.userProfile || !context.mentorProfile) return;

    const auto& userUses = context.userProfile->cooldownUses;
    const auto& mentorUses = context.mentorProfile->cooldownUses;
    if (userUses.empty() && mentorUses.empty()) {
        drawHint(L("benchmark.no_cooldowns"));
        return;
    }

    // Overlapping cooldowns are grouped into one burst, so the damage in the
    // shared window is shown once rather than under each spell.
    struct Burst {
        int32_t startMs = 0;
        int32_t endMs = 0;
        std::vector<uint32_t> spellIds;   // In the order they went out
        int64_t damage = 0;
        int32_t casts = 0;
        std::vector<uint32_t> castSequence;
        std::vector<benchmark::ProfileCooldownUse::WindowBuff> buffs;
        std::vector<int64_t> curve;
        std::vector<benchmark::ProfileDamageDone> targets;
    };

    const auto groupBursts = [](const std::vector<benchmark::ProfileCooldownUse>& uses) {
        std::vector<benchmark::ProfileCooldownUse> sorted(uses.begin(), uses.end());
        std::sort(sorted.begin(), sorted.end(),
                  [](const auto& a, const auto& b) {
                      return a.pullOffsetMs < b.pullOffsetMs;
                  });

        std::vector<Burst> bursts;
        for (const auto& use : sorted) {
            const int32_t end = use.pullOffsetMs + use.windowMs;

            // An overlapping use extends the open burst to whichever cooldown
            // ends last, matching the burst/filler split.
            if (!bursts.empty() && use.pullOffsetMs <= bursts.back().endMs) {
                Burst& burst = bursts.back();
                burst.endMs = std::max(burst.endMs, end);
                burst.spellIds.push_back(use.spellId);

                // Take the largest window rather than summing: overlapping
                // uses measured the same damage.
                if (use.damageInWindow > burst.damage) {
                    burst.damage = use.damageInWindow;
                    burst.casts = use.castsInWindow;
                    burst.castSequence = use.castSequence;
                    burst.curve = use.damageCurve;
                    burst.targets = use.damageByTarget;
                }
                for (const auto& buff : use.buffs) {
                    const bool seen = std::any_of(
                        burst.buffs.begin(), burst.buffs.end(),
                        [&](const auto& b) { return b.spellId == buff.spellId; });
                    if (!seen) burst.buffs.push_back(buff);
                }
                continue;
            }

            Burst burst;
            burst.startMs = use.pullOffsetMs;
            burst.endMs = end;
            burst.spellIds.push_back(use.spellId);
            burst.damage = use.damageInWindow;
            burst.casts = use.castsInWindow;
            burst.castSequence = use.castSequence;
            burst.buffs = use.buffs;
            burst.curve = use.damageCurve;
            burst.targets = use.damageByTarget;
            bursts.push_back(std::move(burst));
        }
        return bursts;
    };

    const auto userBursts = groupBursts(userUses);
    const auto mentorBursts = groupBursts(mentorUses);

    drawHint(L("benchmark.burst_hint"));
    ImGui::Dummy(ImVec2(0, theme::SpaceS));
    awlui::Checkbox(L("benchmark.cd_show_buffs"), &showWindowBuffs_);
    if (showWindowBuffs_) {
        ImGui::SameLine();
        awlui::Checkbox(L("benchmark.cd_show_blacklisted"), &showBlacklistedBuffs_);
    }
    ImGui::Dummy(ImVec2(0, theme::SpaceM));

    const auto shortLabel = [&](uint32_t spellId) {
        std::string name = spellName(context, spellId);
        if (name.size() > 10) name = name.substr(0, 9) + ".";
        return name;
    };

    const auto buildCasts = [&](const std::vector<uint32_t>& ids) {
        std::vector<CooldownDetailRow::Cast> out;
        out.reserve(ids.size());
        for (uint32_t id : ids) out.push_back({id, shortLabel(id)});
        return out;
    };

    const auto* spec = ClassificationStore::instance().find(
        context.mentorProfile ? context.mentorProfile->specId : 0);

    const auto buildBuffs =
        [&](const std::vector<benchmark::ProfileCooldownUse::WindowBuff>& buffs,
            int32_t windowMs) {
        std::vector<CooldownDetailRow::WindowBuff> out;
        const float span = windowMs > 0 ? static_cast<float>(windowMs) : 1.0f;
        for (const auto& buff : buffs) {
            if (buff.overlapMs < 1000) continue;

            bool blacklisted = false;
            if (spec) {
                const auto* aura = spec->findAura(buff.spellId);
                blacklisted = aura && aura->confirmed &&
                              aura->role == AuraRole::Unimportant;
            }
            if (blacklisted && !showBlacklistedBuffs_) continue;
            CooldownDetailRow::WindowBuff row;
            row.spellId = buff.spellId;
            row.label = LF("benchmark.cd_buff",
                           spellName(context, buff.spellId).c_str(),
                           buff.overlapMs / 1000.0);
            row.start = buff.offsetMs / span;
            row.end = (buff.offsetMs + buff.overlapMs) / span;
            row.blacklisted = blacklisted;
            out.push_back(std::move(row));
            if (out.size() >= 8) break;
        }
        return out;
    };

    // Top targets for a window, weighted where the user discounted one.
    const auto buildTargets =
        [&](const std::vector<benchmark::ProfileDamageDone>& targets) {
        std::vector<std::string> out;
        auto& weights = MobWeightSettings::instance();
        for (const auto& target : targets) {
            const float weight = weights.weightFor(target.npcId);
            out.push_back(LF("benchmark.window_target",
                             npcName(context, target.npcId).c_str(),
                             ui::format::formatAmount(
                                 static_cast<int64_t>(target.total * weight)).c_str()));
            if (out.size() >= 3) break;
        }
        return out;
    };

    const auto describeOutput = [&](const Burst& burst) {
        const double seconds = (burst.endMs - burst.startMs) / 1000.0;
        const double dps = seconds > 0.0 ? burst.damage / seconds : 0.0;
        return LF("benchmark.cd_output",
                  ui::format::formatAmount(burst.damage).c_str(),
                  ui::format::formatAmount(static_cast<int64_t>(dps)).c_str(),
                  burst.casts);
    };

    const auto describeWindow = [&](const Burst& burst) {
        return LF("benchmark.cd_window",
                  formatDuration(burst.startMs).c_str(),
                  formatDuration(burst.endMs).c_str());
    };

    // Names every cooldown in the burst.
    const auto describeBurst = [&](const Burst& burst) {
        std::string names;
        std::vector<uint32_t> seen;
        for (uint32_t id : burst.spellIds) {
            if (std::find(seen.begin(), seen.end(), id) != seen.end()) continue;
            seen.push_back(id);
            if (!names.empty()) names += " + ";
            names += spellName(context, id);
        }
        return names;
    };

    const auto iconFor = realIconLookup(context.iconLoader);

    const float width = ImGui::GetContentRegionAvail().x;
    const size_t burstCount = std::max(userBursts.size(), mentorBursts.size());

    for (size_t i = 0; i < burstCount; ++i) {
        const Burst* user = i < userBursts.size() ? &userBursts[i] : nullptr;
        const Burst* mentor = i < mentorBursts.size() ? &mentorBursts[i] : nullptr;

        CooldownDetailRow detail;
        detail.title = LF("benchmark.burst_row", static_cast<int>(i + 1),
                          describeBurst(mentor ? *mentor : *user).c_str());

        if (user) {
            detail.userTime = describeWindow(*user);
            detail.userOutput = describeOutput(*user);
            detail.userCasts = buildCasts(user->castSequence);
            detail.userBuffs = buildBuffs(user->buffs, user->endMs - user->startMs);
            for (int64_t v : user->curve) detail.userCurve.push_back(static_cast<double>(v));
            detail.userTargets = buildTargets(user->targets);
        }
        if (mentor) {
            detail.mentorTime = describeWindow(*mentor);
            detail.mentorOutput = describeOutput(*mentor);
            detail.mentorCasts = buildCasts(mentor->castSequence);
            detail.mentorBuffs = buildBuffs(mentor->buffs, mentor->endMs - mentor->startMs);
            for (int64_t v : mentor->curve) detail.mentorCurve.push_back(static_cast<double>(v));
            detail.mentorTargets = buildTargets(mentor->targets);
        }

        const uint32_t blacklistRequest =
            DrawCooldownDetail(detail, context.userName.c_str(),
                               context.mentorName.c_str(), iconFor,
                               showWindowBuffs_, width);
        if (blacklistRequest != 0) pendingBlacklist_ = blacklistRequest;

        // Both windows plotted from the moment each opened.
        if (!detail.userCurve.empty() || !detail.mentorCurve.empty()) {
            // A fixed id rather than a translated string, so a translation
            // cannot change the plot's id.
            char plotId[32];
            std::snprintf(plotId, sizeof(plotId), "##windowcurve%d",
                          static_cast<int>(i));
            DrawWindowCurve(plotId,
                            detail.userCurve, detail.mentorCurve,
                            context.userName.c_str(), context.mentorName.c_str(),
                            ImVec2(width, 160.0f));
        }

        ImGui::Dummy(ImVec2(0, theme::SpaceS));
    }
}

void UIBenchmarkReportCard::renderThroughputSection(const ReportContext& context) {
    if (!context.userProfile || !context.mentorProfile) return;

    const auto& user = *context.userProfile;
    const auto& mentor = *context.mentorProfile;

    drawHint(L("benchmark.throughput_hint"));
    ImGui::Dummy(ImVec2(0, theme::SpaceM));

    struct Source {
        uint32_t spellId = 0;
        int64_t userDamage = 0;
        int64_t mentorDamage = 0;
    };

    // Keyed by the spell that dealt the damage, not the one cast: most damage
    // arrives under ids that are never cast directly.
    std::unordered_map<uint32_t, Source> sources;
    for (const auto& phase : user.phases) {
        for (const auto& src : phase.damageBySpell) {
            auto& row = sources[src.spellId];
            row.spellId = src.spellId;
            row.userDamage += src.total;
        }
    }
    for (const auto& phase : mentor.phases) {
        for (const auto& src : phase.damageBySpell) {
            auto& row = sources[src.spellId];
            row.spellId = src.spellId;
            row.mentorDamage += src.total;
        }
    }

    int64_t userTotal = 0;
    int64_t mentorTotal = 0;
    std::vector<Source> rows;
    for (const auto& entry : sources) {
        if (entry.second.userDamage <= 0 && entry.second.mentorDamage <= 0) continue;
        userTotal += entry.second.userDamage;
        mentorTotal += entry.second.mentorDamage;
        rows.push_back(entry.second);
    }

    if (rows.empty()) {
        awlui::EmptyState("##nosources", "=", L("benchmark.no_sources"),
                          L("benchmark.no_sources_hint"));
        return;
    }

    std::sort(rows.begin(), rows.end(),
              [](const Source& a, const Source& b) {
                  return std::abs(a.userDamage - a.mentorDamage) >
                         std::abs(b.userDamage - b.mentorDamage);
              });
    if (rows.size() > 14) rows.resize(14);

    const float width = ImGui::GetContentRegionAvail().x;

    std::vector<CompositionSlice> slices;
    for (const auto& row : rows) {
        CompositionSlice slice;
        slice.label = spellName(context, row.spellId);
        slice.userValue = static_cast<double>(row.userDamage);
        slice.mentorValue = static_cast<double>(row.mentorDamage);
        slices.push_back(std::move(slice));
    }

    DrawCompositionRings("##sourcering", std::move(slices),
                         context.userName.c_str(), context.mentorName.c_str(),
                         [](double v) {
                             return ui::format::formatAmount(static_cast<int64_t>(v));
                         },
                         ImVec2(width, 210.0f));
}

void UIBenchmarkReportCard::renderBreakdownSection(const ReportContext& context) {
    if (!context.userProfile || !context.mentorProfile) return;

    const auto& user = *context.userProfile;
    const auto& mentor = *context.mentorProfile;

    drawHint(L("benchmark.breakdown_hint"));
    ImGui::Dummy(ImVec2(0, theme::SpaceM));

    const float width = ImGui::GetContentRegionAvail().x;
    const auto amount = [](int64_t v) { return ui::format::formatAmount(v); };

    // Dps per phase. Phase rules come from the overlay's phase settings.
    if (user.phases.size() > 1 || mentor.phases.size() > 1) {
        if (awlui::BeginSection(L("benchmark.section.phases"))) {
            const size_t phaseCount = std::max(user.phases.size(),
                                               mentor.phases.size());

            // Phases share one scale.
            const auto phaseDps = [](const benchmark::ProfilePhase& phase) {
                return phase.durationMs > 0
                    ? static_cast<double>(phase.totalDamageDone) * 1000.0 /
                          phase.durationMs
                    : 0.0;
            };

            double phasePeak = 1.0;
            for (const auto& phase : user.phases) {
                phasePeak = std::max(phasePeak, phaseDps(phase));
            }
            for (const auto& phase : mentor.phases) {
                phasePeak = std::max(phasePeak, phaseDps(phase));
            }

            for (size_t i = 0; i < phaseCount; ++i) {
                const auto* userPhase = i < user.phases.size() ? &user.phases[i]
                                                              : nullptr;
                const auto* mentorPhase = i < mentor.phases.size()
                    ? &mentor.phases[i] : nullptr;

                std::string label = userPhase ? userPhase->label
                                   : mentorPhase ? mentorPhase->label
                                   : std::string();
                if (label.empty()) label = LF("benchmark.phase_index",
                                              static_cast<int>(i + 1));

                const double userDps = userPhase ? phaseDps(*userPhase) : 0.0;
                const double mentorDps = mentorPhase ? phaseDps(*mentorPhase) : 0.0;

                renderPairBar(label.c_str(), userDps, mentorDps,
                              amount(static_cast<int64_t>(userDps)),
                              amount(static_cast<int64_t>(mentorDps)),
                              true, width, phasePeak);
            }

            // A phase rule that never fired leaves one side missing a phase,
            // which is not a difference in play.
            if (!user.unresolvedPhaseLabels.empty() ||
                !mentor.unresolvedPhaseLabels.empty()) {
                ImGui::PushStyleColor(ImGuiCol_Text, theme::Warn);
                ImGui::TextWrapped("%s", L("benchmark.phases_unresolved"));
                ImGui::PopStyleColor();
            }
        }
        awlui::EndSection();
        ImGui::Dummy(ImVec2(0, theme::SpaceM));
    }

    // Damage by target. Mob weights are shown on the label so a discounted
    // add does not look like missing damage.
    {
        PairTotals<int64_t> byNpc;
        for (const auto& phase : user.phases) {
            for (const auto& done : phase.damageDone) byNpc[done.npcId].first += done.total;
        }
        for (const auto& phase : mentor.phases) {
            for (const auto& done : phase.damageDone) byNpc[done.npcId].second += done.total;
        }

        const auto rows = largestGapsFirst(byNpc, kMaxPairRows);

        // EndSection must follow every BeginSection call, whatever it
        // returns, so an empty list skips both.
        if (!rows.empty()) {
            if (awlui::BeginSection(L("benchmark.section.targets"))) {
                auto& weights = MobWeightSettings::instance();
                std::vector<CompositionSlice> slices;
                for (const auto& [npcId, pair] : rows) {
                    CompositionSlice slice;
                    slice.label = npcName(context, npcId);
                    const float weight = weights.weightFor(npcId);
                    if (weight < 0.999f) {
                        slice.label += LF("benchmark.weight_suffix", weight * 100.0);
                    }
                    slice.userValue = static_cast<double>(pair.first);
                    slice.mentorValue = static_cast<double>(pair.second);
                    slices.push_back(std::move(slice));
                }

                DrawCompositionRings("##targetring", std::move(slices),
                                     context.userName.c_str(),
                                     context.mentorName.c_str(),
                                     [](double v) {
                                         return ui::format::formatAmount(
                                             static_cast<int64_t>(v));
                                     },
                                     ImVec2(width, 190.0f));
            }
            awlui::EndSection();
            ImGui::Dummy(ImVec2(0, theme::SpaceM));
        }
    }

    {
        PairTotals<int64_t> taken;
        for (const auto& phase : user.phases) {
            for (const auto& hit : phase.damageTaken) taken[hit.spellId].first += hit.total;
        }
        for (const auto& phase : mentor.phases) {
            for (const auto& hit : phase.damageTaken) taken[hit.spellId].second += hit.total;
        }

        const auto rows = largestGapsFirst(taken, kMaxPairRows);

        // Same BeginSection/EndSection pairing as the targets section.
        if (!rows.empty()) {
            if (awlui::BeginSection(L("benchmark.section.taken_by_spell"))) {
                const double takenPeak = peakOf(rows);
                for (const auto& [spellId, pair] : rows) {
                    renderPairBar(spellName(context, spellId).c_str(),
                                  static_cast<double>(pair.first),
                                  static_cast<double>(pair.second),
                                  amount(pair.first), amount(pair.second), false,
                                  width, takenPeak);
                }
            }
            awlui::EndSection();
            ImGui::Dummy(ImVec2(0, theme::SpaceM));
        }
    }

    // Downtime, listed as individual gaps with timestamps.
    {
        const auto totalGap = [](const benchmark::MentorProfile& p) {
            int32_t total = 0;
            for (const auto& gap : p.downtimeGaps) total += gap.durationMs;
            return total;
        };
        const auto inCooldownGap = [](const benchmark::MentorProfile& p) {
            int32_t total = 0;
            for (const auto& gap : p.downtimeGaps) {
                if (gap.inCooldown) total += gap.durationMs;
            }
            return total;
        };

        if (awlui::BeginSection(L("benchmark.section.downtime"))) {
            drawHint(L("benchmark.downtime_hint"));

            // Combat logs record no channel events, and some abilities use a
            // global without logging a cast; both show up as downtime. Ruling
            // the buff a Channel fixes the first, the second cannot be
            // recovered from the log, so the caveat is always shown.
            ImGui::PushStyleColor(ImGuiCol_Text, theme::Warn);
            ImGui::TextWrapped("%s", L("benchmark.downtime_caveat"));
            ImGui::PopStyleColor();
            ImGui::Dummy(ImVec2(0, theme::SpaceS));

            renderPairBar(L("benchmark.row.downtime"),
                          static_cast<double>(totalGap(user)),
                          static_cast<double>(totalGap(mentor)),
                          LF("benchmark.downtime_count",
                             formatDuration(totalGap(user)).c_str(),
                             static_cast<int>(user.downtimeGaps.size())),
                          LF("benchmark.downtime_count",
                             formatDuration(totalGap(mentor)).c_str(),
                             static_cast<int>(mentor.downtimeGaps.size())),
                          false, width);

            // Downtime inside a cooldown costs more than in filler, so it is
            // shown separately.
            renderPairBar(L("benchmark.row.downtime_burst"),
                          static_cast<double>(inCooldownGap(user)),
                          static_cast<double>(inCooldownGap(mentor)),
                          formatDuration(inCooldownGap(user)),
                          formatDuration(inCooldownGap(mentor)),
                          false, width);

            ImGui::Dummy(ImVec2(0, theme::SpaceM));
            ImGui::PushStyleColor(ImGuiCol_Text, theme::TextSecondary);
            ImGui::TextUnformatted(LF("benchmark.downtime_worst",
                                      context.userName.c_str()).c_str());
            ImGui::PopStyleColor();
            ImGui::Dummy(ImVec2(0, theme::SpaceXS));

            const auto iconFor = realIconLookup(context.iconLoader);
            const auto shortLabel = [&](uint32_t spellId) {
                return chipLabel(spellName(context, spellId));
            };

            size_t shown = 0;
            for (const auto& gap : user.downtimeGaps) {
                if (shown++ >= 6) break;

                DowntimeStrip strip;
                strip.gapMs = gap.durationMs;
                strip.inCooldown = gap.inCooldown;
                strip.caption = LF("benchmark.downtime_gap",
                                   formatDuration(gap.startMs).c_str(),
                                   gap.durationMs / 1000.0);

                // Debuffs only, as context for why the player stopped.
                std::string context_text;
                for (uint32_t debuffId : gap.debuffSpellIds) {
                    if (context_text.size() > 60) break;
                    if (!context_text.empty()) context_text += "  -  ";
                    context_text += spellName(context, debuffId);
                }
                strip.context = context_text;

                for (const auto& prior : gap.leadIn) {
                    DowntimeStrip::Cast cast;
                    cast.spellId = prior.spellId;
                    cast.occupancyMs = prior.occupancyMs;
                    cast.shortLabel = shortLabel(prior.spellId);
                    strip.leadIn.push_back(std::move(cast));
                }

                strip.resumedSpellId = gap.afterSpellId;
                strip.resumedLabel = shortLabel(gap.afterSpellId);

                ImGui::PushID(static_cast<int>(shown));
                DrawDowntimeStrip("##gapstrip", strip, iconFor, width);
                ImGui::PopID();
            }

            if (user.downtimeGaps.empty()) {
                ImGui::PushStyleColor(ImGuiCol_Text, theme::Success);
                ImGui::TextUnformatted(L("benchmark.downtime_none"));
                ImGui::PopStyleColor();
            }
        }
        awlui::EndSection();
    }
}


namespace {

// Trailing window for each point on the dps line. Long enough to smooth over
// a single global, short enough that a lull still shows as a dip.
constexpr int32_t kDpsWindowMs = 30000;

// A bucket's real span, taken from the next bucket's start. The last bucket
// is usually partial, and the nominal size would show a false dps drop.
double bucketSeconds(const std::vector<benchmark::ProfileTimelineBucket>& buckets,
                     size_t index) {
    if (index + 1 < buckets.size()) {
        return (buckets[index + 1].startMs - buckets[index].startMs) / 1000.0;
    }
    if (index > 0) {
        return (buckets[index].startMs - buckets[index - 1].startMs) / 1000.0;
    }
    return 0.0;
}

} // namespace

void UIBenchmarkReportCard::renderMovementSection(const ReportContext& context) {
    if (!context.userProfile || !context.mentorProfile) return;

    const auto& user = *context.userProfile;
    const auto& mentor = *context.mentorProfile;

    if (user.positions.empty() && mentor.positions.empty()) {
        awlui::EmptyState("##nopositions", "=", L("benchmark.no_positions"),
                          L("benchmark.no_positions_hint"));
        return;
    }

    drawHint(L("benchmark.movement_hint"));
    ImGui::Dummy(ImVec2(0, theme::SpaceS));

    awlui::Checkbox(L("benchmark.movement_relative"), &movementRelative_);
    ImGui::Dummy(ImVec2(0, theme::SpaceS));

    const auto pathOf = [&](const benchmark::MentorProfile& profile) {
        PositionPath path;
        for (const auto& pos : profile.positions) {
            path.timeMs.push_back(pos.timeMs);
            if (movementRelative_) {
                // Skip samples with no boss position rather than placing them
                // at the origin, which would read as standing on the boss.
                if (!pos.hasBoss) {
                    path.timeMs.pop_back();   // Kept in step with x/y
                    continue;
                }
                path.x.push_back(pos.x - pos.bossX);
                path.y.push_back(pos.y - pos.bossY);
            } else {
                path.x.push_back(pos.x);
                path.y.push_back(pos.y);
            }
        }
        return path;
    };

    const int32_t duration = std::max(user.pullDurationMs, mentor.pullDurationMs);

    static constexpr int32_t kTrailOptions[] = {0, 5000, 10000, 20000, 30000};
    static constexpr const char* kTrailKeys[] = {
        "benchmark.trail.full", "benchmark.trail.5s", "benchmark.trail.10s",
        "benchmark.trail.20s", "benchmark.trail.30s",
    };
    static constexpr float kSpeeds[] = {1.0f, 2.0f, 4.0f, 8.0f, 20.0f, 50.0f};

    if (awlui::Button(moviePlaying_ ? L("benchmark.movie.pause")
                                    : L("benchmark.movie.play"),
                      awlui::ButtonVariant::Primary, awlui::ButtonSize::Sm)) {
        moviePlaying_ = !moviePlaying_;
        // Pressing play at the end restarts from the beginning.
        if (moviePlaying_ && movieTimeMs_ >= duration) movieTimeMs_ = 0;
    }

    ImGui::SameLine();
    if (awlui::Button(L("benchmark.movie.restart"),
                      awlui::ButtonVariant::Secondary, awlui::ButtonSize::Sm)) {
        movieTimeMs_ = 0;
    }

    ImGui::SameLine();
    ImGui::SetNextItemWidth(90.0f);
    int speedIndex = 0;
    for (int i = 0; i < IM_ARRAYSIZE(kSpeeds); ++i) {
        if (std::abs(kSpeeds[i] - movieSpeed_) < 0.01f) speedIndex = i;
    }
    std::string speedLabels[IM_ARRAYSIZE(kSpeeds)];
    const char* speedPtrs[IM_ARRAYSIZE(kSpeeds)];
    for (int i = 0; i < IM_ARRAYSIZE(kSpeeds); ++i) {
        speedLabels[i] = LF("benchmark.movie.speed", kSpeeds[i]);
        speedPtrs[i] = speedLabels[i].c_str();
    }
    if (awlui::Combo("##moviespeed", &speedIndex, speedPtrs, IM_ARRAYSIZE(kSpeeds))) {
        movieSpeed_ = kSpeeds[speedIndex];
    }

    ImGui::SameLine();
    ImGui::SetNextItemWidth(120.0f);
    const char* trailPtrs[IM_ARRAYSIZE(kTrailOptions)];
    for (int i = 0; i < IM_ARRAYSIZE(kTrailOptions); ++i) {
        trailPtrs[i] = L(kTrailKeys[i]);
    }
    awlui::Combo("##movietrail", &movieTrailIndex_, trailPtrs,
                 IM_ARRAYSIZE(kTrailOptions));

    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 90.0f);
    int scrub = movieTimeMs_;
    if (ImGui::SliderInt("##moviescrub", &scrub, 0, std::max(duration, 1),
                         formatDuration(movieTimeMs_).c_str())) {
        movieTimeMs_ = scrub;
        moviePlaying_ = false;
    }

    if (moviePlaying_) {
        movieTimeMs_ += static_cast<int32_t>(ImGui::GetIO().DeltaTime * 1000.0f *
                                             movieSpeed_);
        if (movieTimeMs_ >= duration) {
            movieTimeMs_ = duration;
            moviePlaying_ = false;
        }
    }

    ImGui::Dummy(ImVec2(0, theme::SpaceS));

    PositionPlayback playback;
    playback.active = moviePlaying_ || movieTimeMs_ > 0;
    playback.currentMs = movieTimeMs_;
    playback.trailMs = kTrailOptions[movieTrailIndex_];

    const float width = ImGui::GetContentRegionAvail().x;
    DrawPositionMap("##positions", pathOf(user), pathOf(mentor),
                    context.userName.c_str(), context.mentorName.c_str(),
                    movementRelative_, playback, ImVec2(width, 420.0f));

    // Distance travelled; more running usually means less casting.
    const auto distance = [&](const benchmark::MentorProfile& profile) {
        double total = 0.0;
        for (size_t i = 1; i < profile.positions.size(); ++i) {
            const double dx = profile.positions[i].x - profile.positions[i - 1].x;
            const double dy = profile.positions[i].y - profile.positions[i - 1].y;
            total += std::sqrt(dx * dx + dy * dy);
        }
        return total;
    };

    ImGui::Dummy(ImVec2(0, theme::SpaceM));
    renderPairBar(L("benchmark.movement_distance"),
                  distance(user), distance(mentor),
                  LF("benchmark.movement_yards", distance(user)),
                  LF("benchmark.movement_yards", distance(mentor)),
                  false, width);
}

void UIBenchmarkReportCard::renderTimelineSection(const ReportContext& context) {
    if (!context.userProfile || !context.mentorProfile) return;

    const auto& user = *context.userProfile;
    const auto& mentor = *context.mentorProfile;
    const auto& userTimeline = user.timeline;
    const auto& mentorTimeline = mentor.timeline;
    if (userTimeline.empty() || mentorTimeline.empty()) return;

    const size_t count = std::min(userTimeline.size(), mentorTimeline.size());
    if (count < 2) return;

    // Optional filter to one of the reference's burst windows.
    std::vector<std::string> windowLabels;
    std::vector<std::pair<int32_t, int32_t>> windowSpans;   // start, end
    windowLabels.push_back(L("benchmark.window.all"));
    windowSpans.emplace_back(0, 0);

    const auto& mentorUses = context.mentorProfile->cooldownUses;
    for (size_t i = 0; i < mentorUses.size(); ++i) {
        const auto& use = mentorUses[i];
        windowLabels.push_back(LF("benchmark.window.entry",
                                  static_cast<int>(i + 1),
                                  spellName(context, use.spellId).c_str()));
        windowSpans.emplace_back(use.pullOffsetMs, use.pullOffsetMs + use.windowMs);
    }

    if (windowLabels.size() > 1) {
        std::vector<const char*> labelPtrs;
        labelPtrs.reserve(windowLabels.size());
        for (const auto& label : windowLabels) labelPtrs.push_back(label.c_str());

        if (timelineWindow_ >= static_cast<int>(labelPtrs.size())) timelineWindow_ = 0;
        ImGui::SetNextItemWidth(280.0f);
        awlui::Combo("##timelinewindow", &timelineWindow_,
                     labelPtrs.data(), static_cast<int>(labelPtrs.size()));
        ImGui::Dummy(ImVec2(0, theme::SpaceS));
    }

    const bool filtered = timelineWindow_ > 0 &&
                          timelineWindow_ < static_cast<int>(windowSpans.size());
    const int32_t filterStart = filtered ? windowSpans[timelineWindow_].first : 0;
    const int32_t filterEnd = filtered ? windowSpans[timelineWindow_].second : 0;

    std::vector<double> times, userTotal, mentorTotal;
    std::vector<double> userCumulative, mentorCumulative;
    times.reserve(count);
    userTotal.reserve(count);
    mentorTotal.reserve(count);
    userCumulative.reserve(count);
    mentorCumulative.reserve(count);

    // Dps over a trailing window ending at each point. Dps since pull start
    // flattens out late in the pull and hides late lulls, and raw per-bucket
    // dps is too noisy to compare.
    const auto windowedDps = [](const std::vector<benchmark::ProfileTimelineBucket>& buckets,
                                size_t index) {
        const int32_t endMs = buckets[index].startMs +
                              static_cast<int32_t>(bucketSeconds(buckets, index) * 1000.0);
        const int32_t startMs = std::max(endMs - kDpsWindowMs, 0);

        int64_t damage = 0;
        for (size_t i = index + 1; i-- > 0;) {
            if (buckets[i].startMs < startMs) break;
            damage += buckets[i].damageDone;
        }

        // Early in the pull the window is not full yet, so divide by the time
        // actually covered.
        const double spanSec = (endMs - startMs) / 1000.0;
        return spanSec > 0.0 ? damage / spanSec : 0.0;
    };

    int64_t userRunning = 0;
    int64_t mentorRunning = 0;
    for (size_t i = 0; i < count; ++i) {
        // Accumulated for every bucket, including filtered ones, so the running
        // total stays relative to the pull start.
        userRunning += userTimeline[i].damageDone;
        mentorRunning += mentorTimeline[i].damageDone;

        // Buckets outside the window are dropped rather than zeroed so the
        // axis rescales to the window.
        if (filtered) {
            const int32_t bucketStart = userTimeline[i].startMs;
            if (bucketStart < filterStart || bucketStart > filterEnd) continue;
        }

        times.push_back(userTimeline[i].startMs / 1000.0);
        userTotal.push_back(windowedDps(userTimeline, i));
        mentorTotal.push_back(windowedDps(mentorTimeline, i));

        userCumulative.push_back(static_cast<double>(userRunning));
        mentorCumulative.push_back(static_cast<double>(mentorRunning));
    }

    // Cooldown uses for both players on a shared time axis.
    {
        const auto marksFor = [&](const benchmark::MentorProfile& profile) {
            std::vector<CooldownMark> marks;
            for (const auto& use : profile.cooldownUses) {
                CooldownMark mark;
                mark.timeMs = use.pullOffsetMs;
                mark.windowMs = use.windowMs;
                mark.spellId = use.spellId;
                mark.shortLabel = chipLabel(spellName(context, use.spellId));
                marks.push_back(std::move(mark));
            }
            return marks;
        };

        const auto iconFor = realIconLookup(context.iconLoader);
        const int32_t duration = std::max(user.pullDurationMs, mentor.pullDurationMs);
        DrawCooldownTimelineRows("##cdtimeline", marksFor(user), marksFor(mentor),
                                 duration, context.userName.c_str(),
                                 context.mentorName.c_str(), iconFor,
                                 ImVec2(ImGui::GetContentRegionAvail().x, 0));
        ImGui::Dummy(ImVec2(0, theme::SpaceM));
    }

    drawHint(L("benchmark.timeline_hint"));
    ImGui::Dummy(ImVec2(0, theme::SpaceS));

    DrawComparisonLine("##damageovertime",
                       context.userName.c_str(),
                       context.mentorName.c_str(),
                       times, userTotal, mentorTotal,
                       ImVec2(ImGui::GetContentRegionAvail().x, 260.0f));

    ImGui::Dummy(ImVec2(0, theme::SpaceM));
    ImGui::PushStyleColor(ImGuiCol_Text, theme::TextMuted);
    ImGui::TextUnformatted(L("benchmark.strip.cumulative"));
    ImGui::PopStyleColor();
    DrawComparisonLine("##cumulative",
                       context.userName.c_str(),
                       context.mentorName.c_str(),
                       times, userCumulative, mentorCumulative,
                       ImVec2(ImGui::GetContentRegionAvail().x, 150.0f));

    // Bucket where the reference pulled furthest ahead.
    size_t worstIndex = 0;
    int64_t worstDrop = 0;
    for (size_t i = 1; i < count; ++i) {
        const int64_t bucketGap =
            mentorTimeline[i].damageDone - userTimeline[i].damageDone;
        if (bucketGap > worstDrop) {
            worstDrop = bucketGap;
            worstIndex = i;
        }
    }

    // What each player cast inside the chosen window.
    if (filtered) {
        const auto castsIn = [&](const benchmark::MentorProfile& profile) {
            for (const auto& use : profile.cooldownUses) {
                if (use.pullOffsetMs != filterStart) continue;
                return use.castSequence;
            }
            // The user's window rarely starts on the same millisecond, so fall
            // back to whichever of their uses overlaps this span.
            for (const auto& use : profile.cooldownUses) {
                const int32_t end = use.pullOffsetMs + use.windowMs;
                if (end > filterStart && use.pullOffsetMs < filterEnd) {
                    return use.castSequence;
                }
            }
            return std::vector<uint32_t>{};
        };

        const auto listCasts = [&](const char* who, const std::vector<uint32_t>& ids) {
            ImGui::PushStyleColor(ImGuiCol_Text, theme::TextSecondary);
            ImGui::TextUnformatted(who);
            ImGui::PopStyleColor();
            if (ids.empty()) {
                ImGui::PushStyleColor(ImGuiCol_Text, theme::TextMuted);
                ImGui::TextUnformatted(L("benchmark.window.no_casts"));
                ImGui::PopStyleColor();
                return;
            }
            // Counts per spell; the order is already shown on the cooldown tab.
            std::vector<std::pair<uint32_t, int>> counts;
            for (uint32_t id : ids) {
                auto it = std::find_if(counts.begin(), counts.end(),
                                       [&](const auto& c) { return c.first == id; });
                if (it == counts.end()) counts.emplace_back(id, 1);
                else ++it->second;
            }
            std::sort(counts.begin(), counts.end(),
                      [](const auto& a, const auto& b) { return a.second > b.second; });
            for (const auto& [id, n] : counts) {
                ImGui::BulletText("%s", LF("benchmark.window.cast_count",
                                           spellName(context, id).c_str(), n).c_str());
            }
        };

        ImGui::Dummy(ImVec2(0, theme::SpaceM));
        if (awlui::BeginSection(L("benchmark.window.casts_heading"))) {
            listCasts(context.userName.c_str(), castsIn(*context.userProfile));
            ImGui::Dummy(ImVec2(0, theme::SpaceS));
            listCasts(context.mentorName.c_str(), castsIn(*context.mentorProfile));
        }
        awlui::EndSection();
    }

    if (worstDrop > 0) {
        ImGui::Dummy(ImVec2(0, theme::SpaceS));
        ImGui::PushStyleColor(ImGuiCol_Text, theme::TextSecondary);
        ImGui::TextUnformatted(
            LF("benchmark.timeline_worst",
               formatDuration(userTimeline[worstIndex].startMs).c_str(),
               ui::format::formatAmount(worstDrop).c_str()).c_str());
        ImGui::PopStyleColor();
    }
}

ReportAction UIBenchmarkReportCard::render(const ReportContext& context) {
    ReportAction action = ReportAction::None;
    if (!visible_ || !context.result) return action;

    if (justOpened_) {
        ImGui::SetNextWindowFocus();
        justOpened_ = false;
    }

    // The icon loader's mapping table is normally filled by the encounter
    // loader, which the report flows skip, so request the ids here once per
    // open. Without it the cast sequences fall back to text chips.
    if (!iconsPreloaded_ && context.iconLoader) {
        std::unordered_set<uint32_t> iconIds;
        const auto collect = [&](const benchmark::MentorProfile* profile) {
            if (!profile) return;
            for (const auto& use : profile->cooldownUses) {
                iconIds.insert(use.spellId);
                iconIds.insert(use.castSequence.begin(), use.castSequence.end());
                for (const auto& buff : use.buffs) iconIds.insert(buff.spellId);
            }
        };
        collect(context.userProfile);
        collect(context.mentorProfile);
        iconIds.erase(0);
        if (!iconIds.empty()) {
            context.iconLoader->loadMappingsForIds(iconIds);
        }
        iconsPreloaded_ = true;
    }

    // Fills the application window rather than floating.
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);

    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoMove |
                                   ImGuiWindowFlags_NoResize |
                                   ImGuiWindowFlags_NoCollapse |
                                   ImGuiWindowFlags_NoSavedSettings |
                                   ImGuiWindowFlags_NoBringToFrontOnFocus;

    if (ImGui::Begin(L("benchmark.title"), &visible_, flags)) {
        // Above the blocked check so it also shows while waiting for the
        // first pull.
        if (!context.liveStatus.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, theme::TextMuted);
            ImGui::TextUnformatted(context.liveStatus.c_str());
            ImGui::PopStyleColor();
            ImGui::Dummy(ImVec2(0, theme::SpaceS));
        }

        if (!context.result->valid) {
            renderBlocked(context, action);
            ImGui::End();
            return action;
        }

        // Applied here rather than in the section that raised it, because
        // changing a role rebuilds the store's lists that section iterates.
        if (pendingBlacklist_ != 0) {
            auto& store = benchmark::ClassificationStore::instance();
            const uint16_t specId =
                context.mentorProfile ? context.mentorProfile->specId : 0;
            if (const auto* spec = store.find(specId)) {
                const auto* aura = spec->findAura(pendingBlacklist_);
                const bool already = aura && aura->confirmed &&
                                     aura->role == AuraRole::Unimportant;

                // ruleUnlistedAura rather than confirmAura: most buffs in a
                // cooldown window come from other players and have no entry
                // to update.
                //
                // Right-clicking a blacklisted buff removes it from the list.
                // It goes back to Unclassified, not CooldownBuff.
                store.ruleUnlistedAura(specId, pendingBlacklist_,
                                       already ? AuraRole::Unclassified
                                               : AuraRole::Unimportant);
            }
            pendingBlacklist_ = 0;
        }

        renderHeadline(context);
        if (pendingAction_ != ReportAction::None) {
            action = pendingAction_;
            pendingAction_ = ReportAction::None;
        }
        ImGui::Dummy(ImVec2(0, theme::SpaceM));

        static const char* const kTabs[] = {
            "benchmark.tab.findings",
            "benchmark.tab.cooldowns",
            "benchmark.tab.damage",
            "benchmark.tab.movement",
            "benchmark.tab.timeline",
        };
        constexpr int kTabCount = IM_ARRAYSIZE(kTabs);
        const char* tabLabels[kTabCount];
        for (int i = 0; i < kTabCount; ++i) tabLabels[i] = L(kTabs[i]);

        awlui::TabBar("##benchmarktabs", &activeTab_, tabLabels, kTabCount);
        ImGui::Dummy(ImVec2(0, theme::SpaceS));

        ImGui::BeginChild("##benchmarkbody", ImVec2(0, -44.0f));
        switch (activeTab_) {
            case 0: renderFindings(context, action); break;
            case 1: renderCooldownSection(context); break;
            case 2:
                renderThroughputSection(context);
                ImGui::Dummy(ImVec2(0, theme::SpaceL));
                renderBreakdownSection(context);
                break;
            case 3: renderMovementSection(context); break;
            case 4: renderTimelineSection(context); break;
            default: break;
        }
        ImGui::EndChild();

        ImGui::Separator();
        if (awlui::Button(L("benchmark.action.open_replay"),
                          awlui::ButtonVariant::Primary)) {
            action = ReportAction::OpenReplay;
        }
        ImGui::SameLine();
        if (awlui::Button(L("benchmark.action.change_mentor"),
                          awlui::ButtonVariant::Secondary)) {
            action = ReportAction::ChangeMentor;
        }
        ImGui::SameLine();
        if (awlui::Button(L("benchmark.action.review_classes"),
                          awlui::ButtonVariant::Secondary)) {
            action = ReportAction::ReviewClasses;
        }
        ImGui::SameLine();
        if (awlui::Button(L("benchmark.action.dismiss"),
                          awlui::ButtonVariant::Ghost)) {
            action = ReportAction::Dismiss;
            visible_ = false;
        }
    }
    ImGui::End();

    return action;
}

} // namespace benchmark_ui
