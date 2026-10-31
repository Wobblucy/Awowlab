#pragma once

#include <imgui.h>

// Shared color constants for landing page UI
// Using inline constexpr to avoid multiple definition issues in unity builds
namespace LandingColors {
    // Main background - translucent dark
    inline constexpr ImVec4 Background = ImVec4(0.06f, 0.06f, 0.07f, 0.78f);
    inline constexpr ImVec4 BackgroundDark = ImVec4(0.04f, 0.04f, 0.05f, 0.82f);

    // Card colors - translucent
    inline constexpr ImVec4 CardBg = ImVec4(0.08f, 0.08f, 0.10f, 0.70f);
    inline constexpr ImVec4 CardBgHover = ImVec4(0.12f, 0.12f, 0.16f, 0.80f);
    inline constexpr ImVec4 CardBorder = ImVec4(0.60f, 0.42f, 0.15f, 0.40f);
    inline constexpr ImVec4 CardBorderHover = ImVec4(0.80f, 0.55f, 0.20f, 0.70f);

    // Accent colors - warm amber
    inline constexpr ImVec4 AccentAmber = ImVec4(0.85f, 0.58f, 0.18f, 1.0f);
    inline constexpr ImVec4 AccentAmberBright = ImVec4(0.95f, 0.70f, 0.25f, 1.0f);
    inline constexpr ImVec4 AccentGold = ImVec4(0.85f, 0.65f, 0.25f, 1.0f);
    inline constexpr ImVec4 AccentBlue = ImVec4(0.30f, 0.50f, 0.90f, 1.0f);
    inline constexpr ImVec4 AccentPurple = ImVec4(0.55f, 0.35f, 0.85f, 1.0f);

    // Text colors - warm white
    inline constexpr ImVec4 TextPrimary = ImVec4(0.98f, 0.95f, 0.88f, 1.0f);
    inline constexpr ImVec4 TextSecondary = ImVec4(0.70f, 0.65f, 0.55f, 1.0f);
    inline constexpr ImVec4 TextMuted = ImVec4(0.50f, 0.45f, 0.38f, 1.0f);

    // Button colors - amber
    inline constexpr ImVec4 ButtonPrimary = ImVec4(0.55f, 0.38f, 0.14f, 0.90f);
    inline constexpr ImVec4 ButtonPrimaryHover = ImVec4(0.80f, 0.55f, 0.18f, 1.0f);
    inline constexpr ImVec4 ButtonSecondary = ImVec4(0.35f, 0.28f, 0.15f, 0.85f);
    inline constexpr ImVec4 ButtonSecondaryHover = ImVec4(0.50f, 0.38f, 0.18f, 0.95f);
}
