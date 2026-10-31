#include "PlayAlong/UIPlayAlongStatsPanel.h"
#include "Core/LocalizationManager.h"
#include <imgui.h>
#include <algorithm>
#include <cstdio>

namespace PlayAlong {

void UIPlayAlongStatsPanel::render(const PlayAlongFeedbackSystem* feedbackSystem) {
    if (!feedbackSystem) return;

    float scale = ImGui::GetIO().FontGlobalScale;

    ImGuiViewport* viewport = ImGui::GetMainViewport();
    float panelWidth = 180.0f * scale;

    ImGui::SetNextWindowPos(
        ImVec2(viewport->Pos.x + viewport->Size.x - panelWidth - 20.0f * scale,
               viewport->Pos.y + 20.0f * scale),
        ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(
        ImVec2(150.0f * scale, 100.0f * scale),
        ImVec2(300.0f * scale, 400.0f * scale)
    );

    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.08f, 0.08f, 0.12f, 0.92f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f * scale);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f * scale, 10.0f * scale));

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar |
                             ImGuiWindowFlags_NoCollapse |
                             ImGuiWindowFlags_AlwaysAutoResize;

    if (ImGui::Begin(L("playalong.stats"), nullptr, flags)) {
        if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(0)) {
            ImGui::SetKeyboardFocusHere(-1);
        }

        uint32_t totalGood = feedbackSystem->getTotalGood();
        uint32_t totalBad = feedbackSystem->getTotalBad();
        uint32_t totalMissed = feedbackSystem->getTotalMissed();
        uint32_t totalAttempts = totalGood + totalBad + totalMissed;
        float overallAccuracy = feedbackSystem->getOverallAccuracy();

        if (totalGood > lastGoodCount_) {
            uint32_t newGood = totalGood - lastGoodCount_;
            if (totalBad == lastBadCount_) {
                currentStreak_ += newGood;
            } else {
                // A bad input came in this frame too; the streak restarts from the new good ones
                currentStreak_ = newGood;
            }
            bestStreak_ = std::max(bestStreak_, currentStreak_);
        }
        if (totalBad > lastBadCount_) {
            currentStreak_ = 0;
        }
        lastGoodCount_ = totalGood;
        lastBadCount_ = totalBad;

        const auto& recentFeedback = feedbackSystem->getRecentFeedback();
        uint32_t recentGood = 0, recentBad = 0;
        size_t count = 0;
        for (auto it = recentFeedback.rbegin(); it != recentFeedback.rend() && count < RECENT_WINDOW; ++it, ++count) {
            if (it->result == FeedbackResult::Good) {
                recentGood++;
            } else if (it->result == FeedbackResult::Bad) {
                recentBad++;
            }
        }
        float recentAccuracy = (recentGood + recentBad > 0)
            ? static_cast<float>(recentGood) / (recentGood + recentBad)
            : 1.0f;

        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 0.75f, 1.0f));
        ImGui::Text("%s", L("playalong.overall"));
        ImGui::PopStyleColor();

        ImGui::SameLine(100.0f * scale);
        ImU32 overallColor;
        if (overallAccuracy >= 0.9f) {
            overallColor = IM_COL32(50, 255, 50, 255);
        } else if (overallAccuracy >= 0.7f) {
            overallColor = IM_COL32(255, 200, 50, 255);
        } else {
            overallColor = IM_COL32(255, 100, 100, 255);
        }
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(overallColor));
        ImGui::Text("%.0f%%", overallAccuracy * 100.0f);
        ImGui::PopStyleColor();

        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 0.75f, 1.0f));
        ImGui::Text("%s", L("playalong.last_15"));
        ImGui::PopStyleColor();

        ImGui::SameLine(100.0f * scale);
        ImU32 recentColor;
        if (recentAccuracy >= 0.9f) {
            recentColor = IM_COL32(50, 255, 50, 255);
        } else if (recentAccuracy >= 0.7f) {
            recentColor = IM_COL32(255, 200, 50, 255);
        } else {
            recentColor = IM_COL32(255, 100, 100, 255);
        }
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(recentColor));
        ImGui::Text("%.0f%%", recentAccuracy * 100.0f);
        ImGui::PopStyleColor();

        ImGui::Separator();

        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 0.75f, 1.0f));
        ImGui::Text("%s", L("playalong.streak"));
        ImGui::PopStyleColor();

        ImGui::SameLine(100.0f * scale);
        ImU32 streakColor = currentStreak_ >= 10 ? IM_COL32(50, 255, 50, 255) :
                           currentStreak_ >= 5 ? IM_COL32(255, 200, 50, 255) :
                                                 IM_COL32(200, 200, 200, 255);
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(streakColor));
        ImGui::Text("%u", currentStreak_);
        ImGui::PopStyleColor();

        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 0.75f, 1.0f));
        ImGui::Text("%s", L("playalong.best_streak"));
        ImGui::PopStyleColor();

        ImGui::SameLine(100.0f * scale);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.8f, 1.0f, 1.0f));
        ImGui::Text("%u", bestStreak_);
        ImGui::PopStyleColor();

        ImGui::Separator();

        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.9f, 0.4f, 1.0f));
        ImGui::Text("%s", L("playalong.good"));
        ImGui::PopStyleColor();
        ImGui::SameLine(100.0f * scale);
        ImGui::Text("%u", totalGood);

        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.4f, 0.4f, 1.0f));
        ImGui::Text("%s", L("playalong.bad"));
        ImGui::PopStyleColor();
        ImGui::SameLine(100.0f * scale);
        ImGui::Text("%u", totalBad);

        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 0.4f, 1.0f));
        ImGui::Text("%s", L("playalong.missed"));
        ImGui::PopStyleColor();
        ImGui::SameLine(100.0f * scale);
        ImGui::Text("%u", totalMissed);

        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.65f, 1.0f));
        ImGui::Text("%s", L("playalong.total"));
        ImGui::PopStyleColor();
        ImGui::SameLine(100.0f * scale);
        ImGui::Text("%u", totalAttempts);
    }
    ImGui::End();

    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
}

} // namespace PlayAlong
