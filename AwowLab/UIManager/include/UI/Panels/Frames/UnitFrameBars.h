#pragma once
#include "Core/PowerTypeColors.h"
#include <imgui.h>
#include <cstdint>

// Health and power bars shared by the raid frames and the enemy grid
namespace unit_frame {

// Background is the bar colour at this brightness
constexpr float BAR_BACKGROUND_DIM = 0.3f;

inline void drawFilledBar(float fraction, const ImVec4& color, float width, float height) {
    ImVec4 bgColor(color.x * BAR_BACKGROUND_DIM, color.y * BAR_BACKGROUND_DIM,
                   color.z * BAR_BACKGROUND_DIM, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, color);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, bgColor);
    ImGui::ProgressBar(fraction, ImVec2(width, height), "");
    ImGui::PopStyleColor(2);
}

// Health in the unit's colour; dead units draw dark grey
inline void drawHealthBar(float healthFraction, float r, float g, float b,
                          float width, float height, bool isDead) {
    ImVec4 color = isDead ? ImVec4(0.3f, 0.3f, 0.3f, 1.0f) : ImVec4(r, g, b, 1.0f);
    drawFilledBar(healthFraction, color, width, height);
}

inline void drawPowerBar(float powerFraction, int8_t powerType, float width, float height) {
    drawFilledBar(powerFraction, ui::getPowerTypeColor(powerType), width, height);
}

} // namespace unit_frame
