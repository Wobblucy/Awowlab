#include "UI/Panels/Navigation/UILandingPage.h"
#include "UI/AwlUI/Widgets.h"
#include "UI/LandingPageColors.h"
#include "UI/UISettings.h"
#include "Core/LocalizationManager.h"
#include "Core/ErrorLogger.h"
#include "Core/SpellNameDatabase.h"
#include "Core/BrowserHelper.h"
#include "AwowLabBuildOptions.h"
#include <imgui.h>
#include <cmath>
#include <string>
#include <cstdio>

namespace {

// Width of the column the landing cards are centred in, before scaling.
constexpr float kContentWidth = 900.0f;

// Source cards (the 280-wide ones): rounding, and where the icon and the
// title block sit inside the card.
constexpr float kSourceCardRounding = 16.0f;
constexpr float kIconInsetX = 25.0f;
constexpr float kIconInsetY = 15.0f;
constexpr float kHeadingX = 70.0f;
constexpr float kHeadingY = 15.0f;
constexpr float kButtonInsetX = 15.0f;
constexpr float kButtonBottomMargin = 12.0f;

// Child window colours for a source card; the border takes the card's accent
// while hovered. Pair with popSourceCardFrame.
void pushSourceCardFrame(bool hovered, const ImVec4& accent, float scale) {
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, kSourceCardRounding * scale);
    ImGui::PushStyleColor(ImGuiCol_ChildBg,
        hovered ? LandingColors::CardBgHover : LandingColors::CardBg);
    ImGui::PushStyleColor(ImGuiCol_Border,
        hovered ? accent : LandingColors::CardBorder);
}

void popSourceCardFrame() {
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar();
}

// Top-left of the icon area, in screen space, for the card being drawn.
ImVec2 sourceCardIconOrigin(float scale) {
    ImVec2 origin = ImGui::GetCursorScreenPos();
    origin.x += kIconInsetX * scale;
    origin.y += kIconInsetY * scale;
    return origin;
}

// The disc behind a card's icon, with a pulsing halo in `glow` (its alpha is
// ignored) while the card is hovered.
void drawIconBackdrop(ImDrawList* drawList, ImVec2 iconOrigin, float scale,
                      bool hovered, ImVec4 glow, const ImVec4& disc,
                      float animationTime) {
    const ImVec2 centre(iconOrigin.x + 20.0f * scale, iconOrigin.y + 15.0f * scale);
    if (hovered) {
        glow.w = 0.3f + 0.1f * std::sin(animationTime * 3.0f);
        drawList->AddCircleFilled(centre, 35.0f * scale,
                                  ImGui::ColorConvertFloat4ToU32(glow), 32);
    }
    drawList->AddCircleFilled(centre, 25.0f * scale,
                              ImGui::ColorConvertFloat4ToU32(disc), 32);
}

// Title and wrapped description to the right of the icon.
void drawSourceCardHeading(const char* title, const char* description,
                           float cardWidth, float titleGap, float scale) {
    ImGui::SetCursorPosX(kHeadingX * scale);
    ImGui::SetCursorPosY(kHeadingY * scale);
    ImGui::BeginGroup();

    ImGui::PushStyleColor(ImGuiCol_Text, LandingColors::TextPrimary);
    ImGui::SetWindowFontScale(1.3f);
    ImGui::Text("%s", title);
    ImGui::SetWindowFontScale(1.0f);
    ImGui::PopStyleColor();

    ImGui::Dummy(ImVec2(0, titleGap * scale));

    ImGui::PushStyleColor(ImGuiCol_Text, LandingColors::TextSecondary);
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + cardWidth - 90.0f * scale);
    ImGui::TextWrapped("%s", description);
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();

    ImGui::EndGroup();
}

}  // namespace

void UILandingPage::renderSourceCards() {
    // Get UI scale factor for proper scaling at different font sizes
    float scale = ImGui::GetIO().FontGlobalScale;

    // Section title
    {
        ImGui::PushStyleColor(ImGuiCol_Text, LandingColors::TextSecondary);

        const char* sectionTitle = L("landing.choose_source");
        float titleWidth = ImGui::CalcTextSize(sectionTitle).x;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (kContentWidth * scale - titleWidth) * 0.5f);
        ImGui::Text("%s", sectionTitle);

        ImGui::PopStyleColor();
    }

    ImGui::Dummy(ImVec2(0, 20.0f * scale));

    // 2x2 grid layout for four cards
    float cardWidth = 280.0f * scale;
    float cardHeight = 200.0f * scale;
    float cardSpacing = 20.0f * scale;
    float totalWidth = cardWidth * 2 + cardSpacing;  // 2 cards per row
    float startX = (kContentWidth * scale - totalWidth) * 0.5f;

    // The two tall cards stack three buttons along the bottom edge.
    const float buttonHeight = 28.0f * scale;
    const float buttonSpacing = 5.0f * scale;
    const float buttonWidth = cardWidth - 2.0f * kButtonInsetX * scale;
    const float buttonStackY = cardHeight -
        (buttonHeight * 3 + buttonSpacing * 2 + kButtonBottomMargin * scale);

    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + startX);

    bool localHovered = (hoveredCard_ == 0);
    pushSourceCardFrame(localHovered, LandingColors::AccentBlue, scale);

    if (ImGui::BeginChild("##LocalFileCard", ImVec2(cardWidth, cardHeight), true,
                          ImGuiWindowFlags_NoScrollbar)) {

        if (ImGui::IsWindowHovered()) {
            hoveredCard_ = 0;
            ImGui::SetTooltip("%s", L("tooltip.landing_local_file"));
        }

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const ImVec2 iconPos = sourceCardIconOrigin(scale);
        drawIconBackdrop(drawList, iconPos, scale, localHovered,
                         ImVec4(0.30f, 0.50f, 0.90f, 1.0f),
                         ImVec4(0.15f, 0.25f, 0.45f, 1.0f), animationTime_);

        // Folder icon using simple shapes
        ImVec4 iconColor = localHovered ? LandingColors::AccentBlue : LandingColors::TextSecondary;
        ImU32 iconCol = ImGui::ColorConvertFloat4ToU32(iconColor);

        // Folder body
        drawList->AddRectFilled(
            ImVec2(iconPos.x + 8.0f * scale, iconPos.y + 10.0f * scale),
            ImVec2(iconPos.x + 32.0f * scale, iconPos.y + 25.0f * scale),
            iconCol, 3.0f * scale
        );
        // Folder tab
        drawList->AddRectFilled(
            ImVec2(iconPos.x + 8.0f * scale, iconPos.y + 6.0f * scale),
            ImVec2(iconPos.x + 18.0f * scale, iconPos.y + 12.0f * scale),
            iconCol, 2.0f * scale
        );

        drawSourceCardHeading(L("landing.local_file"), L("landing.local_file_desc"),
                              cardWidth, 6.0f, scale);

        ImGui::SetCursorPosY(buttonStackY);
        ImGui::SetCursorPosX(kButtonInsetX * scale);
        if (awlui::Button(L("btn.browse_files"), awlui::ButtonVariant::Primary, awlui::ButtonSize::Md, ImVec2(buttonWidth, buttonHeight))) {
            result_.choice = LandingPageChoice::LocalFile;
        }

        ImGui::SetCursorPosX(kButtonInsetX * scale);
        if (awlui::Button(L("btn.play_again"), awlui::ButtonVariant::Primary, awlui::ButtonSize::Md, ImVec2(buttonWidth, buttonHeight))) {
            result_.choice = LandingPageChoice::LocalPlayAgain;
        }

        ImGui::SetCursorPosX(kButtonInsetX * scale);
        if (awlui::Button(L("btn.open_snapshot"), awlui::ButtonVariant::Primary, awlui::ButtonSize::Md, ImVec2(buttonWidth, buttonHeight))) {
            result_.choice = LandingPageChoice::ImportSnapshot;
        }
    }
    ImGui::EndChild();
    popSourceCardFrame();

    ImGui::SameLine();
    ImGui::Dummy(ImVec2(cardSpacing - 8.0f * scale, 0));
    ImGui::SameLine();

    bool wclHovered = (hoveredCard_ == 1);
    pushSourceCardFrame(wclHovered, LandingColors::AccentPurple, scale);

    if (ImGui::BeginChild("##WarcraftLogsCard", ImVec2(cardWidth, cardHeight), true,
                          ImGuiWindowFlags_NoScrollbar)) {

        if (ImGui::IsWindowHovered()) {
            hoveredCard_ = 1;
            ImGui::SetTooltip("%s", L("tooltip.landing_warcraftlogs"));
        }

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const ImVec2 iconPos = sourceCardIconOrigin(scale);
        drawIconBackdrop(drawList, iconPos, scale, wclHovered,
                         ImVec4(0.55f, 0.35f, 0.85f, 1.0f),
                         ImVec4(0.28f, 0.18f, 0.43f, 1.0f), animationTime_);

        // Globe/cloud icon
        ImVec4 iconColor = wclHovered ? LandingColors::AccentPurple : LandingColors::TextSecondary;
        ImU32 iconCol = ImGui::ColorConvertFloat4ToU32(iconColor);

        drawList->AddCircle(
            ImVec2(iconPos.x + 20.0f * scale, iconPos.y + 15.0f * scale),
            12.0f * scale, iconCol, 24, 2.0f * scale
        );
        // Horizontal line
        drawList->AddLine(
            ImVec2(iconPos.x + 8.0f * scale, iconPos.y + 15.0f * scale),
            ImVec2(iconPos.x + 32.0f * scale, iconPos.y + 15.0f * scale),
            iconCol, 1.5f * scale
        );
        // Vertical ellipse
        drawList->AddEllipse(
            ImVec2(iconPos.x + 20.0f * scale, iconPos.y + 15.0f * scale),
            ImVec2(5.0f * scale, 12.0f * scale), iconCol, 0.0f, 16, 1.5f * scale
        );

        drawSourceCardHeading(L("landing.warcraftlogs"), L("landing.warcraftlogs_desc"),
                              cardWidth, 6.0f, scale);

        size_t cachedCount = getCachedFightCount();
        bool hasCachedFights = cachedCount > 0;

        ImGui::SetCursorPosY(buttonStackY);
        ImGui::SetCursorPosX(kButtonInsetX * scale);

        if (awlui::Button(L("btn.fetch_url"), awlui::ButtonVariant::Primary, awlui::ButtonSize::Md, ImVec2(buttonWidth, buttonHeight))) {
            wclDialogOpen_ = true;
            wclUrlDialog_->open();
        }

        // The cache buttons stay visible but disabled until a fight is cached.
        ImGui::SetCursorPosX(kButtonInsetX * scale);
        if (!hasCachedFights) ImGui::BeginDisabled();

        char browseCacheLabel[64];
        if (hasCachedFights) {
            snprintf(browseCacheLabel, sizeof(browseCacheLabel), "%s (%zu)", L("btn.browse_cache"), cachedCount);
        } else {
            snprintf(browseCacheLabel, sizeof(browseCacheLabel), "%s", L("btn.browse_cache"));
        }

        if (awlui::Button(browseCacheLabel, awlui::ButtonVariant::Secondary, awlui::ButtonSize::Md, ImVec2(buttonWidth, buttonHeight))) {
            cachedFightsPanelOpen_ = true;
            cachedFightsPanel_->open();
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip("%s", L("tooltip.landing_recent"));
        }

        ImGui::SetCursorPosX(kButtonInsetX * scale);
        if (awlui::Button(L("btn.play_along"), awlui::ButtonVariant::Primary, awlui::ButtonSize::Md, ImVec2(buttonWidth, buttonHeight))) {
            cachedFightsPanelOpen_ = true;
            cachedFightsPanel_->open();
            playAlongPending_ = true;  // Flag to enter play along after fight selection
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip("%s", L("tooltip.landing_playalong"));
        }

        if (!hasCachedFights) ImGui::EndDisabled();
    }
    ImGui::EndChild();
    popSourceCardFrame();

    ImGui::Dummy(ImVec2(0, cardSpacing));

    float overlayCardWidth = 280.0f * scale;
    float overlayCardHeight = 120.0f * scale;  // Shorter card
    float overlayStartX = (kContentWidth * scale - overlayCardWidth) * 0.5f;

    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + overlayStartX);

    bool overlayHovered = (hoveredCard_ == 2);
    ImVec4 AccentGreen = ImVec4(0.25f, 0.75f, 0.50f, 1.0f);
    pushSourceCardFrame(overlayHovered, AccentGreen, scale);

    if (ImGui::BeginChild("##OverlayModeCard", ImVec2(overlayCardWidth, overlayCardHeight), true,
                          ImGuiWindowFlags_NoScrollbar)) {

        if (ImGui::IsWindowHovered()) {
            hoveredCard_ = 2;
            ImGui::SetTooltip("%s", L("tooltip.landing_overlay"));
        }

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const ImVec2 iconPos = sourceCardIconOrigin(scale);
        drawIconBackdrop(drawList, iconPos, scale, overlayHovered, AccentGreen,
                         ImVec4(0.13f, 0.38f, 0.25f, 1.0f), animationTime_);

        // Monitor/chart icon
        ImVec4 iconColor = overlayHovered ? AccentGreen : LandingColors::TextSecondary;
        ImU32 iconCol = ImGui::ColorConvertFloat4ToU32(iconColor);

        // Monitor outline
        drawList->AddRect(
            ImVec2(iconPos.x + 8.0f * scale, iconPos.y + 5.0f * scale),
            ImVec2(iconPos.x + 32.0f * scale, iconPos.y + 22.0f * scale),
            iconCol, 2.0f * scale, 0, 2.0f * scale
        );
        // Monitor stand
        drawList->AddLine(
            ImVec2(iconPos.x + 20.0f * scale, iconPos.y + 22.0f * scale),
            ImVec2(iconPos.x + 20.0f * scale, iconPos.y + 26.0f * scale),
            iconCol, 2.0f * scale
        );
        drawList->AddLine(
            ImVec2(iconPos.x + 14.0f * scale, iconPos.y + 26.0f * scale),
            ImVec2(iconPos.x + 26.0f * scale, iconPos.y + 26.0f * scale),
            iconCol, 2.0f * scale
        );
        // Bar chart inside monitor
        drawList->AddRectFilled(
            ImVec2(iconPos.x + 12.0f * scale, iconPos.y + 15.0f * scale),
            ImVec2(iconPos.x + 15.0f * scale, iconPos.y + 19.0f * scale),
            iconCol
        );
        drawList->AddRectFilled(
            ImVec2(iconPos.x + 18.0f * scale, iconPos.y + 11.0f * scale),
            ImVec2(iconPos.x + 21.0f * scale, iconPos.y + 19.0f * scale),
            iconCol
        );
        drawList->AddRectFilled(
            ImVec2(iconPos.x + 24.0f * scale, iconPos.y + 8.0f * scale),
            ImVec2(iconPos.x + 27.0f * scale, iconPos.y + 19.0f * scale),
            iconCol
        );

        drawSourceCardHeading(L("landing.overlay_mode"), L("landing.overlay_mode_desc"),
                              overlayCardWidth, 4.0f, scale);

        // Single button at bottom, the same height as the stacked ones above
        ImGui::SetCursorPosY(overlayCardHeight - buttonHeight - kButtonBottomMargin * scale);
        ImGui::SetCursorPosX(kButtonInsetX * scale);

        if (awlui::Button(L("btn.launch_overlay"), awlui::ButtonVariant::Primary, awlui::ButtonSize::Md, ImVec2(overlayCardWidth - 2.0f * kButtonInsetX * scale, buttonHeight))) {
            result_.choice = LandingPageChoice::OverlayMode;
        }
    }
    ImGui::EndChild();
    popSourceCardFrame();

    // One card for comparing a pull against a reference, from a recorded log
    // or live.
    ImGui::Dummy(ImVec2(0, cardSpacing));

    const float compareCardWidth = 280.0f * scale;
    const float compareCardHeight = 120.0f * scale;
    const float compareStartX = (kContentWidth * scale - compareCardWidth) * 0.5f;

    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + compareStartX);

    const ImVec4 AccentAmber = ImVec4(0.98f, 0.68f, 0.20f, 1.0f);
    const bool compareHovered = (hoveredCard_ == 3);
    pushSourceCardFrame(compareHovered, AccentAmber, scale);

    if (ImGui::BeginChild("##ComparePullsCard",
                          ImVec2(compareCardWidth, compareCardHeight), true,
                          ImGuiWindowFlags_NoScrollbar)) {

        if (ImGui::IsWindowHovered()) {
            hoveredCard_ = 3;
            ImGui::SetTooltip("%s", L("tooltip.landing_compare"));
        }

        ImDrawList* compareDl = ImGui::GetWindowDrawList();
        const ImVec2 compareIcon = sourceCardIconOrigin(scale);
        drawIconBackdrop(compareDl, compareIcon, scale, compareHovered, AccentAmber,
                         ImVec4(0.38f, 0.28f, 0.10f, 1.0f), animationTime_);

        const ImU32 compareIconCol = ImGui::ColorConvertFloat4ToU32(
            compareHovered ? AccentAmber : LandingColors::TextSecondary);

        // Two bars, one taller: the comparison itself.
        compareDl->AddRectFilled(
            ImVec2(compareIcon.x + 12.0f * scale, compareIcon.y + 16.0f * scale),
            ImVec2(compareIcon.x + 17.0f * scale, compareIcon.y + 26.0f * scale),
            compareIconCol);
        compareDl->AddRectFilled(
            ImVec2(compareIcon.x + 22.0f * scale, compareIcon.y + 8.0f * scale),
            ImVec2(compareIcon.x + 27.0f * scale, compareIcon.y + 26.0f * scale),
            compareIconCol);

        drawSourceCardHeading(L("landing.compare"), L("landing.compare_desc"),
                              compareCardWidth, 4.0f, scale);

        // Three buttons: prepare a reference, then use it either against a
        // recorded log or against pulls as they happen. Preparing comes first
        // because the other two have nothing to offer until it has been done.
        const float compareButtonH = 26.0f * scale;
        const float compareButtonGap = 6.0f * scale;
        const float compareButtonW =
            (compareCardWidth - 2.0f * kButtonInsetX * scale - compareButtonGap * 2.0f) / 3.0f;

        ImGui::SetCursorPosY(compareCardHeight - compareButtonH - kButtonBottomMargin * scale);
        ImGui::SetCursorPosX(kButtonInsetX * scale);

        // A reference can come from a cached report or a local combat log, so
        // this asks which.
        if (awlui::Button(L("btn.prepare_mentor"), awlui::ButtonVariant::Primary,
                          awlui::ButtonSize::Sm,
                          ImVec2(compareButtonW, compareButtonH))) {
            ImGui::OpenPopup("##PrepareSource");
        }

        if (ImGui::BeginPopup("##PrepareSource")) {
            if (ImGui::MenuItem(L("prepare.source_cache"))) {
                prepareMentorPending_ = true;
                cachedFightsPanelOpen_ = true;
                if (cachedFightsPanel_) cachedFightsPanel_->open();
                AWOW_LOG_INFO("BENCHMARK",
                    "Prepare pressed - cache browser opened, awaiting a fight pick");
            }
            if (ImGui::MenuItem(L("prepare.source_local"))) {
                AWOW_LOG_INFO("BENCHMARK", "Prepare pressed - local log requested");
                result_.choice = LandingPageChoice::PrepareMentorLocal;
            }
            ImGui::EndPopup();
        }

        // The other two need a reference to exist. Showing them disabled says
        // the feature is there and what unlocks it, which a hidden button
        // cannot.
        const bool haveMentors =
            !benchmark::BenchmarkSession::instance().storedMentors().empty();

        ImGui::SameLine(0.0f, compareButtonGap);
        if (!haveMentors) ImGui::BeginDisabled();

        if (awlui::Button(L("btn.compare_report"), awlui::ButtonVariant::Secondary,
                          awlui::ButtonSize::Sm,
                          ImVec2(compareButtonW, compareButtonH))) {
            mentorPickerMode_ = 1;   // Reference and the pull to measure
            mentorPickerOpen_ = true;
            if (mentorPicker_) {
                mentorPicker_->open(
                    benchmark_ui::UIMentorPicker::Mode::ReferenceAndFight);
            }
        }

        ImGui::SameLine(0.0f, compareButtonGap);

        if (awlui::Button(L("btn.compare_live"), awlui::ButtonVariant::Secondary,
                          awlui::ButtonSize::Sm,
                          ImVec2(compareButtonW, compareButtonH))) {
            mentorPickerMode_ = 0;   // Reference only
            mentorPickerOpen_ = true;
            if (mentorPicker_) {
                mentorPicker_->open(
                    benchmark_ui::UIMentorPicker::Mode::ReferenceOnly);
            }
        }

        if (!haveMentors) {
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                ImGui::SetTooltip("%s", L("tooltip.needs_mentor"));
            }
        }
    }
    ImGui::EndChild();
    popSourceCardFrame();

    // Exports a fight's tables as a zip, reusing the fight pickers from the
    // rows above.
    ImGui::Dummy(ImVec2(0, cardSpacing));

    const float packageCardWidth = 280.0f * scale;
    // Tall enough that the wrapped description clears both button rows.
    const float packageCardHeight = 168.0f * scale;
    const float packageStartX = (kContentWidth * scale - packageCardWidth) * 0.5f;

    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + packageStartX);

    const ImVec4 AccentTeal = ImVec4(0.30f, 0.78f, 0.72f, 1.0f);
    const bool packageHovered = (hoveredCard_ == 4);
    pushSourceCardFrame(packageHovered, AccentTeal, scale);

    if (ImGui::BeginChild("##PackageAnalysisCard",
                          ImVec2(packageCardWidth, packageCardHeight), true,
                          ImGuiWindowFlags_NoScrollbar)) {

        if (ImGui::IsWindowHovered()) {
            hoveredCard_ = 4;
            ImGui::SetTooltip("%s", L("tooltip.landing_package"));
        }

        ImDrawList* packageDl = ImGui::GetWindowDrawList();
        const ImVec2 packageIcon = sourceCardIconOrigin(scale);
        drawIconBackdrop(packageDl, packageIcon, scale, packageHovered, AccentTeal,
                         ImVec4(0.10f, 0.30f, 0.29f, 1.0f), animationTime_);

        const ImU32 packageIconCol = ImGui::ColorConvertFloat4ToU32(
            packageHovered ? AccentTeal : LandingColors::TextSecondary);

        // A box with a lid: the archive this produces.
        packageDl->AddRect(
            ImVec2(packageIcon.x + 11.0f * scale, packageIcon.y + 13.0f * scale),
            ImVec2(packageIcon.x + 29.0f * scale, packageIcon.y + 27.0f * scale),
            packageIconCol, 2.0f * scale, 0, 1.6f * scale);
        packageDl->AddRectFilled(
            ImVec2(packageIcon.x + 11.0f * scale, packageIcon.y + 13.0f * scale),
            ImVec2(packageIcon.x + 29.0f * scale, packageIcon.y + 18.0f * scale),
            packageIconCol);

        drawSourceCardHeading(L("landing.package"), L("landing.package_desc"),
                              packageCardWidth, 4.0f, scale);

        const float packageButtonH = 26.0f * scale;
        const float packageButtonGap = 6.0f * scale;
        const float packageRowW = packageCardWidth - 2.0f * kButtonInsetX * scale;

        // Two rows. The top one exports a fight you already have; the bottom
        // starts a baseline from the season's rankings, which is a different
        // job and needs nothing loaded.
        ImGui::SetCursorPosY(packageCardHeight - (packageButtonH * 2.0f)
                             - packageButtonGap - kButtonBottomMargin * scale);
        ImGui::SetCursorPosX(kButtonInsetX * scale);

        if (awlui::Button(L("btn.package_export"), awlui::ButtonVariant::Primary,
                          awlui::ButtonSize::Sm,
                          ImVec2(packageRowW, packageButtonH))) {
            result_.choice = LandingPageChoice::PackageForAnalysis;
        }

        // Dungeons and raids are separate because WCL ranks them differently -
        // a keystone run per dungeon, a boss per encounter - so the query is
        // not the same one with a flag flipped.
        const float thirdW = (packageRowW - packageButtonGap * 2.0f) / 3.0f;
        ImGui::SetCursorPosX(kButtonInsetX * scale);

        if (awlui::Button(L("btn.baseline_dungeon"), awlui::ButtonVariant::Ghost,
                          awlui::ButtonSize::Sm, ImVec2(thirdW, packageButtonH))) {
            result_.choice = LandingPageChoice::FetchRankingsDungeon;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", L("tooltip.baseline_dungeon"));
        }

        ImGui::SameLine(0.0f, packageButtonGap);

        if (awlui::Button(L("btn.baseline_raid"), awlui::ButtonVariant::Ghost,
                          awlui::ButtonSize::Sm, ImVec2(thirdW, packageButtonH))) {
            result_.choice = LandingPageChoice::FetchRankingsRaid;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", L("tooltip.baseline_raid"));
        }

        ImGui::SameLine(0.0f, packageButtonGap);

        // The season list only changes when a patch lands, so it is read from
        // a local copy. This is how the user says a patch has landed - the app
        // would only be guessing.
        if (awlui::Button(L("btn.baseline_rebuild"), awlui::ButtonVariant::Ghost,
                          awlui::ButtonSize::Sm, ImVec2(thirdW, packageButtonH))) {
            result_.choice = LandingPageChoice::RebuildSeasonList;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", L("tooltip.baseline_rebuild"));
        }

        // What the last action did. The season list is written somewhere the
        // user is not looking, so without this a successful rebuild is
        // indistinguishable from the button doing nothing.
        if (!packageStatus_.empty()) {
            ImGui::SetCursorPosX(kButtonInsetX * scale);
            ImGui::PushStyleColor(ImGuiCol_Text,
                packageStatusIsError_ ? ImVec4(0.90f, 0.45f, 0.40f, 1.0f)
                                      : ImVec4(0.45f, 0.82f, 0.60f, 1.0f));
            ImGui::SetWindowFontScale(0.9f);
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + packageRowW);
            ImGui::TextWrapped("%s", packageStatus_.c_str());
            ImGui::PopTextWrapPos();
            ImGui::SetWindowFontScale(1.0f);
            ImGui::PopStyleColor();
        }
    }
    ImGui::EndChild();
    popSourceCardFrame();
}

void UILandingPage::renderLanguagePackCard() {
    // Only show for locales that have optional language packs
    auto& locMgr = LocalizationManager::instance();
    Locale currentLocale = locMgr.getLocale();

    // Only CJK locales have optional packs (Korean, Simplified Chinese, Traditional Chinese)
    bool needsOptionalPack = (currentLocale == Locale::ko_KR ||
                              currentLocale == Locale::zh_CN ||
                              currentLocale == Locale::zh_TW);

    if (!needsOptionalPack) {
        return;  // English and other Latin locales don't need optional packs
    }

    // Check if the language pack is already installed
    auto& spellDb = SpellNameDatabase::instance();
    if (spellDb.hasLocaleFile(currentLocale)) {
        return;  // Pack already installed, don't show prompt
    }

    // User dismissed the prompt this session
    if (languagePackPromptDismissed_) {
        return;
    }

    // Get UI scale factor for proper scaling at different font sizes
    float scale = ImGui::GetIO().FontGlobalScale;

    float cardWidth = 420.0f * scale;
    float cardHeight = 120.0f * scale;
    float startX = (kContentWidth * scale - cardWidth) * 0.5f;

    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + startX);

    // Cyan/teal accent for download card
    ImVec4 AccentCyan = ImVec4(0.30f, 0.70f, 0.85f, 1.0f);

    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 10.0f * scale);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.12f, 0.14f, 0.18f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Border, AccentCyan);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.5f * scale);

    if (ImGui::BeginChild("##LanguagePackCard", ImVec2(cardWidth, cardHeight), true,
                          ImGuiWindowFlags_NoScrollbar)) {

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        ImVec2 iconPos = ImGui::GetCursorScreenPos();
        iconPos.x += 15.0f * scale;
        iconPos.y += 10.0f * scale;

        // Draw icon background circle with subtle glow (cyan tint)
        float glowAlpha = 0.25f + 0.1f * std::sin(animationTime_ * 2.5f);
        drawList->AddCircleFilled(
            ImVec2(iconPos.x + 20.0f * scale, iconPos.y + 20.0f * scale),
            35.0f * scale,
            ImGui::ColorConvertFloat4ToU32(ImVec4(0.30f, 0.70f, 0.85f, glowAlpha)),
            32
        );
        drawList->AddCircleFilled(
            ImVec2(iconPos.x + 20.0f * scale, iconPos.y + 20.0f * scale),
            25.0f * scale,
            ImGui::ColorConvertFloat4ToU32(ImVec4(0.15f, 0.35f, 0.43f, 1.0f)),
            32
        );

        // Draw download arrow icon (cyan)
        ImU32 iconCol = ImGui::ColorConvertFloat4ToU32(AccentCyan);
        float cx = iconPos.x + 20.0f * scale;
        float cy = iconPos.y + 20.0f * scale;

        // Arrow body (vertical line)
        drawList->AddRectFilled(
            ImVec2(cx - 2.0f * scale, cy - 10.0f * scale),
            ImVec2(cx + 2.0f * scale, cy + 4.0f * scale),
            iconCol
        );
        // Arrow head (triangle pointing down)
        drawList->AddTriangleFilled(
            ImVec2(cx - 8.0f * scale, cy + 2.0f * scale),
            ImVec2(cx + 8.0f * scale, cy + 2.0f * scale),
            ImVec2(cx, cy + 12.0f * scale),
            iconCol
        );
        // Base line
        drawList->AddRectFilled(
            ImVec2(cx - 10.0f * scale, cy + 8.0f * scale),
            ImVec2(cx + 10.0f * scale, cy + 11.0f * scale),
            iconCol
        );

        // Title and description next to icon
        ImGui::SetCursorPosX(80.0f * scale);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10.0f * scale);

        ImGui::BeginGroup();

        // Title
        ImGui::PushStyleColor(ImGuiCol_Text, LandingColors::TextPrimary);
        ImGui::SetWindowFontScale(1.15f);
        ImGui::Text("%s", L("langpack.title"));
        ImGui::SetWindowFontScale(1.0f);
        ImGui::PopStyleColor();

        ImGui::Dummy(ImVec2(0, 4.0f * scale));

        // Description with locale name
        ImGui::PushStyleColor(ImGuiCol_Text, LandingColors::TextSecondary);
        const char* localeName = LocalizationManager::getLocaleDisplayName(currentLocale);
        char descBuf[256];
        snprintf(descBuf, sizeof(descBuf), L("langpack.desc"), localeName);
        ImGui::TextWrapped("%s", descBuf);
        ImGui::PopStyleColor();

        // Size hint
        ImGui::PushStyleColor(ImGuiCol_Text, LandingColors::TextMuted);
        ImGui::SetWindowFontScale(0.85f);
#if AWOWLAB_ENABLE_UPDATER
        // Approximate pack sizes: Korean ~17MB (font + spell names)
        int sizeHint = 17;  // MB
        char sizeBuf[64];
        snprintf(sizeBuf, sizeof(sizeBuf), L("langpack.size_hint"), sizeHint);
        ImGui::Text("%s", sizeBuf);
#else
        // Packs come down through the updater, which this build doesn't have
        ImGui::TextWrapped("%s", L("langpack.disabled_build"));
#endif
        ImGui::SetWindowFontScale(1.0f);
        ImGui::PopStyleColor();

        ImGui::EndGroup();

        // Buttons at the bottom right
        float buttonWidth = 110.0f * scale;
        float buttonHeight = 28.0f * scale;
#if AWOWLAB_ENABLE_UPDATER
        float buttonSpacing = 10.0f * scale;
        float buttonsX = cardWidth - (buttonWidth * 2 + buttonSpacing + 15.0f * scale);
#else
        float buttonsX = cardWidth - (buttonWidth + 15.0f * scale);
#endif

        ImGui::SetCursorPosY(cardHeight - buttonHeight - 12.0f * scale);
        ImGui::SetCursorPosX(buttonsX);

        if (awlui::Button(L("langpack.no_thanks"), awlui::ButtonVariant::Ghost, awlui::ButtonSize::Md, ImVec2(buttonWidth, buttonHeight))) {
            languagePackPromptDismissed_ = true;
        }

#if AWOWLAB_ENABLE_UPDATER
        ImGui::SameLine(0, buttonSpacing);

        if (awlui::Button(L("langpack.download"), awlui::ButtonVariant::Primary, awlui::ButtonSize::Md, ImVec2(buttonWidth, buttonHeight))) {
            // Launch updater with optional pack argument
            // Format: --optional-pack ko_KR (or zh_CN, zh_TW)
            std::string localeCode = LocalizationManager::getLocaleCode(currentLocale);
            std::string args = "--optional-pack " + localeCode;
            launchUpdater(args);
        }
#endif
    }
    ImGui::EndChild();

    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);

    ImGui::Dummy(ImVec2(0, 20.0f * scale));  // Spacing after card
}
