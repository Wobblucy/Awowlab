#pragma once
#include <imgui.h>
#include <cstdint>
#include <cstdio>

// Text drawn on top of spell icons: countdowns and stack counts, with a
// black outline so they stay readable over any icon art.
namespace icon_overlay {

inline void drawOutlinedText(ImDrawList* drawList, float x, float y, const char* text) {
    constexpr ImU32 outlineColor = IM_COL32(0, 0, 0, 255);
    constexpr ImU32 textColor = IM_COL32(255, 255, 255, 255);
    constexpr float offsets[][2] = {
        {-1, -1}, {1, -1}, {-1, 1}, {1, 1},
        {-1, 0}, {1, 0}, {0, -1}, {0, 1},
    };
    for (const auto& offset : offsets) {
        drawList->AddText(ImVec2(x + offset[0], y + offset[1]), outlineColor, text);
    }
    drawList->AddText(ImVec2(x, y), textColor, text);
}

// Time left on an aura that ends at removedAtMs. Zero when it has already
// ended or has no end in the log (removedAtMs == INT32_MAX), so no
// countdown is drawn for it.
inline uint32_t auraRemainingMs(int32_t removedAtMs, uint32_t currentTimeMs) {
    if (removedAtMs == INT32_MAX) return 0;
    if (removedAtMs <= static_cast<int32_t>(currentTimeMs)) return 0;
    return static_cast<uint32_t>(removedAtMs) - currentTimeMs;
}

// "1:05" from a minute up, whole seconds from 10s, tenths below that
inline void formatCountdown(char* buffer, size_t bufferSize, uint32_t remainingMs) {
    float remainingSec = remainingMs / 1000.0f;
    if (remainingSec >= 60.0f) {
        int minutes = static_cast<int>(remainingSec / 60.0f);
        int seconds = static_cast<int>(remainingSec) % 60;
        std::snprintf(buffer, bufferSize, "%d:%02d", minutes, seconds);
    } else if (remainingSec >= 10.0f) {
        std::snprintf(buffer, bufferSize, "%.0f", remainingSec);
    } else {
        std::snprintf(buffer, bufferSize, "%.1f", remainingSec);
    }
}

// Countdown centred on a square icon
inline void drawCountdown(ImDrawList* drawList, ImVec2 iconMin, float iconSize, uint32_t remainingMs) {
    char timerText[16];
    formatCountdown(timerText, sizeof(timerText), remainingMs);
    ImVec2 textSize = ImGui::CalcTextSize(timerText);
    drawOutlinedText(drawList,
                     iconMin.x + (iconSize - textSize.x) * 0.5f,
                     iconMin.y + (iconSize - textSize.y) * 0.5f,
                     timerText);
}

// Stack count in the bottom-right corner; nothing for a single stack
inline void drawStackCount(ImDrawList* drawList, ImVec2 iconMax, uint32_t stacks) {
    if (stacks <= 1) return;
    char stackText[16];
    std::snprintf(stackText, sizeof(stackText), "%u", stacks);
    ImVec2 textSize = ImGui::CalcTextSize(stackText);
    drawOutlinedText(drawList, iconMax.x - textSize.x - 2, iconMax.y - textSize.y - 2, stackText);
}

} // namespace icon_overlay
