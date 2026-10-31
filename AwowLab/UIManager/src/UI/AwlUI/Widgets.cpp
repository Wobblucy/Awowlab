#include "UI/AwlUI/Widgets.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace awlui {

namespace T = theme;

namespace {

ImU32 U32(ImVec4 c) { return ImGui::ColorConvertFloat4ToU32(c); }

// Height for a size preset. An explicit size passed to Button overrides it.
float sizePresetHeight(ButtonSize sz) {
    switch (sz) {
        case ButtonSize::Sm: return 22.0f;
        case ButtonSize::Md:
        default:             return T::ControlHeight;   // 28
    }
}

float sizePresetPadX(ButtonSize sz) {
    switch (sz) {
        case ButtonSize::Sm: return T::SpaceS + 2.0f;   // 10
        case ButtonSize::Md:
        default:             return T::ButtonPadX;       // 14
    }
}

struct ButtonPalette {
    ImVec4 fill;
    ImVec4 text;
    ImVec4 border;   // alpha 0 = no border
};

// Disabled buttons need no palette of their own: ImGui::BeginDisabled at the
// call site blocks clicks and dims whatever is drawn.
ButtonPalette buttonPalette(ButtonVariant v, bool hovered, bool active) {
    ButtonPalette p{};
    switch (v) {
        case ButtonVariant::Primary:
            p.fill   = active ? T::AccentActive : (hovered ? T::AccentHover : T::Accent);
            p.text   = T::Surface0;
            p.border = ImVec4(0, 0, 0, 0);
            break;
        case ButtonVariant::Secondary:
            p.fill   = active ? T::Surface3 : (hovered ? T::Surface3 : T::Surface2);
            p.text   = T::TextPrimary;
            p.border = T::BorderSubtle;
            break;
        case ButtonVariant::Ghost:
            // No fill when idle; a translucent Surface2 on hover.
            if (active)       p.fill = T::Surface3;
            else if (hovered) p.fill = ImVec4(T::Surface2.x, T::Surface2.y, T::Surface2.z, 0.60f);
            else              p.fill = ImVec4(0, 0, 0, 0);
            p.text   = hovered ? T::TextPrimary : T::TextSecondary;
            p.border = ImVec4(0, 0, 0, 0);
            break;
        case ButtonVariant::Danger:
            p.fill   = active
                ? ImVec4(T::Danger.x * 1.10f, T::Danger.y * 1.10f, T::Danger.z * 1.10f, 1.00f)
                : (hovered
                    ? ImVec4(T::Danger.x * 1.05f, T::Danger.y * 1.05f, T::Danger.z * 1.05f, 1.00f)
                    : T::Danger);
            p.text   = T::TextPrimary;
            p.border = ImVec4(0, 0, 0, 0);
            break;
    }
    return p;
}

// Keyboard focus outline matching the theme's NavHighlight, so custom
// widgets show the same focus signal as built-in ones.
void drawFocusOutline(ImDrawList* draw, ImVec2 min, ImVec2 max, float radius) {
    draw->AddRect(min, max, U32(T::Accent), radius, 0, 2.0f);
}

// Fill and border shared by Button and IconButton.
void drawButtonFrame(ImDrawList* draw, ImVec2 min, ImVec2 max, const ButtonPalette& pal) {
    if (pal.fill.w > 0.0f) {
        draw->AddRectFilled(min, max, U32(pal.fill), T::RadiusControl);
    }
    if (pal.border.w > 0.0f) {
        draw->AddRect(min, max, U32(pal.border), T::RadiusControl, 0, T::BorderThin);
    }
}

// Small filled triangle pointing down (open, or a dropdown) or right (closed).
void drawChevron(ImDrawList* draw, ImVec2 centre, float size, bool pointsDown, ImU32 colour) {
    if (pointsDown) {
        draw->AddTriangleFilled(
            ImVec2(centre.x - size, centre.y - size * 0.5f),
            ImVec2(centre.x + size, centre.y - size * 0.5f),
            ImVec2(centre.x,        centre.y + size * 0.75f),
            colour);
    } else {
        draw->AddTriangleFilled(
            ImVec2(centre.x - size * 0.5f,  centre.y - size),
            ImVec2(centre.x - size * 0.5f,  centre.y + size),
            ImVec2(centre.x + size * 0.75f, centre.y),
            colour);
    }
}

// The visible part of a label, following ImGui's convention that everything
// from "##" onwards is the id, not text. "Icon Size##strip" draws "Icon Size";
// "##raid_icon_size" draws nothing. The slice is not null-terminated at the
// split, so callers pass `end` to AddText and CalcTextSize.
struct LabelText {
    const char* begin;
    const char* end;      // nullptr means "to the null terminator"
    bool visible;
};

LabelText visibleLabel(const char* label) {
    if (!label || !*label) return {"", nullptr, false};
    const char* hash = std::strstr(label, "##");
    if (!hash) return {label, nullptr, true};
    return {label, hash, hash != label};
}

}  // namespace

bool Button(const char* label, ButtonVariant variant, ButtonSize sizePreset, ImVec2 size) {
    const ImVec2 labelSize = ImGui::CalcTextSize(label, nullptr, true);
    const float padX = sizePresetPadX(sizePreset);
    const float minH = sizePresetHeight(sizePreset);
    const float w = size.x > 0 ? size.x : labelSize.x + padX * 2.0f;
    const float h = size.y > 0 ? size.y : std::max(minH, labelSize.y + 8.0f);

    const ImVec2 cursorScreen = ImGui::GetCursorScreenPos();
    const ImVec2 rectMin = cursorScreen;
    const ImVec2 rectMax = ImVec2(cursorScreen.x + w, cursorScreen.y + h);

    ImGui::InvisibleButton(label, ImVec2(w, h));

    const bool hovered = ImGui::IsItemHovered();
    const bool held    = ImGui::IsItemActive();
    const bool focused = ImGui::IsItemFocused();
    const bool clicked = ImGui::IsItemClicked();

    const ButtonPalette pal = buttonPalette(variant, hovered, held);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    drawButtonFrame(draw, rectMin, rectMax, pal);

    // Stop at "##" like stock ImGui buttons, matching the measurement above.
    const ImVec2 textPos = ImVec2(
        rectMin.x + (w - labelSize.x) * 0.5f,
        rectMin.y + (h - labelSize.y) * 0.5f);
    draw->AddText(textPos, U32(pal.text), label, std::strstr(label, "##"));

    if (focused) {
        drawFocusOutline(draw, rectMin, rectMax, T::RadiusControl);
    }

    return clicked;
}

bool IconButton(const char* id, const char* glyph, ButtonVariant variant) {
    const float side = T::ControlHeight;
    const ImVec2 cursorScreen = ImGui::GetCursorScreenPos();
    const ImVec2 rectMin = cursorScreen;
    const ImVec2 rectMax = ImVec2(cursorScreen.x + side, cursorScreen.y + side);

    ImGui::InvisibleButton(id, ImVec2(side, side));

    const bool hovered = ImGui::IsItemHovered();
    const bool held    = ImGui::IsItemActive();
    const bool focused = ImGui::IsItemFocused();
    const bool clicked = ImGui::IsItemClicked();

    const ButtonPalette pal = buttonPalette(variant, hovered, held);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    drawButtonFrame(draw, rectMin, rectMax, pal);

    const ImVec2 glyphSize = ImGui::CalcTextSize(glyph);
    const ImVec2 textPos = ImVec2(
        rectMin.x + (side - glyphSize.x) * 0.5f,
        rectMin.y + (side - glyphSize.y) * 0.5f);
    draw->AddText(textPos, U32(pal.text), glyph);

    if (focused) {
        drawFocusOutline(draw, rectMin, rectMax, T::RadiusControl);
    }

    return clicked;
}

bool Checkbox(const char* label, bool* v) {
    if (!v) return false;

    const float box = T::CheckboxBox;
    const float labelPad = T::SpaceS;
    // An id-only label ("##enable") draws nothing, so the row is just the box
    // with no padding that would still take clicks.
    const LabelText lt = visibleLabel(label);
    const bool hasText = lt.visible;
    const ImVec2 labelSize = hasText ? ImGui::CalcTextSize(lt.begin, lt.end)
                                     : ImVec2(0, 0);
    const float rowHeight = std::max(box, labelSize.y);
    const float rowWidth = box + (hasText ? labelPad + labelSize.x : 0.0f);

    const ImVec2 rowMin = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton(label, ImVec2(rowWidth, rowHeight));
    const bool hovered = ImGui::IsItemHovered();
    const bool clicked = ImGui::IsItemClicked();
    const bool focused = ImGui::IsItemFocused();

    bool changed = false;
    if (clicked) {
        *v = !*v;
        changed = true;
    }

    ImDrawList* draw = ImGui::GetWindowDrawList();

    const ImVec2 boxMin = ImVec2(rowMin.x, rowMin.y + (rowHeight - box) * 0.5f);
    const ImVec2 boxMax = ImVec2(boxMin.x + box, boxMin.y + box);
    const float r = 4.0f;

    ImVec4 boxFill = *v ? T::Accent : (hovered ? T::Surface3 : T::Surface2);
    draw->AddRectFilled(boxMin, boxMax, U32(boxFill), r);
    if (!*v) {
        draw->AddRect(boxMin, boxMax, U32(T::BorderStrong), r, 0, T::BorderThin);
    }

    if (*v) {
        const float x0 = boxMin.x + 3.5f, y0 = boxMin.y + 8.0f;
        const float x1 = boxMin.x + 7.0f, y1 = boxMin.y + 11.5f;
        const float x2 = boxMin.x + 12.5f, y2 = boxMin.y + 4.5f;
        const ImVec2 pts[3] = { ImVec2(x0, y0), ImVec2(x1, y1), ImVec2(x2, y2) };
        draw->AddPolyline(pts, 3, U32(T::Surface0), 0, 2.0f);
    }

    if (hasText) {
        const ImVec2 textPos = ImVec2(
            rowMin.x + box + labelPad,
            rowMin.y + (rowHeight - labelSize.y) * 0.5f);
        draw->AddText(textPos, U32(T::TextPrimary), lt.begin, lt.end);
    }

    if (focused) {
        const ImVec2 rowMax = ImVec2(rowMin.x + rowWidth + 2.0f, rowMin.y + rowHeight);
        drawFocusOutline(draw, ImVec2(rowMin.x - 2.0f, rowMin.y - 1.0f), rowMax, r);
    }

    return changed;
}

// Pill-shaped switch.
bool Toggle(const char* label, bool* v) {
    if (!v) return false;

    const float trackW = 32.0f;
    const float trackH = 18.0f;
    const float knob   = 14.0f;
    const float labelPad = T::SpaceS;
    const ImVec2 labelSize = ImGui::CalcTextSize(label);
    const float rowHeight = std::max(trackH, labelSize.y);
    const float rowWidth = trackW + labelPad + labelSize.x;

    const ImVec2 rowMin = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton(label, ImVec2(rowWidth, rowHeight));
    const bool hovered = ImGui::IsItemHovered();
    const bool clicked = ImGui::IsItemClicked();
    const bool focused = ImGui::IsItemFocused();

    bool changed = false;
    if (clicked) {
        *v = !*v;
        changed = true;
    }

    ImDrawList* draw = ImGui::GetWindowDrawList();

    const ImVec2 trackMin = ImVec2(rowMin.x, rowMin.y + (rowHeight - trackH) * 0.5f);
    const ImVec2 trackMax = ImVec2(trackMin.x + trackW, trackMin.y + trackH);
    const float trackR = trackH * 0.5f;

    ImVec4 trackColor;
    if (*v) trackColor = hovered ? T::AccentHover : T::Accent;
    else    trackColor = hovered ? T::Surface3     : T::Surface2;
    draw->AddRectFilled(trackMin, trackMax, U32(trackColor), trackR);

    const float knobPad = (trackH - knob) * 0.5f;
    const float knobX = *v
        ? trackMax.x - knobPad - knob
        : trackMin.x + knobPad;
    const ImVec2 knobCenter = ImVec2(knobX + knob * 0.5f,
                                      trackMin.y + trackH * 0.5f);
    const ImU32 knobColor = *v ? U32(T::Surface0) : U32(T::TextSecondary);
    draw->AddCircleFilled(knobCenter, knob * 0.5f, knobColor);

    const ImVec2 textPos = ImVec2(
        rowMin.x + trackW + labelPad,
        rowMin.y + (rowHeight - labelSize.y) * 0.5f);
    draw->AddText(textPos, U32(T::TextPrimary), label);

    if (focused) {
        const ImVec2 outMin = ImVec2(rowMin.x - 2.0f, rowMin.y - 1.0f);
        const ImVec2 outMax = ImVec2(rowMin.x + rowWidth + 2.0f, rowMin.y + rowHeight + 1.0f);
        drawFocusOutline(draw, outMin, outMax, trackR);
    }

    return changed;
}

namespace {

// Slider track and grab shared by the float and int sliders. Updates
// *value_normalized (0..1) from drag input and returns true when it changed.
bool sliderCore(const char* id,
                float* value_normalized,
                ImVec2 size,
                bool* out_focused) {
    if (!value_normalized) return false;

    const ImVec2 pos = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton(id, size);
    const bool hovered = ImGui::IsItemHovered();
    const bool held    = ImGui::IsItemActive();
    if (out_focused) *out_focused = ImGui::IsItemFocused();

    bool changed = false;
    if (held) {
        const float mx = ImGui::GetIO().MousePos.x;
        float t = (mx - pos.x) / size.x;
        t = std::clamp(t, 0.0f, 1.0f);
        if (t != *value_normalized) {
            *value_normalized = t;
            changed = true;
        }
    }

    ImDrawList* draw = ImGui::GetWindowDrawList();

    const float trackH = 4.0f;
    const float trackY = pos.y + (size.y - trackH) * 0.5f;
    const ImVec2 trackMin = ImVec2(pos.x, trackY);
    const ImVec2 trackMax = ImVec2(pos.x + size.x, trackY + trackH);
    draw->AddRectFilled(trackMin, trackMax, U32(T::Surface2), trackH * 0.5f);

    const float grabX = pos.x + size.x * (*value_normalized);
    draw->AddRectFilled(trackMin, ImVec2(grabX, trackMax.y),
                        U32(T::Accent), trackH * 0.5f);

    const float grabR = (hovered || held) ? 8.0f : 7.0f;
    const ImVec2 grabCenter = ImVec2(grabX, pos.y + size.y * 0.5f);
    draw->AddCircleFilled(grabCenter, grabR, U32(T::TextPrimary));
    draw->AddCircle(grabCenter, grabR, U32(T::BorderStrong), 0, 1.5f);

    return changed;
}

}  // namespace

bool SliderFloat(const char* label, float* v, float lo, float hi, const char* fmt) {
    if (!v) return false;

    // Label and value on the top row, slider below.
    ImGui::PushID(label);

    // %.*s rather than %s: the drawable part is a slice of `label`, not its
    // own string, so it has no terminator at the split.
    const LabelText lt = visibleLabel(label);
    const int labelLen = lt.visible
        ? static_cast<int>((lt.end ? lt.end : label + std::strlen(label)) - lt.begin)
        : 0;
    ImGui::TextColored(T::TextSecondary, "%.*s", labelLen, lt.begin);
    ImGui::SameLine();

    char valueBuf[64];
    std::snprintf(valueBuf, sizeof(valueBuf), fmt, *v);
    ImVec2 valueSize = ImGui::CalcTextSize(valueBuf);
    float avail = ImGui::GetContentRegionAvail().x;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, avail - valueSize.x));
    ImGui::TextUnformatted(valueBuf);

    const float sliderH = 20.0f;
    const float span = std::max(1e-6f, hi - lo);
    float normalized = std::clamp((*v - lo) / span, 0.0f, 1.0f);
    bool focused = false;
    bool changed = sliderCore("##slider",
        &normalized,
        ImVec2(ImGui::GetContentRegionAvail().x, sliderH),
        &focused);
    if (changed) {
        *v = lo + normalized * span;
    }

    if (focused) {
        ImDrawList* draw = ImGui::GetWindowDrawList();
        const ImVec2 pos = ImGui::GetItemRectMin();
        const ImVec2 posMax = ImGui::GetItemRectMax();
        drawFocusOutline(draw,
            ImVec2(pos.x - 2.0f, pos.y - 2.0f),
            ImVec2(posMax.x + 2.0f, posMax.y + 2.0f),
            4.0f);
    }

    ImGui::PopID();
    return changed;
}

bool SliderInt(const char* label, int* v, int lo, int hi, const char* fmt) {
    if (!v) return false;
    // Format the int with the caller's spec (e.g. "%d%%") up front and pass
    // the result to SliderFloat as its format, so it prints the int as-is.
    char overlay[32];
    std::snprintf(overlay, sizeof(overlay), fmt ? fmt : "%d", *v);
    float fv = static_cast<float>(*v);
    bool changed = SliderFloat(label, &fv,
                               static_cast<float>(lo),
                               static_cast<float>(hi), overlay);
    if (changed) {
        *v = static_cast<int>(fv + (fv >= 0.0f ? 0.5f : -0.5f));
    }
    return changed;
}

bool Combo(const char* label, int* current, const char* const items[], int count) {
    if (!current || !items || count <= 0) return false;

    ImGui::PushID(label);

    const int cur = std::clamp(*current, 0, count - 1);
    const char* currentText = items[cur] ? items[cur] : "";
    const ImVec2 labelSize = ImGui::CalcTextSize(currentText, nullptr, true);
    const float padX = T::ButtonPadX;
    const float h = T::ControlHeight;
    const float w = std::max(labelSize.x + padX * 2.0f + 20.0f, 120.0f);

    // Uses ImGui's popup system so keyboard navigation and click-outside to
    // close work; only the button face is custom drawn.
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const ImVec2 rectMax = ImVec2(pos.x + w, pos.y + h);

    ImGui::InvisibleButton("##combo", ImVec2(w, h));
    const bool hovered = ImGui::IsItemHovered();
    const bool held    = ImGui::IsItemActive();
    const bool focused = ImGui::IsItemFocused();
    const bool clicked = ImGui::IsItemClicked();

    if (clicked) {
        ImGui::OpenPopup("##combo_popup");
    }

    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec4 fill = held ? T::Surface3 : (hovered ? T::Surface3 : T::Surface2);
    draw->AddRectFilled(pos, rectMax, U32(fill), T::RadiusControl);
    draw->AddRect(pos, rectMax, U32(T::BorderSubtle), T::RadiusControl, 0, T::BorderThin);

    const ImVec2 textPos = ImVec2(pos.x + padX,
                                   pos.y + (h - labelSize.y) * 0.5f);
    draw->AddText(textPos, U32(T::TextPrimary), currentText);

    drawChevron(draw, ImVec2(rectMax.x - padX, pos.y + h * 0.5f), 4.0f, true,
                U32(T::TextSecondary));

    if (focused) {
        drawFocusOutline(draw, pos, rectMax, T::RadiusControl);
    }

    bool changed = false;
    ImGui::SetNextWindowSize(ImVec2(w, 0.0f));
    if (ImGui::BeginPopup("##combo_popup")) {
        for (int i = 0; i < count; ++i) {
            const bool isSelected = (i == cur);
            const char* item = items[i] ? items[i] : "";
            if (ImGui::Selectable(item, isSelected)) {
                *current = i;
                changed = (i != cur);
            }
            if (isSelected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndPopup();
    }

    // A visible label, if any, sits to the right.
    if (label && label[0] != '\0' && !(label[0] == '#' && label[1] == '#')) {
        ImGui::SameLine();
        ImGui::TextColored(T::TextSecondary, "%s", label);
    }

    ImGui::PopID();
    return changed;
}

// Flat segmented control.
bool TabBar(const char* id, int* current, const char* const items[], int count) {
    if (!current || !items || count <= 0) return false;

    ImGui::PushID(id);

    const float h = T::TabHeight;
    const float availWidth = ImGui::GetContentRegionAvail().x;
    const float segW = availWidth / static_cast<float>(count);

    const ImVec2 stripMin = ImGui::GetCursorScreenPos();

    // Reserve the strip with a Dummy rather than an InvisibleButton. A button
    // covering the whole strip would be registered first and take every click
    // from the per-segment buttons below.
    ImGui::Dummy(ImVec2(availWidth, h));
    ImGui::SetCursorScreenPos(stripMin);

    ImDrawList* draw = ImGui::GetWindowDrawList();
    bool changed = false;

    for (int i = 0; i < count; ++i) {
        ImGui::PushID(i);
        const ImVec2 segMin = ImVec2(stripMin.x + segW * i, stripMin.y);
        const ImVec2 segMax = ImVec2(segMin.x + segW, segMin.y + h);
        const bool isActive = (i == *current);

        ImGui::SetCursorScreenPos(segMin);
        ImGui::InvisibleButton("##seg", ImVec2(segW, h));
        const bool hovered = ImGui::IsItemHovered();
        const bool clicked = ImGui::IsItemClicked();

        if (clicked && !isActive) {
            *current = i;
            changed = true;
        }

        ImVec4 fill;
        if (isActive)      fill = T::Surface2;
        else if (hovered)  fill = T::Surface3;
        else               fill = T::Surface1;
        draw->AddRectFilled(segMin, segMax, U32(fill));

        if (isActive) {
            const float ulH = 2.0f;
            draw->AddRectFilled(
                ImVec2(segMin.x, segMax.y - ulH),
                segMax,
                U32(T::Accent));
        }

        const char* item = items[i] ? items[i] : "";
        const ImVec2 lsz = ImGui::CalcTextSize(item);
        const ImU32 textCol = isActive ? U32(T::TextPrimary) : U32(T::TextSecondary);
        const ImVec2 textPos = ImVec2(
            segMin.x + (segW - lsz.x) * 0.5f,
            segMin.y + (h - lsz.y) * 0.5f);
        draw->AddText(textPos, textCol, item);

        ImGui::PopID();
    }

    ImGui::SetCursorScreenPos(ImVec2(stripMin.x, stripMin.y + h));

    ImGui::PopID();
    return changed;
}

// Open state lives in ImGui's window state storage, keyed under the
// section's label, the same way the built-in CollapsingHeader stores it.
bool BeginSection(const char* label, bool defaultOpen) {
    ImGui::PushID(label);
    ImGuiStorage* storage = ImGui::GetStateStorage();
    ImGuiID stateId = ImGui::GetID("##section_open");
    bool open = storage->GetBool(stateId, defaultOpen);

    const float h = T::SectionHeaderHeight;
    const float availWidth = ImGui::GetContentRegionAvail().x;
    const ImVec2 rowMin = ImGui::GetCursorScreenPos();
    const ImVec2 rowMax = ImVec2(rowMin.x + availWidth, rowMin.y + h);

    ImGui::InvisibleButton("##hdr", ImVec2(availWidth, h));
    const bool hovered = ImGui::IsItemHovered();
    const bool clicked = ImGui::IsItemClicked();
    const bool focused = ImGui::IsItemFocused();

    if (clicked) {
        open = !open;
    }
    // Stored every frame, so EndSection sees this frame's state even for a
    // section that starts closed and has never been clicked
    storage->SetBool(stateId, open);

    ImDrawList* draw = ImGui::GetWindowDrawList();

    if (hovered) {
        ImVec4 fill = ImVec4(T::Surface2.x, T::Surface2.y, T::Surface2.z, 0.50f);
        draw->AddRectFilled(rowMin, rowMax, U32(fill));
    }

    const float chevPad = T::SpaceM;
    const ImVec2 chevCenter = ImVec2(rowMin.x + chevPad + 6.0f,
                                      rowMin.y + h * 0.5f);
    const float chevSize = 4.0f;
    drawChevron(draw, chevCenter, chevSize, open, U32(T::TextSecondary));

    if (theme::gFontMedium) ImGui::PushFont(theme::gFontMedium);
    const ImVec2 labelSize = ImGui::CalcTextSize(label);
    const ImVec2 textPos = ImVec2(
        rowMin.x + chevPad + 6.0f + chevSize + 8.0f,
        rowMin.y + (h - labelSize.y) * 0.5f);
    draw->AddText(textPos, U32(T::TextPrimary), label);
    if (theme::gFontMedium) ImGui::PopFont();

    draw->AddLine(
        ImVec2(rowMin.x, rowMax.y - 1.0f),
        ImVec2(rowMax.x, rowMax.y - 1.0f),
        U32(T::BorderSubtle),
        T::BorderThin);

    if (focused) {
        drawFocusOutline(draw, rowMin, rowMax, 0.0f);
    }

    if (open) {
        ImGui::Dummy(ImVec2(0.0f, T::SpaceXS));
        ImGui::Indent(chevPad);
    }

    return open;
}

void EndSection() {
    // BeginSection only indents when open, so read the same stored state
    // (still inside its PushID scope) to decide whether to unindent.
    ImGuiStorage* storage = ImGui::GetStateStorage();
    ImGuiID stateId = ImGui::GetID("##section_open");
    bool wasOpen = storage->GetBool(stateId, true);
    if (wasOpen) {
        ImGui::Unindent(T::SpaceM);
    }
    ImGui::Dummy(ImVec2(0.0f, T::SpaceS));
    ImGui::PopID();
}

void TextNumber(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    if (theme::gFontMono) ImGui::PushFont(theme::gFontMono);
    ImGui::TextV(fmt, args);
    if (theme::gFontMono) ImGui::PopFont();
    va_end(args);
}

void Spinner(const char* label, float radius) {
    const ImVec2 cursor = ImGui::GetCursorScreenPos();
    const float ringDiameter = radius * 2.0f;
    const float labelWidth = label && label[0]
        ? ImGui::CalcTextSize(label).x
        : 0.0f;
    const float gap = label && label[0] ? T::SpaceS : 0.0f;
    const float totalWidth = ringDiameter + gap + labelWidth;
    const float lineHeight = std::max(ringDiameter, ImGui::GetTextLineHeight());

    ImDrawList* draw = ImGui::GetWindowDrawList();

    // Driven by ImGui's clock, so it keeps turning while the parse thread
    // holds the ActorMap lock. A 3/4 arc over a faint full ring.
    const float t = static_cast<float>(ImGui::GetTime());
    constexpr int kSegments = 32;
    const ImVec2 center(cursor.x + radius, cursor.y + lineHeight * 0.5f);

    draw->AddCircle(center, radius,
                    U32(ImVec4(T::AccentSubtle.x, T::AccentSubtle.y,
                               T::AccentSubtle.z, 0.35f)),
                    kSegments, 2.0f);

    constexpr float kTurn = 6.28318f;
    const float phase = t * kTurn;  // One turn per second
    constexpr float kArcLen = 0.75f * kTurn;
    constexpr int kArcPoints = 24;
    draw->PathClear();
    for (int i = 0; i <= kArcPoints; ++i) {
        const float a = phase + (kArcLen * i / static_cast<float>(kArcPoints));
        draw->PathLineTo(ImVec2(center.x + std::cos(a) * radius,
                                center.y + std::sin(a) * radius));
    }
    draw->PathStroke(U32(T::Accent), 0, 2.0f);

    if (label && label[0]) {
        const ImVec2 textPos(cursor.x + ringDiameter + gap,
                             center.y - ImGui::GetTextLineHeight() * 0.5f);
        draw->AddText(textPos, U32(T::TextSecondary), label);
    }

    ImGui::Dummy(ImVec2(totalWidth, lineHeight));
}

bool EmptyState(const char* id,
                const char* glyph,
                const char* headline,
                const char* hint,
                const char* actionLabel) {
    ImGui::PushID(id);

    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImDrawList* draw = ImGui::GetWindowDrawList();

    ImFont* font = ImGui::GetFont();
    const float glyphFontSize = ImGui::GetFontSize() * 2.4f;
    const ImVec2 glyphDim = font->CalcTextSizeA(glyphFontSize, 3.402823e38f, 0.0f, glyph);
    const ImVec2 headDim = ImGui::CalcTextSize(headline);
    const ImVec2 hintDim = hint ? ImGui::CalcTextSize(hint) : ImVec2(0, 0);

    float stackHeight = glyphDim.y + T::SpaceM + headDim.y;
    if (hint) stackHeight += T::SpaceS + hintDim.y;
    if (actionLabel) stackHeight += T::SpaceL + T::ControlHeight;

    // Slightly above centre in tall panels, a small top pad in short ones.
    const float topPad = std::max(T::SpaceL, (avail.y - stackHeight) * 0.4f);
    const float centerX = origin.x + avail.x * 0.5f;
    float y = origin.y + topPad;

    draw->AddText(font, glyphFontSize,
                  ImVec2(centerX - glyphDim.x * 0.5f, y),
                  U32(T::TextMuted), glyph);
    y += glyphDim.y + T::SpaceM;

    draw->AddText(ImVec2(centerX - headDim.x * 0.5f, y),
                  U32(T::TextSecondary), headline);
    y += headDim.y;

    if (hint) {
        y += T::SpaceS;
        draw->AddText(ImVec2(centerX - hintDim.x * 0.5f, y),
                      U32(T::TextMuted), hint);
        y += hintDim.y;
    }

    bool clicked = false;
    if (actionLabel) {
        y += T::SpaceL;
        const float buttonWidth = ImGui::CalcTextSize(actionLabel).x + 2.0f * T::ButtonPadX;
        ImGui::SetCursorScreenPos(ImVec2(centerX - buttonWidth * 0.5f, y));
        clicked = Button(actionLabel, ButtonVariant::Secondary);
        y += T::ControlHeight;
    }

    ImGui::SetCursorScreenPos(origin);
    ImGui::Dummy(ImVec2(avail.x, (y - origin.y) + T::SpaceL));

    ImGui::PopID();
    return clicked;
}

}  // namespace awlui
