#include "PlayAlong/UIPlayerSelector.h"
#include "Core/LocalizationManager.h"
#include "Parser/SpecializationColors.h"
#include <imgui.h>
#include <cmath>

namespace PlayAlong {

void UIPlayerSelector::setPlayers(const std::vector<PlayerInfo>& players) {
    players_ = players;
}

void UIPlayerSelector::reset() {
    selection_ = PlayerSelection{};
    isOpen_ = false;
    cancelled_ = false;
    hoveredPlayer_ = -1;
}

bool UIPlayerSelector::render() {
    if (!isOpen_) {
        return false;
    }

    animationTime_ += ImGui::GetIO().DeltaTime;

    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImVec2 windowSize = viewport->Size;

    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize(windowSize);

    ImGuiWindowFlags windowFlags =
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoBringToFrontOnFocus;

    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.08f, 0.08f, 0.12f, 1.0f));

    ImGui::Begin("##PlayerSelector", nullptr, windowFlags);

    float scale = ImGui::GetIO().FontGlobalScale;

    float contentWidth = std::min(900.0f * scale, windowSize.x - 80.0f * scale);
    float contentStartX = (windowSize.x - contentWidth) * 0.5f;

    ImGui::SetCursorPosX(contentStartX);
    ImGui::BeginGroup();

    ImGui::Dummy(ImVec2(0, 60.0f * scale));

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.95f, 0.98f, 1.0f));
    ImGui::SetWindowFontScale(2.0f);

    const char* title = L("playalong.select_player");
    float titleWidth = ImGui::CalcTextSize(title).x;
    ImGui::SetCursorPosX(contentStartX + (contentWidth - titleWidth) * 0.5f);
    ImGui::Text("%s", title);

    ImGui::SetWindowFontScale(1.0f);
    ImGui::PopStyleColor();

    ImGui::Dummy(ImVec2(0, 10.0f * scale));

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.65f, 0.65f, 0.75f, 1.0f));
    const char* subtitle = L("playalong.select_player_desc");
    float subtitleWidth = ImGui::CalcTextSize(subtitle).x;
    ImGui::SetCursorPosX(contentStartX + (contentWidth - subtitleWidth) * 0.5f);
    ImGui::Text("%s", subtitle);
    ImGui::PopStyleColor();

    ImGui::Dummy(ImVec2(0, 40.0f * scale));

    float cardWidth = 200.0f * scale;
    float cardHeight = 80.0f * scale;
    float cardSpacing = 15.0f * scale;
    int cardsPerRow = static_cast<int>((contentWidth + cardSpacing) / (cardWidth + cardSpacing));
    if (cardsPerRow < 1) cardsPerRow = 1;

    float gridWidth = cardsPerRow * cardWidth + (cardsPerRow - 1) * cardSpacing;
    float gridStartX = contentStartX + (contentWidth - gridWidth) * 0.5f;

    for (size_t i = 0; i < players_.size(); i++) {
        int col = static_cast<int>(i) % cardsPerRow;
        if (col == 0 && i > 0) {
            ImGui::Dummy(ImVec2(0, cardSpacing));
        }

        if (col == 0) {
            ImGui::SetCursorPosX(gridStartX);
        } else {
            ImGui::SameLine(0, cardSpacing);
        }

        bool isHovered = (hoveredPlayer_ == static_cast<int>(i));

        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 10.0f * scale);
        ImGui::PushStyleColor(ImGuiCol_ChildBg,
            isHovered ? ImVec4(0.18f, 0.18f, 0.25f, 1.0f) : ImVec4(0.12f, 0.12f, 0.18f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Border,
            isHovered ? ImVec4(0.40f, 0.60f, 0.95f, 1.0f) : ImVec4(0.20f, 0.20f, 0.30f, 1.0f));

        char childId[32];
        snprintf(childId, sizeof(childId), "##Player%zu", i);

        if (ImGui::BeginChild(childId, ImVec2(cardWidth, cardHeight), true,
                              ImGuiWindowFlags_NoScrollbar)) {
            if (ImGui::IsWindowHovered()) {
                hoveredPlayer_ = static_cast<int>(i);
            }

            ImGui::SetCursorPos(ImVec2(15.0f * scale, 15.0f * scale));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.95f, 0.98f, 1.0f));
            ImGui::SetWindowFontScale(1.2f);
            ImGui::Text("%s", players_[i].name.c_str());
            ImGui::SetWindowFontScale(1.0f);
            ImGui::PopStyleColor();

            // Fall back to the class when the report gave no spec, rather than
            // showing a blank line
            ImGui::SetCursorPosX(15.0f * scale);
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.55f, 0.65f, 1.0f));
            if (players_[i].specId != 0) {
                ImGui::Text("%s", getSpecName(players_[i].specId));
            } else if (!players_[i].className.empty()) {
                ImGui::Text("%s", players_[i].className.c_str());
            } else {
                ImGui::Text("%s", getSpecName(0));
            }
            ImGui::PopStyleColor();

            if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(0)) {
                selection_.selected = true;
                selection_.playerGuid = players_[i].guid;
                selection_.playerName = players_[i].name;
                selection_.specId = players_[i].specId;
                isOpen_ = false;
            }
        }
        ImGui::EndChild();

        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar();
    }

    ImGui::EndGroup();

    ImGui::Dummy(ImVec2(0, 30.0f * scale));
    ImGui::SetCursorPosX(contentStartX + (contentWidth - 150.0f * scale) * 0.5f);

    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.25f, 0.25f, 0.35f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.35f, 0.35f, 0.45f, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 8.0f * scale);

    if (ImGui::Button(L("btn.back"), ImVec2(150.0f * scale, 40.0f * scale))) {
        cancelled_ = true;
        isOpen_ = false;
    }

    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);

    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        cancelled_ = true;
        isOpen_ = false;
    }

    ImGui::End();
    ImGui::PopStyleColor();

    return !isOpen_;
}

const char* UIPlayerSelector::getSpecName(uint16_t specId) const {
    return parser::getSpecName(specId);
}

} // namespace PlayAlong
