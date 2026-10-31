#pragma once

#include <imgui.h>
#include "UI/AwlUI/Theme.h"

// AwlUI widgets: custom drawing on top of ImGui's interaction primitives
// (InvisibleButton for hit-testing and layout, IsItemHovered/Active for state,
// the window draw list for pixels). Same approach as
// src/Core/MeterBarRenderer.cpp.
//
// Each widget takes a label or id for its ImGui id, and draws a focus outline
// when IsItemFocused() so keyboard navigation matches the built-in widgets.
namespace awlui {

enum class ButtonVariant {
    Primary,     // reserved-accent fill; use for the CTA on a surface
    Secondary,   // neutral Surface2 fill; use for non-primary actions
    Ghost,       // no fill idle, subtle hover; use for chrome / toggles
    Danger,      // Danger-colored fill; destructive actions
};

enum class ButtonSize {
    Sm,   // 22px height, tighter padding
    Md,   // 28px height (default)
};

// Returns true on click, like ImGui::Button. A zero size.x or size.y is
// sized to fit the label and padding.
bool Button(const char* label,
            ButtonVariant variant = ButtonVariant::Secondary,
            ButtonSize sizePreset = ButtonSize::Md,
            ImVec2 size = ImVec2(0, 0));

// Square icon-only button (close, lock, chevron). `glyph` is a short string
// centred in the button.
bool IconButton(const char* id,
                const char* glyph,
                ButtonVariant variant = ButtonVariant::Ghost);

// Returns true on the frame the state changes.
bool Checkbox(const char* label, bool* v);

// Pill switch, for options that toggle a mode (auto-freeze, click-through)
// rather than filter-style flags.
bool Toggle(const char* label, bool* v);

// Slider with the label and current value on a row above the track.
// Returns true on the frame the value changes.
bool SliderFloat(const char* label, float* v, float lo, float hi,
                 const char* fmt = "%.2f");
bool SliderInt(const char* label, int* v, int lo, int hi,
               const char* fmt = "%d");

// Combo box shaped like a Secondary button with a chevron. `current` is a
// 0-based index into `items`. Returns true when the selection changes.
bool Combo(const char* label,
           int* current,
           const char* const items[],
           int count);

// Flat segmented tab strip, split equally across the available width.
// Returns true when the selection changes.
bool TabBar(const char* id,
            int* current,
            const char* const items[],
            int count);

// Collapsible section header. Returns whether the section is open.
// EndSection must be called after every BeginSection, whether it returned
// true or false.
bool BeginSection(const char* label, bool defaultOpen = true);
void EndSection();

// Text in the monospaced font (gFontMono) when it is loaded, for numbers
// that need to line up in columns.
void TextNumber(const char* fmt, ...);

// Small spinner with an inline label, for work in progress such as a log
// re-parse. Advances the cursor like TextUnformatted.
void Spinner(const char* label, float radius = 8.0f);

// Centred block for panels with nothing to show: a large glyph, a headline,
// an optional hint and an optional action button. Returns true on the frame
// the button is clicked.
bool EmptyState(const char* id,
                const char* glyph,
                const char* headline,
                const char* hint = nullptr,
                const char* actionLabel = nullptr);

}  // namespace awlui
