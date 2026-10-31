#include "UI/Panels/Navigation/UILandingPage.h"
#include "UI/LandingPageColors.h"
#include <imgui.h>

void UILandingPage::applyLandingTheme() {
    ImGuiStyle& style = ImGui::GetStyle();

    // Rounded corners
    style.WindowRounding = 12.0f;
    style.FrameRounding = 8.0f;
    style.PopupRounding = 8.0f;
    style.ScrollbarRounding = 8.0f;
    style.GrabRounding = 8.0f;
    style.TabRounding = 8.0f;
    style.ChildRounding = 8.0f;

    // Padding and spacing
    style.WindowPadding = ImVec2(20.0f, 20.0f);
    style.FramePadding = ImVec2(12.0f, 8.0f);
    style.ItemSpacing = ImVec2(12.0f, 10.0f);
    style.ItemInnerSpacing = ImVec2(8.0f, 6.0f);

    // Borders
    style.WindowBorderSize = 0.0f;
    style.FrameBorderSize = 1.0f;
    style.PopupBorderSize = 1.0f;

    // Colors
    ImVec4* colors = style.Colors;
    colors[ImGuiCol_WindowBg] = LandingColors::Background;
    colors[ImGuiCol_ChildBg] = LandingColors::CardBg;
    colors[ImGuiCol_PopupBg] = ImVec4(0.08f, 0.08f, 0.09f, 1.00f);  // Menus opaque
    colors[ImGuiCol_Border] = LandingColors::CardBorder;
    colors[ImGuiCol_BorderShadow] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);

    colors[ImGuiCol_FrameBg] = ImVec4(0.08f, 0.07f, 0.06f, 0.70f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.14f, 0.11f, 0.08f, 0.80f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.20f, 0.15f, 0.08f, 0.88f);

    colors[ImGuiCol_TitleBg] = ImVec4(0.05f, 0.05f, 0.05f, 0.82f);
    colors[ImGuiCol_TitleBgActive] = ImVec4(0.16f, 0.11f, 0.05f, 0.88f);
    colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.04f, 0.04f, 0.04f, 0.70f);

    colors[ImGuiCol_Button] = LandingColors::ButtonPrimary;
    colors[ImGuiCol_ButtonHovered] = LandingColors::ButtonPrimaryHover;
    colors[ImGuiCol_ButtonActive] = ImVec4(0.95f, 0.68f, 0.22f, 1.0f);

    colors[ImGuiCol_Header] = ImVec4(0.55f, 0.36f, 0.12f, 0.75f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.80f, 0.52f, 0.15f, 0.90f);
    colors[ImGuiCol_HeaderActive] = ImVec4(0.92f, 0.62f, 0.18f, 1.0f);

    colors[ImGuiCol_Text] = LandingColors::TextPrimary;
    colors[ImGuiCol_TextDisabled] = LandingColors::TextMuted;

    colors[ImGuiCol_ScrollbarBg] = ImVec4(0.02f, 0.02f, 0.025f, 0.60f);
    colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.50f, 0.35f, 0.14f, 0.75f);
    colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.68f, 0.48f, 0.18f, 0.90f);
    colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.85f, 0.60f, 0.22f, 1.0f);

    colors[ImGuiCol_Separator] = ImVec4(0.60f, 0.42f, 0.16f, 0.50f);
    colors[ImGuiCol_SeparatorHovered] = LandingColors::AccentAmber;
    colors[ImGuiCol_SeparatorActive] = LandingColors::AccentAmberBright;
}
