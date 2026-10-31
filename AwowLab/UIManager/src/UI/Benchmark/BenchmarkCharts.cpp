#include "UI/Benchmark/BenchmarkCharts.h"

#include "Core/LocalizationManager.h"
#include "UI/AwlUI/Theme.h"

#include <imgui_internal.h>   // IM_PI
#include <implot.h>

#include <algorithm>
#include <cstdio>
#include <string>

namespace benchmark_ui {

namespace {

using namespace awlui;

// Neutral tone for the reference's series.
constexpr ImVec4 kMentorLine  = ImVec4(0.55f, 0.60f, 0.68f, 1.00f);
constexpr ImVec4 kBehindFill  = ImVec4(0.90f, 0.35f, 0.35f, 0.20f);

ImU32 toU32(const ImVec4& c) { return ImGui::ColorConvertFloat4ToU32(c); }

// Inside an open plot: the gap between the two series shaded, then the
// reference's line, then the user's on top.
void plotUserAgainstMentor(const char* gapId,
                           const double* xs,
                           const double* userValues,
                           const double* mentorValues,
                           int count,
                           const char* userLabel,
                           const char* mentorLabel) {
    ImPlotSpec gapSpec;
    gapSpec.FillColor = kBehindFill;
    gapSpec.FillAlpha = 1.0f;
    gapSpec.Flags = ImPlotItemFlags_NoLegend;
    ImPlot::PlotShaded(gapId, xs, userValues, mentorValues, count, gapSpec);

    ImPlotSpec mentorSpec;
    mentorSpec.LineColor = kMentorLine;
    mentorSpec.LineWeight = 2.0f;
    ImPlot::PlotLine(mentorLabel, xs, mentorValues, count, mentorSpec);

    ImPlotSpec userSpec;
    userSpec.LineColor = theme::Accent;
    userSpec.LineWeight = 2.5f;
    ImPlot::PlotLine(userLabel, xs, userValues, count, userSpec);
}

} // namespace

void PushPlotStyle() {
    ImPlot::PushStyleColor(ImPlotCol_FrameBg, theme::Surface1);
    ImPlot::PushStyleColor(ImPlotCol_PlotBg, theme::Surface1);
    ImPlot::PushStyleColor(ImPlotCol_PlotBorder, theme::BorderSubtle);
    ImPlot::PushStyleColor(ImPlotCol_LegendBg, theme::Surface2);
    ImPlot::PushStyleColor(ImPlotCol_LegendBorder, theme::BorderSubtle);
    ImPlot::PushStyleColor(ImPlotCol_LegendText, theme::TextSecondary);
    ImPlot::PushStyleColor(ImPlotCol_AxisText, theme::TextMuted);
    ImPlot::PushStyleColor(ImPlotCol_AxisGrid, theme::BorderSubtle);
    ImPlot::PushStyleColor(ImPlotCol_AxisBgHovered, theme::Surface2);
    ImPlot::PushStyleColor(ImPlotCol_AxisBgActive, theme::Surface3);

    ImPlot::PushStyleVar(ImPlotStyleVar_PlotPadding, ImVec2(theme::SpaceS, theme::SpaceS));
    ImPlot::PushStyleVar(ImPlotStyleVar_LabelPadding, ImVec2(theme::SpaceXS, theme::SpaceXS));
    ImPlot::PushStyleVar(ImPlotStyleVar_LegendPadding, ImVec2(theme::SpaceS, theme::SpaceS));
    ImPlot::PushStyleVar(ImPlotStyleVar_PlotBorderSize, 0.0f);
}

void PopPlotStyle() {
    ImPlot::PopStyleVar(4);
    ImPlot::PopStyleColor(10);
}

void DrawComparisonLine(const char* id,
                        const char* userLabel,
                        const char* mentorLabel,
                        const std::vector<double>& timesSec,
                        const std::vector<double>& userValues,
                        const std::vector<double>& mentorValues,
                        ImVec2 size) {
    if (timesSec.empty()) return;

    const int count = static_cast<int>(std::min({timesSec.size(), userValues.size(),
                                                 mentorValues.size()}));
    if (count <= 0) return;

    PushPlotStyle();
    if (ImPlot::BeginPlot(id, size, ImPlotFlags_NoTitle | ImPlotFlags_NoMouseText)) {
        ImPlot::SetupAxes(nullptr, nullptr,
                          ImPlotAxisFlags_NoHighlight,
                          ImPlotAxisFlags_NoHighlight | ImPlotAxisFlags_AutoFit);
        ImPlot::SetupAxisFormat(ImAxis_X1, "%.0fs");
        ImPlot::SetupLegend(ImPlotLocation_NorthEast,
                            ImPlotLegendFlags_Horizontal | ImPlotLegendFlags_NoButtons);

        plotUserAgainstMentor("##gap", timesSec.data(), userValues.data(),
                              mentorValues.data(), count, userLabel, mentorLabel);

        ImPlot::EndPlot();
    }
    PopPlotStyle();
}

namespace {

// One side of an expanded cooldown row. Returns the height it drew, and sets
// blacklistRequest to a buff's spell id when its row is right-clicked.
float drawDetailSide(ImDrawList* draw,
                     ImVec2 origin,
                     float width,
                     const char* who,
                     ImU32 accent,
                     const std::string& timeText,
                     const std::string& outputText,
                     const std::vector<CooldownDetailRow::Cast>& casts,
                     const std::vector<CooldownDetailRow::WindowBuff>& buffs,
                     const std::vector<std::string>& targets,
                     const std::function<ImTextureID(uint32_t)>& iconFor,
                     bool showBuffs,
                     uint32_t& blacklistRequest) {
    const float iconSize = 22.0f;
    const float iconGap = 3.0f;
    const float textX = origin.x + theme::SpaceS;
    float y = origin.y;

    draw->AddText(theme::gFontMedium, 0.0f, ImVec2(textX, y),
                  accent, who);

    const float metaX = textX + 66.0f;
    if (timeText.empty()) {
        draw->AddText(ImVec2(metaX, y), toU32(theme::Danger), L("benchmark.not_used"));
        return (y + 20.0f) - origin.y;
    }

    draw->AddText(ImVec2(metaX, y), toU32(theme::TextSecondary), timeText.c_str());
    if (!outputText.empty()) {
        draw->AddText(ImVec2(metaX + 108.0f, y), toU32(theme::TextPrimary),
                      outputText.c_str());
    }
    y += 20.0f;

    // The cast sequence, left to right, wrapping at the panel edge.
    float x = textX;
    const float wrapAt = origin.x + width - theme::SpaceS;
    for (const auto& cast : casts) {
        const ImTextureID tex = iconFor ? iconFor(cast.spellId) : ImTextureID{};
        const float advance = tex ? iconSize
                                  : ImGui::CalcTextSize(cast.shortLabel.c_str()).x + 8.0f;
        if (x + advance > wrapAt) {
            x = textX;
            y += iconSize + iconGap;
        }

        if (tex) {
            draw->AddImage(tex, ImVec2(x, y), ImVec2(x + iconSize, y + iconSize));
        } else {
            // No icon for this spell; draw a short name instead.
            draw->AddRectFilled(ImVec2(x, y), ImVec2(x + advance, y + iconSize),
                                toU32(theme::Surface2), 3.0f);
            draw->AddText(ImVec2(x + 4.0f, y + 4.0f), toU32(theme::TextSecondary),
                          cast.shortLabel.c_str());
        }
        x += advance + iconGap;
    }
    if (!casts.empty()) y += iconSize + iconGap;

    if (!targets.empty()) {
        std::string line;
        for (const auto& target : targets) {
            if (!line.empty()) line += "   ";
            line += target;
        }
        draw->AddText(ImVec2(textX, y), toU32(theme::TextSecondary), line.c_str());
        y += 17.0f;
    }

    if (showBuffs && !buffs.empty()) {
        // One row per buff, a bar on a track spanning the whole window.
        const float barHeight = 9.0f;
        const float rowGap = 3.0f;
        const float labelWidth = 168.0f;
        const float trackX = textX + labelWidth;
        const float trackWidth = std::max((origin.x + width - theme::SpaceS) - trackX,
                                          40.0f);

        for (const auto& buff : buffs) {
            // Blacklisted buffs only appear when revealed, and are dimmed.
            const ImU32 labelColour = buff.blacklisted
                ? toU32(theme::Surface3) : toU32(theme::TextMuted);
            const ImU32 barColour = buff.blacklisted
                ? toU32(theme::Surface3) : accent;

            draw->AddText(ImVec2(textX, y - 1.0f), labelColour, buff.label.c_str());

            draw->AddRectFilled(ImVec2(trackX, y),
                                ImVec2(trackX + trackWidth, y + barHeight),
                                toU32(theme::Surface2), 2.0f);

            const float x0 = trackX + trackWidth * std::clamp(buff.start, 0.0f, 1.0f);
            const float x1 = trackX + trackWidth * std::clamp(buff.end, 0.0f, 1.0f);
            draw->AddRectFilled(ImVec2(x0, y),
                                ImVec2(std::max(x1, x0 + 2.0f), y + barHeight),
                                barColour, 2.0f);

            // Right-click the row to blacklist it. Hit tested by rect because
            // these rows are drawn directly, not ImGui items.
            const ImVec2 rowMin(textX, y - 2.0f);
            const ImVec2 rowMax(trackX + trackWidth, y + barHeight + 2.0f);
            if (ImGui::IsMouseHoveringRect(rowMin, rowMax) &&
                ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
                blacklistRequest = buff.spellId;
            }

            y += barHeight + rowGap;
        }
        y += 2.0f;
    }

    return y - origin.y;
}

} // namespace

uint32_t DrawCooldownDetail(const CooldownDetailRow& row,
                            const char* userLabel,
                            const char* mentorLabel,
                            const std::function<ImTextureID(uint32_t)>& iconFor,
                            bool showBuffs,
                            float width) {
    uint32_t blacklistRequest = 0;
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 start = ImGui::GetCursorScreenPos();

    // The panel's height is only known after the cast sequences wrap, so the
    // content goes on the front channel first and the background is drawn
    // afterwards on the back one.
    draw->ChannelsSplit(2);
    draw->ChannelsSetCurrent(1);

    float y = start.y + theme::SpaceS;
    draw->AddText(theme::gFontMedium, 0.0f,
                  ImVec2(start.x + theme::SpaceS, y),
                  toU32(theme::TextPrimary), row.title.c_str());
    y += 22.0f;

    y += drawDetailSide(draw, ImVec2(start.x, y), width, userLabel,
                        toU32(theme::Accent), row.userTime, row.userOutput,
                        row.userCasts, row.userBuffs, row.userTargets,
                        iconFor, showBuffs, blacklistRequest);
    y += theme::SpaceXS;
    y += drawDetailSide(draw, ImVec2(start.x, y), width, mentorLabel,
                        toU32(kMentorLine), row.mentorTime, row.mentorOutput,
                        row.mentorCasts, row.mentorBuffs, row.mentorTargets,
                        iconFor, showBuffs, blacklistRequest);

    const float height = (y + theme::SpaceS) - start.y;

    draw->ChannelsSetCurrent(0);
    draw->AddRectFilled(start, ImVec2(start.x + width, start.y + height),
                        toU32(theme::Surface1), theme::RadiusCard);
    draw->ChannelsMerge();

    // Drawing to the draw list does not advance the cursor, so reserve the
    // space now that the height is known.
    ImGui::Dummy(ImVec2(width, height));

    return blacklistRequest;
}

void DrawWindowCurve(const char* id,
                     const std::vector<double>& userCurve,
                     const std::vector<double>& mentorCurve,
                     const char* userLabel,
                     const char* mentorLabel,
                     ImVec2 size) {
    if (userCurve.empty() && mentorCurve.empty()) return;

    // The time axis starts when each player's own window opened, not at pull
    // time.
    const size_t count = std::max(userCurve.size(), mentorCurve.size());
    std::vector<double> seconds;
    seconds.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        seconds.push_back(static_cast<double>(i));
    }

    // Pad to the same length so both draw against one axis. The shorter
    // window holds its final total.
    const auto padded = [&](const std::vector<double>& curve) {
        std::vector<double> out = curve;
        const double last = out.empty() ? 0.0 : out.back();
        out.resize(count, last);
        return out;
    };
    const std::vector<double> userPadded = padded(userCurve);
    const std::vector<double> mentorPadded = padded(mentorCurve);

    PushPlotStyle();
    if (ImPlot::BeginPlot(id, size, ImPlotFlags_NoTitle | ImPlotFlags_NoMouseText)) {
        ImPlot::SetupAxes(nullptr, nullptr,
                          ImPlotAxisFlags_NoHighlight,
                          ImPlotAxisFlags_NoHighlight | ImPlotAxisFlags_AutoFit);
        ImPlot::SetupAxisFormat(ImAxis_X1, "+%.0fs");
        ImPlot::SetupLegend(ImPlotLocation_NorthWest,
                            ImPlotLegendFlags_Horizontal | ImPlotLegendFlags_NoButtons);

        plotUserAgainstMentor("##windowgap", seconds.data(), userPadded.data(),
                              mentorPadded.data(), static_cast<int>(count),
                              userLabel, mentorLabel);

        ImPlot::EndPlot();
    }
    PopPlotStyle();
}

void DrawCooldownTimelineRows(const char* id,
                              const std::vector<CooldownMark>& userMarks,
                              const std::vector<CooldownMark>& mentorMarks,
                              int32_t pullDurationMs,
                              const char* userLabel,
                              const char* mentorLabel,
                              const std::function<ImTextureID(uint32_t)>& iconFor,
                              ImVec2 size) {
    if (pullDurationMs <= 0) return;

    // Scoped so two timelines on one screen keep their own widget state.
    ImGui::PushID(id);

    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float labelWidth = 110.0f;
    const float trackX = origin.x + labelWidth;
    const float trackWidth = std::max(size.x - labelWidth - theme::SpaceM, 80.0f);
    const float rowHeight = 34.0f;
    const float iconSize = 24.0f;

    const auto toX = [&](int32_t timeMs) {
        const float t = std::clamp(static_cast<float>(timeMs) /
                                   static_cast<float>(pullDurationMs), 0.0f, 1.0f);
        return trackX + trackWidth * t;
    };

    const auto drawRow = [&](float y, const char* who, ImU32 accent,
                             const std::vector<CooldownMark>& marks) {
        draw->AddText(theme::gFontMedium, 0.0f,
                      ImVec2(origin.x + theme::SpaceS, y + iconSize * 0.5f - 6.0f),
                      accent, who);

        draw->AddRectFilled(ImVec2(trackX, y + iconSize * 0.5f - 1.0f),
                            ImVec2(trackX + trackWidth, y + iconSize * 0.5f + 1.0f),
                            toU32(theme::Surface2));

        for (const auto& mark : marks) {
            const float x0 = toX(mark.timeMs);

            // The window as a bar behind the icon.
            if (mark.windowMs > 0) {
                const float x1 = toX(mark.timeMs + mark.windowMs);
                draw->AddRectFilled(ImVec2(x0, y + 2.0f),
                                    ImVec2(std::max(x1, x0 + 2.0f), y + iconSize - 2.0f),
                                    (accent & 0x00FFFFFF) | 0x33000000, 3.0f);
            }

            const ImTextureID tex = iconFor ? iconFor(mark.spellId) : ImTextureID{};
            if (tex) {
                draw->AddImage(tex, ImVec2(x0, y),
                               ImVec2(x0 + iconSize, y + iconSize));
                draw->AddRect(ImVec2(x0, y), ImVec2(x0 + iconSize, y + iconSize),
                              accent, 2.0f, 0, 1.5f);
            } else {
                draw->AddRectFilled(ImVec2(x0, y),
                                    ImVec2(x0 + iconSize, y + iconSize),
                                    toU32(theme::Surface3), 3.0f);
                draw->AddText(ImVec2(x0 + 2.0f, y + 5.0f), accent,
                              mark.shortLabel.c_str());
            }
        }
    };

    drawRow(origin.y, userLabel, toU32(theme::Accent), userMarks);
    drawRow(origin.y + rowHeight + theme::SpaceS, mentorLabel,
            toU32(kMentorLine), mentorMarks);

    // Time scale under both rows, every 30 seconds.
    const float scaleY = origin.y + (rowHeight + theme::SpaceS) * 2.0f;
    for (int32_t markMs = 0; markMs <= pullDurationMs; markMs += 30000) {
        const float x = toX(markMs);
        draw->AddLine(ImVec2(x, scaleY), ImVec2(x, scaleY + 4.0f),
                      toU32(theme::Surface3));
        char label[16];
        std::snprintf(label, sizeof(label), "%d:%02d",
                      markMs / 60000, (markMs / 1000) % 60);
        draw->AddText(ImVec2(x - 10.0f, scaleY + 5.0f),
                      toU32(theme::TextMuted), label);
    }

    ImGui::Dummy(ImVec2(size.x, scaleY + 20.0f - origin.y));
    ImGui::PopID();
}

void DrawPositionMap(const char* id,
                     const PositionPath& userPath,
                     const PositionPath& mentorPath,
                     const char* userLabel,
                     const char* mentorLabel,
                     bool relativeToBoss,
                     const PositionPlayback& playback,
                     ImVec2 size) {
    if (userPath.x.empty() && mentorPath.x.empty()) return;

    // The visible part of a path: all of it outside playback, otherwise from
    // the trail cutoff to the playhead. An index range, so the plot reads the
    // existing arrays without copying.
    struct Slice { int first = 0; int count = 0; };
    const auto sliceOf = [&](const PositionPath& path) -> Slice {
        if (!playback.active || path.timeMs.empty()) {
            return {0, static_cast<int>(path.x.size())};
        }

        int last = 0;
        while (last + 1 < static_cast<int>(path.timeMs.size()) &&
               path.timeMs[last + 1] <= playback.currentMs) {
            ++last;
        }

        int first = 0;
        if (playback.trailMs > 0) {
            const int32_t cutoff = playback.currentMs - playback.trailMs;
            while (first < last && path.timeMs[first] < cutoff) ++first;
        }
        return {first, last - first + 1};
    };

    const Slice userSlice = sliceOf(userPath);
    const Slice mentorSlice = sliceOf(mentorPath);

    PushPlotStyle();
    if (ImPlot::BeginPlot(id, size, ImPlotFlags_NoTitle | ImPlotFlags_NoMouseText |
                                        ImPlotFlags_Equal)) {
        // Equal axes so path shapes are not distorted.
        ImPlot::SetupAxes(nullptr, nullptr,
                          ImPlotAxisFlags_NoHighlight | ImPlotAxisFlags_AutoFit,
                          ImPlotAxisFlags_NoHighlight | ImPlotAxisFlags_AutoFit);
        ImPlot::SetupLegend(ImPlotLocation_NorthEast,
                            ImPlotLegendFlags_Horizontal | ImPlotLegendFlags_NoButtons);

        // The boss sits at the origin in relative mode.
        if (relativeToBoss) {
            const double zero[1] = {0.0};
            ImPlotSpec bossSpec;
            bossSpec.Marker = ImPlotMarker_Cross;
            bossSpec.MarkerSize = 8.0f;
            bossSpec.LineColor = theme::TextMuted;
            bossSpec.Flags = ImPlotItemFlags_NoLegend;
            ImPlot::PlotScatter("##boss", zero, zero, 1, bossSpec);
        }

        // During playback the full path is drawn underneath at low alpha.
        if (playback.active) {
            ImPlotSpec ghostSpec;
            ghostSpec.LineWeight = 1.0f;
            ghostSpec.Flags = ImPlotItemFlags_NoLegend;

            // ImPlotSpec has no line alpha, only a fill one, so fade the
            // colour itself.
            const auto faded = [](ImVec4 c) { c.w = 0.18f; return c; };

            if (!mentorPath.x.empty()) {
                ghostSpec.LineColor = faded(kMentorLine);
                ImPlot::PlotLine("##mentorghost", mentorPath.x.data(),
                                 mentorPath.y.data(),
                                 static_cast<int>(mentorPath.x.size()), ghostSpec);
            }
            if (!userPath.x.empty()) {
                ghostSpec.LineColor = faded(theme::Accent);
                ImPlot::PlotLine("##userghost", userPath.x.data(),
                                 userPath.y.data(),
                                 static_cast<int>(userPath.x.size()), ghostSpec);
            }
        }

        if (mentorSlice.count > 0) {
            ImPlotSpec mentorSpec;
            mentorSpec.LineColor = kMentorLine;
            mentorSpec.LineWeight = 1.6f;
            ImPlot::PlotLine(mentorLabel, mentorPath.x.data() + mentorSlice.first,
                             mentorPath.y.data() + mentorSlice.first,
                             mentorSlice.count, mentorSpec);
        }

        if (userSlice.count > 0) {
            ImPlotSpec userSpec;
            userSpec.LineColor = theme::Accent;
            userSpec.LineWeight = 2.0f;
            ImPlot::PlotLine(userLabel, userPath.x.data() + userSlice.first,
                             userPath.y.data() + userSlice.first,
                             userSlice.count, userSpec);
        }

        // A dot at each player's current position during playback, or at the
        // start of the pull when stopped.
        const auto marker = [&](const PositionPath& path, const Slice& slice,
                                ImVec4 colour, const char* markerId) {
            if (slice.count <= 0) return;
            const int index = playback.active ? slice.first + slice.count - 1 : 0;
            ImPlotSpec spec;
            spec.Marker = ImPlotMarker_Circle;
            spec.MarkerSize = 6.0f;
            spec.LineColor = colour;
            spec.MarkerFillColor = colour;
            spec.Flags = ImPlotItemFlags_NoLegend;
            ImPlot::PlotScatter(markerId, path.x.data() + index,
                                path.y.data() + index, 1, spec);
        };
        marker(mentorPath, mentorSlice, kMentorLine, "##mentornow");
        marker(userPath, userSlice, theme::Accent, "##usernow");

        ImPlot::EndPlot();
    }
    PopPlotStyle();
}

void DrawCompositionRings(const char* id,
                          std::vector<CompositionSlice> slices,
                          const char* userLabel,
                          const char* mentorLabel,
                          const std::function<std::string(double)>& formatValue,
                          ImVec2 size) {
    if (slices.empty()) return;

    std::sort(slices.begin(), slices.end(),
              [](const CompositionSlice& a, const CompositionSlice& b) {
                  return (a.userValue + a.mentorValue) >
                         (b.userValue + b.mentorValue);
              });

    // Six named slices plus a remainder.
    constexpr size_t kMaxSlices = 6;
    bool hasRemainder = false;
    if (slices.size() > kMaxSlices) {
        CompositionSlice rest;
        rest.label = L("benchmark.composition.other");
        for (size_t i = kMaxSlices; i < slices.size(); ++i) {
            rest.userValue += slices[i].userValue;
            rest.mentorValue += slices[i].mentorValue;
        }
        slices.resize(kMaxSlices);
        slices.push_back(std::move(rest));
        hasRemainder = true;
    }

    double userTotal = 0.0;
    double mentorTotal = 0.0;
    for (const auto& slice : slices) {
        userTotal += slice.userValue;
        mentorTotal += slice.mentorValue;
    }
    if (userTotal <= 0.0 && mentorTotal <= 0.0) return;

    ImGui::PushID(id);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 origin = ImGui::GetCursorScreenPos();

    // A distinct hue per slice. The remainder always takes the last, muted
    // entry.
    const auto sliceColour = [&](size_t index) {
        if (hasRemainder && index == slices.size() - 1) {
            return ImGui::ColorConvertFloat4ToU32(
                theme::Series[theme::SeriesCount - 1]);
        }
        return ImGui::ColorConvertFloat4ToU32(theme::seriesColour(index));
    };

    const float ringRadius = 62.0f;
    const float ringThickness = 22.0f;
    const float ringGap = theme::SpaceL;

    const auto drawRing = [&](ImVec2 centre, double total, const char* who,
                              double CompositionSlice::*side) {
        if (total <= 0.0) {
            draw->AddCircle(centre, ringRadius - ringThickness * 0.5f,
                            ImGui::ColorConvertFloat4ToU32(theme::Surface2),
                            48, ringThickness);
        } else {
            float angle = -IM_PI * 0.5f;
            for (size_t i = 0; i < slices.size(); ++i) {
                const double value = slices[i].*side;
                if (value <= 0.0) continue;
                const float sweep = static_cast<float>(value / total) * IM_PI * 2.0f;
                draw->PathArcTo(centre, ringRadius - ringThickness * 0.5f,
                                angle, angle + sweep, 32);
                draw->PathStroke(sliceColour(i), 0, ringThickness);
                angle += sweep;
            }
        }

        const std::string totalText = formatValue(total);
        const ImVec2 totalSize = ImGui::CalcTextSize(totalText.c_str());
        draw->AddText(theme::gFontMedium, 0.0f,
                      ImVec2(centre.x - totalSize.x * 0.5f, centre.y - 9.0f),
                      ImGui::ColorConvertFloat4ToU32(theme::TextPrimary),
                      totalText.c_str());

        const ImVec2 whoSize = ImGui::CalcTextSize(who);
        draw->AddText(ImVec2(centre.x - whoSize.x * 0.5f, centre.y + 5.0f),
                      ImGui::ColorConvertFloat4ToU32(theme::TextMuted), who);
    };

    const ImVec2 userCentre(origin.x + ringRadius, origin.y + ringRadius);
    const ImVec2 mentorCentre(origin.x + ringRadius * 3.0f + ringGap,
                              origin.y + ringRadius);
    drawRing(userCentre, userTotal, userLabel, &CompositionSlice::userValue);
    drawRing(mentorCentre, mentorTotal, mentorLabel, &CompositionSlice::mentorValue);

    // Legend to the right of the rings, each row with its two shares.
    const float legendX = mentorCentre.x + ringRadius + ringGap;
    float legendY = origin.y + 2.0f;

    {
        const std::string header = LF("benchmark.composition.header",
                                      userLabel, mentorLabel);
        draw->AddText(ImVec2(legendX + 168.0f, legendY),
                      ImGui::ColorConvertFloat4ToU32(theme::TextMuted),
                      header.c_str());
        legendY += 17.0f;
    }

    for (size_t i = 0; i < slices.size(); ++i) {
        const auto& slice = slices[i];

        draw->AddRectFilled(ImVec2(legendX, legendY + 3.0f),
                            ImVec2(legendX + 10.0f, legendY + 13.0f),
                            sliceColour(i), 2.0f);

        // Truncated rather than wrapped so rows stay aligned with their shares.
        std::string label = slice.label;
        if (label.size() > 22) label = label.substr(0, 21) + ".";
        draw->AddText(ImVec2(legendX + 17.0f, legendY),
                      ImGui::ColorConvertFloat4ToU32(theme::TextPrimary),
                      label.c_str());

        const double userShare = userTotal > 0.0 ? slice.userValue / userTotal : 0.0;
        const double mentorShare = mentorTotal > 0.0
            ? slice.mentorValue / mentorTotal : 0.0;

        const float barX = legendX + 168.0f;
        const float barWidth = 54.0f;
        const auto shareBar = [&](float y, double share, ImU32 colour) {
            draw->AddRectFilled(ImVec2(barX, y), ImVec2(barX + barWidth, y + 5.0f),
                                ImGui::ColorConvertFloat4ToU32(theme::Surface2), 1.0f);
            const float w = barWidth * static_cast<float>(
                std::clamp(share, 0.0, 1.0));
            if (w > 0.0f) {
                draw->AddRectFilled(ImVec2(barX, y), ImVec2(barX + w, y + 5.0f),
                                    colour, 1.0f);
            }
        };
        shareBar(legendY + 2.0f, userShare, sliceColour(i));
        shareBar(legendY + 9.0f, mentorShare,
                 ImGui::ColorConvertFloat4ToU32(theme::TextSecondary));

        draw->AddText(ImVec2(barX + barWidth + 8.0f, legendY),
                      ImGui::ColorConvertFloat4ToU32(theme::TextSecondary),
                      LF("benchmark.composition.shares",
                         userShare * 100.0, mentorShare * 100.0).c_str());

        legendY += 19.0f;
    }

    const float used = std::max(ringRadius * 2.0f + theme::SpaceM,
                                legendY - origin.y + theme::SpaceS);
    ImGui::Dummy(ImVec2(size.x, used));
    ImGui::PopID();
}

void DrawDowntimeStrip(const char* id,
                       const DowntimeStrip& strip,
                       const std::function<ImTextureID(uint32_t)>& iconFor,
                       float width) {
    ImGui::PushID(id);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 origin = ImGui::GetCursorScreenPos();

    const float iconSize = 26.0f;
    const float rowHeight = iconSize + 22.0f;

    // Fixed time scale so gaps are comparable between strips.
    constexpr float kPixelsPerSecond = 34.0f;

    const std::string& caption = strip.caption;
    draw->AddText(theme::gFontMedium, 0.0f, origin,
                  toU32(strip.inCooldown ? theme::Warn : theme::TextPrimary),
                  caption.c_str());

    if (!strip.context.empty()) {
        const ImVec2 capSize = ImGui::CalcTextSize(caption.c_str());
        draw->AddText(ImVec2(origin.x + capSize.x + theme::SpaceM, origin.y),
                      toU32(theme::TextMuted), strip.context.c_str());
    }

    float x = origin.x;
    const float y = origin.y + 18.0f;

    // The spell's icon, or a chip with its short name when it has none.
    const auto drawSpell = [&](float left, uint32_t spellId, const std::string& label) {
        const ImTextureID tex = iconFor ? iconFor(spellId) : ImTextureID{};
        if (tex) {
            draw->AddImage(tex, ImVec2(left, y), ImVec2(left + iconSize, y + iconSize));
        } else {
            draw->AddRectFilled(ImVec2(left, y), ImVec2(left + iconSize, y + iconSize),
                                toU32(theme::Surface3), 3.0f);
            draw->AddText(ImVec2(left + 2.0f, y + 6.0f), toU32(theme::TextSecondary),
                          label.c_str());
        }
    };

    for (const auto& cast : strip.leadIn) {
        // Width follows how long the cast occupied the player.
        const float w = std::max(iconSize,
                                 cast.occupancyMs / 1000.0f * kPixelsPerSecond);
        if (x + w > origin.x + width - 60.0f) break;

        drawSpell(x, cast.spellId, cast.shortLabel);

        // The tail past the icon is the rest of the global it occupied.
        if (w > iconSize) {
            draw->AddRectFilled(ImVec2(x + iconSize, y + iconSize * 0.35f),
                                ImVec2(x + w - 2.0f, y + iconSize * 0.65f),
                                toU32(theme::Surface2), 2.0f);
        }
        x += w;
    }

    // The gap itself, hatched.
    const float gapWidth = std::max(18.0f,
                                    strip.gapMs / 1000.0f * kPixelsPerSecond);
    const float gapEnd = std::min(x + gapWidth, origin.x + width - 34.0f);

    draw->AddRectFilled(ImVec2(x, y), ImVec2(gapEnd, y + iconSize),
                        toU32(strip.inCooldown ? theme::Danger : theme::Surface2),
                        3.0f);
    for (float hatch = x + 4.0f; hatch < gapEnd; hatch += 7.0f) {
        draw->AddLine(ImVec2(hatch, y + iconSize - 2.0f),
                      ImVec2(hatch + 5.0f, y + 2.0f),
                      toU32(theme::Surface1), 1.5f);
    }

    const std::string gapText = LF("benchmark.gap_seconds", strip.gapMs / 1000.0);
    const ImVec2 gapSize = ImGui::CalcTextSize(gapText.c_str());
    if (gapEnd - x > gapSize.x + 6.0f) {
        draw->AddText(ImVec2((x + gapEnd) * 0.5f - gapSize.x * 0.5f, y + 6.0f),
                      toU32(theme::TextPrimary), gapText.c_str());
    }

    // The spell that ended the gap.
    if (strip.resumedSpellId != 0 && gapEnd + iconSize < origin.x + width) {
        drawSpell(gapEnd + 4.0f, strip.resumedSpellId, strip.resumedLabel);
    }


    ImGui::Dummy(ImVec2(width, rowHeight));
    ImGui::PopID();
}

void DrawVersusStat(const VersusStat& stat, ImVec2 size) {
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 pos = ImGui::GetCursorScreenPos();

    draw->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y),
                        toU32(theme::Surface1), theme::RadiusCard);
    draw->AddRect(pos, ImVec2(pos.x + size.x, pos.y + size.y),
                  toU32(theme::BorderSubtle), theme::RadiusCard);

    const float padX = theme::SpaceM;
    float y = pos.y + theme::SpaceS;

    draw->AddText(ImVec2(pos.x + padX, y), toU32(theme::TextMuted), stat.label);
    y += 18.0f;

    ImGui::PushFont(theme::gFontMedium);
    const float valueSize = ImGui::GetFontSize() * 1.35f;
    draw->AddText(ImGui::GetFont(), valueSize, ImVec2(pos.x + padX, y),
                  toU32(theme::Accent), stat.userText.c_str());

    const float userWidth =
        ImGui::GetFont()->CalcTextSizeA(valueSize, FLT_MAX, 0.0f,
                                        stat.userText.c_str()).x;
    ImGui::PopFont();

    const float smallSize = ImGui::GetFontSize();
    draw->AddText(ImGui::GetFont(), smallSize,
                  ImVec2(pos.x + padX + userWidth + 8.0f, y + valueSize - smallSize - 2.0f),
                  toU32(kMentorLine),
                  LF("benchmark.vs", stat.mentorText.c_str()).c_str());

    y += valueSize + 6.0f;

    // Both bars scale against the larger of the two values.
    const float trackW = size.x - padX * 2.0f;
    const float barH = 6.0f;
    const double peak = std::max({stat.userValue, stat.mentorValue, 1.0});

    const auto bar = [&](float top, double value, const ImVec4& color) {
        draw->AddRectFilled(ImVec2(pos.x + padX, top),
                            ImVec2(pos.x + padX + trackW, top + barH),
                            toU32(theme::Surface2), barH * 0.5f);
        const float w = static_cast<float>(std::clamp(value / peak, 0.0, 1.0)) * trackW;
        if (w > 1.0f) {
            draw->AddRectFilled(ImVec2(pos.x + padX, top),
                                ImVec2(pos.x + padX + w, top + barH),
                                toU32(color), barH * 0.5f);
        }
    };

    bar(y, stat.userValue, theme::Accent);
    bar(y + barH + 3.0f, stat.mentorValue, kMentorLine);
    y += barH * 2.0f + 3.0f + 6.0f;

    if (!stat.deltaText.empty()) {
        // Colour follows higherIsBetter, not the sign.
        const bool ahead = stat.higherIsBetter
                               ? stat.userValue >= stat.mentorValue
                               : stat.userValue <= stat.mentorValue;
        draw->AddText(ImVec2(pos.x + padX, y),
                      toU32(ahead ? theme::Success : theme::Danger),
                      stat.deltaText.c_str());
    }

    ImGui::Dummy(size);
}

void DrawShortfallSplit(int64_t burstDelta,
                        int64_t fillerDelta,
                        const char* burstLabel,
                        const char* fillerLabel,
                        ImVec2 size) {
    // Only losses are apportioned; a side where the user was ahead counts
    // as zero.
    const double burstLoss = burstDelta < 0 ? -static_cast<double>(burstDelta) : 0.0;
    const double fillerLoss = fillerDelta < 0 ? -static_cast<double>(fillerDelta) : 0.0;
    const double total = burstLoss + fillerLoss;
    if (total <= 0.0) return;

    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 pos = ImGui::GetCursorScreenPos();

    const float barH = 10.0f;
    const float burstW = static_cast<float>(burstLoss / total) * size.x;

    draw->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + barH),
                        toU32(theme::Surface2), barH * 0.5f);
    if (burstW > 1.0f) {
        draw->AddRectFilled(pos, ImVec2(pos.x + burstW, pos.y + barH),
                            toU32(theme::Warn), barH * 0.5f);
    }
    if (size.x - burstW > 1.0f) {
        draw->AddRectFilled(ImVec2(pos.x + burstW, pos.y),
                            ImVec2(pos.x + size.x, pos.y + barH),
                            toU32(theme::Danger), barH * 0.5f);
    }

    // An empty label means that side had no loss to report.
    const float labelY = pos.y + barH + 4.0f;
    if (burstLabel && burstLabel[0] != '\0') {
        draw->AddText(ImVec2(pos.x, labelY), toU32(theme::Warn), burstLabel);
    }
    if (fillerLabel && fillerLabel[0] != '\0') {
        const float fillerWidth = ImGui::CalcTextSize(fillerLabel).x;
        draw->AddText(ImVec2(pos.x + size.x - fillerWidth, labelY),
                      toU32(theme::Danger), fillerLabel);
    }

    ImGui::Dummy(ImVec2(size.x, barH + 22.0f));
}

} // namespace benchmark_ui
