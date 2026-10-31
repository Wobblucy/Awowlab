#include "UI/Panels/WarcraftLogs/UIWarcraftLogsPanel.h"
#include "UI/AwlUI/Widgets.h"
#include "UI/UISettings.h"
#include "Core/WarcraftLogsSession.h"
#include "Core/LocalizationManager.h"
#include "Adapter/WclToAwowAdapter.h"
#include <imgui.h>

UIWarcraftLogsPanel::UIWarcraftLogsPanel()
    : session_(nullptr)
    , selectedFightId_(-1)
    , fightSelected_(false)
    , cancelled_(false)
    , isLoading_(false)
    , progressValue_(0.0f)
{
}

void UIWarcraftLogsPanel::setSession(warcraftlogs::WarcraftLogsSession* session) {
    session_ = session;
    selectedFightId_ = -1;
    fightSelected_ = false;
    cancelled_ = false;
}

void UIWarcraftLogsPanel::reset() {
    session_ = nullptr;
    selectedFightId_ = -1;
    fightSelected_ = false;
    cancelled_ = false;
    isLoading_ = false;
    progressMessage_.clear();
    progressValue_ = 0.0f;
}

void UIWarcraftLogsPanel::setProgress(const std::string& message, float progress) {
    progressMessage_ = message;
    progressValue_ = progress;
}

bool UIWarcraftLogsPanel::render() {
    if (fightSelected_ || cancelled_) {
        return false;
    }

    ImGui::OpenPopup("Select Fight");

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(800, 450), ImGuiCond_Appearing);

    bool isOpen = true;
    if (ImGui::BeginPopupModal("Select Fight", &isOpen, ImGuiWindowFlags_NoResize)) {
        if (isLoading_) {
            ImGui::Text("%s", progressMessage_.c_str());

            if (progressValue_ >= 0.0f) {
                ImGui::ProgressBar(progressValue_, ImVec2(-1, 0));
            } else {
                // Indeterminate progress - show animated dots
                static float time = 0.0f;
                time += ImGui::GetIO().DeltaTime;
                int dots = static_cast<int>(time * 3.0f) % 4;
                std::string dotStr(dots, '.');
                ImGui::Text("Loading%s", dotStr.c_str());
            }

            ImGui::EndPopup();
            return false;
        }

        if (!session_) {
            ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "%s", L("wcl.no_session"));
            if (awlui::Button(L("btn.cancel"), awlui::ButtonVariant::Ghost,
                              awlui::ButtonSize::Md, ImVec2(100, 0))) {
                cancelled_ = true;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
            return false;
        }

        const auto& fights = session_->getFights();
        const std::string& reportCode = session_->getReportCode();

        // Header
        ImGui::Text(L("wcl.report"), reportCode.c_str());
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), L("wcl.fights_count"), fights.size());

        // Rate limit status and API key guidance
        if (rateLimitState_) {
            int used = rateLimitState_->queriesUsed;
            int max = WclRateLimitState::MAX_QUERIES;
            int minutesLeft = rateLimitState_->getMinutesUntilReset();
            bool atLimit = used >= max;

            ImVec4 color = atLimit ? ImVec4(1.0f, 0.3f, 0.3f, 1.0f) : ImVec4(0.7f, 0.7f, 0.3f, 1.0f);

            if (minutesLeft > 0 && atLimit) {
                ImGui::TextColored(color, L("wcl.api_usage"), used, max, minutesLeft);
            } else {
                ImGui::TextColored(color, L("wcl.api_usage_hourly"), used, max);
            }

            ImGui::SameLine();
            ImGui::TextDisabled("(?)");
            if (ImGui::IsItemHovered()) {
                ImGui::BeginTooltip();
                ImGui::PushTextWrapPos(500.0f);
                ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.5f, 1.0f), "%s", L("wcl_help.want_unlimited"));
                ImGui::Spacing();
                ImGui::Text("%s", L("wcl_help.step1"));
                ImGui::Text("%s", L("wcl_help.step2"));
                ImGui::Text("%s", L("wcl_help.step3"));
                ImGui::Text("%s", L("wcl_help.step4"));
                ImGui::Text("%s", L("wcl_help.step5"));
                ImGui::Text("%s", L("wcl_help.step6"));
                ImGui::Text("%s", L("wcl_help.step7"));
                ImGui::Text("%s", L("wcl_help.step8"));
                ImGui::Text("%s", L("wcl_help.step9"));
                ImGui::PopTextWrapPos();
                ImGui::EndTooltip();
            }

            if (atLimit) {
                ImGui::Spacing();
                ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.2f, 0.15f, 0.1f, 1.0f));
                ImGui::BeginChild("APIKeyHelp", ImVec2(0, 130), true);
                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "%s", L("wcl_help.get_unlimited"));
                ImGui::Spacing();
                ImGui::BulletText("%s", L("wcl_help.go_to"));
                ImGui::BulletText("%s", L("wcl_help.app_name"));
                ImGui::BulletText("%s", L("wcl_help.redirect_url"));
                ImGui::BulletText("%s", L("wcl_help.public_client"));
                ImGui::BulletText("%s", L("wcl_help.save_credentials"));
                ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "   %s", L("wcl_help.credentials_format"));
                ImGui::EndChild();
                ImGui::PopStyleColor();
            }
        }

        ImGui::Spacing();

        // Buttons
        if (awlui::Button(L("btn.load_fight"), awlui::ButtonVariant::Primary,
                          awlui::ButtonSize::Md, ImVec2(180, 0))) {
            fightSelected_ = true;
            ImGui::CloseCurrentPopup();
        }

        ImGui::SameLine();

        if (awlui::Button(L("btn.cancel"), awlui::ButtonVariant::Ghost,
                          awlui::ButtonSize::Md, ImVec2(100, 0))) {
            cancelled_ = true;
            ImGui::CloseCurrentPopup();
        }

        ImGui::Separator();

        // Fight table
        if (ImGui::BeginTable("FightTable", 5,
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY)) {

            // Headers
            ImGui::TableSetupColumn(L("encounter.col_number"), ImGuiTableColumnFlags_WidthFixed, 40.0f);
            ImGui::TableSetupColumn(L("encounter.col_fight"), ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn(L("encounter.col_difficulty"), ImGuiTableColumnFlags_WidthFixed, 100.0f);
            ImGui::TableSetupColumn(L("encounter.col_duration"), ImGuiTableColumnFlags_WidthFixed, 80.0f);
            ImGui::TableSetupColumn(L("cache.meta"), ImGuiTableColumnFlags_WidthFixed, 60.0f);
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableHeadersRow();

            // Fight rows
            for (size_t i = 0; i < fights.size(); ++i) {
                const auto& fight = fights[i];

                ImGui::TableNextRow();

                // Column 0: Fight ID
                ImGui::TableSetColumnIndex(0);
                bool isSelected = (selectedFightId_ == fight.id);
                if (ImGui::Selectable(
                    ("##fight" + std::to_string(i)).c_str(),
                    isSelected,
                    ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick,
                    ImVec2(0, 0)
                )) {
                    selectedFightId_ = fight.id;
                    if (ImGui::IsMouseDoubleClicked(0)) {
                        fightSelected_ = true;
                        ImGui::CloseCurrentPopup();
                    }
                }
                ImGui::SameLine();
                ImGui::Text("%d", fight.id);

                // Column 1: Fight Name
                ImGui::TableSetColumnIndex(1);
                // Color boss fights differently than trash
                if (fight.encounterID > 0) {
                    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "%s", fight.name.c_str());
                } else {
                    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "%s", fight.name.c_str());
                }

                // Column 2: Difficulty
                ImGui::TableSetColumnIndex(2);
                ImGui::Text("%s", getDifficultyName(fight.difficulty));

                // Column 3: Duration
                ImGui::TableSetColumnIndex(3);
                uint64_t durationMs = fight.duration();
                uint32_t durationSec = static_cast<uint32_t>(durationMs / 1000);
                uint32_t minutes = durationSec / 60;
                uint32_t seconds = durationSec % 60;
                ImGui::Text("%u:%02u", minutes, seconds);

                // Column 4: Cached status
                ImGui::TableSetColumnIndex(4);
                if (session_->isFightCached(fight.id)) {
                    ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "%s", L("cache.yes"));
                } else {
                    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "No");
                }
            }

            ImGui::EndTable();
        }

        if (!isOpen) {
            cancelled_ = true;
        }

        ImGui::EndPopup();
    }

    return fightSelected_;
}

const char* UIWarcraftLogsPanel::getDifficultyName(int32_t difficultyId) {
    // WarcraftLogs API difficulty values (different from WoW internal IDs!)
    switch (difficultyId) {
        // WarcraftLogs API values
        case 1:  return L("difficulty.lfr");
        case 3:  return L("difficulty.normal");
        case 4:  return L("difficulty.heroic");
        case 5:  return L("difficulty.mythic");
        case 10: return L("difficulty.mythic_keystone");
        // Legacy/fallback WoW internal values
        case 2:  return L("difficulty.heroic");
        case 6:  return L("difficulty.25_heroic");
        case 7:  return L("difficulty.lfr");
        case 8:  return L("difficulty.mythic_keystone");
        case 9:  return L("difficulty.40_player");
        case 14: return L("difficulty.normal");
        case 15: return L("difficulty.heroic");
        case 16: return L("difficulty.mythic");
        case 17: return L("difficulty.lfr");
        case 23: return L("difficulty.mythic");
        case 24: return L("difficulty.timewalking");
        default: return "";
    }
}
