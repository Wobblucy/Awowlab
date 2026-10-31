#pragma once

#include <imgui.h>

// AwlUI - AwowLab's thin custom widget layer over Dear ImGui.
//
// Design tokens (spacing, radii, colours, font handles) used by the awlui
// widgets. Change a value here and every custom widget picks it up.
//
// The amber accent is reserved for states that signal a primary action or
// focus:
//
//   - awlui::Button(Primary)          fills with Accent
//   - active TabBar underline         2px Accent
//   - Checkbox / Toggle "on" state    fill Accent
//   - Slider fill up to grab          Accent
//   - keyboard focus outline          NavHighlight (== Accent)
//   - text selection highlight        TextSelectedBg (Accent, low alpha)
//   - plot data series                Accent gradient
//
// Everything else (hover, secondary buttons, idle combos and tabs, section
// dividers) uses the neutral Surface* palette: Surface2 for hover, Surface3
// for pressed.
//
// The base ImGui theme in ImGuiManager.cpp uses the same tokens for the
// built-in widgets, so they match the custom ones.
namespace awlui { namespace theme {

inline constexpr float SpaceXS = 4.0f;
inline constexpr float SpaceS  = 8.0f;
inline constexpr float SpaceM  = 12.0f;
inline constexpr float SpaceL  = 16.0f;
inline constexpr float SpaceXL = 24.0f;

inline constexpr float RadiusCard    = 8.0f;
inline constexpr float RadiusControl = 6.0f;
inline constexpr float RadiusPill    = 999.0f;   // sentinel; widgets use height/2

inline constexpr float ControlHeight       = 28.0f;
inline constexpr float ButtonPadX          = 14.0f;
inline constexpr float SectionHeaderHeight = 32.0f;
inline constexpr float TabHeight           = 32.0f;
inline constexpr float CheckboxBox         = 16.0f;

inline constexpr float BorderThin = 1.0f;

// Surface palette. Layered from page (0) to strongly-lifted (3).
inline constexpr ImVec4 Surface0 = ImVec4(0.055f, 0.058f, 0.065f, 1.00f);
inline constexpr ImVec4 Surface1 = ImVec4(0.085f, 0.090f, 0.100f, 1.00f);
inline constexpr ImVec4 Surface2 = ImVec4(0.115f, 0.122f, 0.135f, 1.00f);
inline constexpr ImVec4 Surface3 = ImVec4(0.155f, 0.163f, 0.180f, 1.00f);

inline constexpr ImVec4 BorderSubtle = ImVec4(1.00f, 1.00f, 1.00f, 0.06f);
inline constexpr ImVec4 BorderStrong = ImVec4(1.00f, 1.00f, 1.00f, 0.10f);

inline constexpr ImVec4 TextPrimary   = ImVec4(0.94f, 0.95f, 0.97f, 1.00f);
inline constexpr ImVec4 TextSecondary = ImVec4(0.65f, 0.68f, 0.73f, 1.00f);
inline constexpr ImVec4 TextMuted     = ImVec4(0.45f, 0.48f, 0.52f, 1.00f);

// Accent - reserved (see top-of-file rules).
inline constexpr ImVec4 Accent        = ImVec4(0.98f, 0.68f, 0.20f, 1.00f);
inline constexpr ImVec4 AccentHover   = ImVec4(1.00f, 0.75f, 0.28f, 1.00f);
inline constexpr ImVec4 AccentActive  = ImVec4(1.00f, 0.82f, 0.35f, 1.00f);
inline constexpr ImVec4 AccentSubtle  = ImVec4(0.98f, 0.68f, 0.20f, 0.14f);

// Semantic - use for status / danger, not accent.
inline constexpr ImVec4 Success = ImVec4(0.35f, 0.80f, 0.55f, 1.00f);
inline constexpr ImVec4 Warn    = ImVec4(0.95f, 0.75f, 0.30f, 1.00f);
inline constexpr ImVec4 Danger  = ImVec4(0.90f, 0.35f, 0.35f, 1.00f);

// Categorical series colours, for charts where each slice is a different
// category rather than a different amount. The reserved accent is left out so
// no slice looks highlighted. Hues vary, not just lightness, so adjacent
// slices stay distinguishable on a dark surface.
inline constexpr ImVec4 Series[] = {
    ImVec4(0.40f, 0.65f, 0.95f, 1.00f),   // blue
    ImVec4(0.45f, 0.80f, 0.60f, 1.00f),   // green
    ImVec4(0.85f, 0.50f, 0.75f, 1.00f),   // magenta
    ImVec4(0.55f, 0.75f, 0.35f, 1.00f),   // olive
    ImVec4(0.90f, 0.55f, 0.40f, 1.00f),   // coral
    ImVec4(0.60f, 0.55f, 0.90f, 1.00f),   // violet
    ImVec4(0.40f, 0.78f, 0.82f, 1.00f),   // teal
    ImVec4(0.55f, 0.58f, 0.65f, 1.00f),   // slate, for the folded remainder
};

inline constexpr int SeriesCount =
    static_cast<int>(sizeof(Series) / sizeof(Series[0]));

// The colour for slice n, wrapping if a chart somehow has more.
inline ImVec4 seriesColour(size_t index) {
    return Series[index % static_cast<size_t>(SeriesCount)];
}

// Font handles, set by CaptureFonts() at the end of ImGui init. Null when a
// font failed to load; PushFont(nullptr) keeps the default font.
inline ImFont* gFontMedium = nullptr;   // semibold body / section headers
inline ImFont* gFontMono   = nullptr;   // monospace for numeric spans

// Called at the end of ImGuiManager::initialize(), after the fonts are
// loaded, to store the Medium and Mono fonts on the globals above.
void CaptureFonts(ImFont* medium, ImFont* mono);

}}  // namespace awlui::theme
