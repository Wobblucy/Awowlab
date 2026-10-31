#include "PlayAlong/UIKeybindConfigModal.h"
#include "UI/SpellIconLoader.h"
#include "Database/SpellCastDatabase.h"
#include "Core/SpellNameDatabase.h"
#include "Core/LocalizationManager.h"
#include <imgui.h>
#include <algorithm>
#include <unordered_map>

namespace PlayAlong {

void UIKeybindConfigModal::open(uint16_t specId,
                                 const std::string& playerGuid,
                                 const SpellCastDatabase* spellCastDb) {
    specId_ = specId;
    isOpen_ = true;
    wasCancelled_ = false;
    isCapturing_ = false;
    capturingSpellId_ = 0;

    keybindSettings_.load(specId);

    gatherUniqueSpells(playerGuid, spellCastDb);
}

void UIKeybindConfigModal::gatherUniqueSpells(const std::string& playerGuid,
                                               const SpellCastDatabase* spellCastDb) {
    spells_.clear();

    if (!spellCastDb) {
        return;
    }

    const auto& allCasts = spellCastDb->getAllActorCasts();
    auto it = allCasts.find(playerGuid);
    if (it == allCasts.end()) {
        return;
    }

    std::unordered_map<uint32_t, int> spellCounts;
    for (const auto& cast : it->second) {
        // Skip melee attacks (spell IDs 0 and 1)
        if (cast.spell_id == 0 || cast.spell_id == 1) {
            continue;
        }
        spellCounts[cast.spell_id]++;
    }

    spells_.reserve(spellCounts.size());
    std::unordered_set<uint32_t> usedSpellIds;

    for (const auto& [spellId, count] : spellCounts) {
        SpellForKeybind spell;
        spell.spellId = spellId;
        spell.spellName = SPELL_NAME(spellId);
        spell.castCount = count;
        spells_.push_back(std::move(spell));
        usedSpellIds.insert(spellId);
    }

    // Store used spells so keybinds for spells not in this encounter are disabled
    keybindSettings_.setUsedSpells(usedSpellIds);

    std::sort(spells_.begin(), spells_.end(), [](const auto& a, const auto& b) {
        return a.castCount > b.castCount;
    });
}

bool UIKeybindConfigModal::render(SpellIconLoader* iconLoader) {
    if (!isOpen_) {
        return true;
    }

    if (isCapturing_) {
        captureKeybind();
    }

    float scale = ImGui::GetIO().FontGlobalScale;
    ImGuiViewport* viewport = ImGui::GetMainViewport();

    float modalWidth = std::min(1600.0f * scale, viewport->Size.x * 0.95f);
    float modalHeight = std::min(1400.0f * scale, viewport->Size.y * 0.95f);

    ImVec2 modalPos = ImVec2(
        viewport->Pos.x + (viewport->Size.x - modalWidth) * 0.5f,
        viewport->Pos.y + (viewport->Size.y - modalHeight) * 0.5f
    );

    ImGui::SetNextWindowPos(modalPos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(modalWidth, modalHeight), ImGuiCond_Always);

    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.12f, 0.12f, 0.16f, 0.98f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10.0f * scale);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(20.0f * scale, 20.0f * scale));

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar |
                             ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_NoCollapse |
                             ImGuiWindowFlags_NoScrollbar |
                             ImGuiWindowFlags_NoScrollWithMouse;

    bool done = false;

    if (ImGui::Begin("##KeybindConfigModal", nullptr, flags)) {
        ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[0]);  // Use default font, scaled
        float titleScale = 1.4f;
        ImGui::SetWindowFontScale(titleScale);
        ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.95f, 1.0f), L("playalong.configure_keybinds"));
        ImGui::SetWindowFontScale(1.0f);
        ImGui::PopFont();

        const std::string specName = PlayAlongKeybindSettings::specDisplayName(specId_);
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "(%s)",
                           specName.empty() ? L("playalong.unknown_spec") : specName.c_str());

        ImGui::Spacing();

        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.6f, 1.0f), L("playalong.keybind_help"));

        ImGui::Spacing();

        // Buttons above the list (Done + Clear All)
        float buttonHeight = 40.0f * scale;
        float buttonWidth = 140.0f * scale;
        float contentWidth = ImGui::GetContentRegionAvail().x;
        float totalButtonsWidth = buttonWidth * 2 + 20.0f * scale;  // Two buttons + spacing
        float buttonX = (contentWidth - totalButtonsWidth) * 0.5f;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + buttonX);

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.5f, 0.2f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.6f, 0.25f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.15f, 0.4f, 0.15f, 1.0f));

        if (ImGui::Button(L("btn.done"), ImVec2(buttonWidth, buttonHeight))) {
            keybindSettings_.save();
            isOpen_ = false;
            done = true;
        }

        ImGui::PopStyleColor(3);

        ImGui::SameLine();

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.5f, 0.2f, 0.2f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.6f, 0.25f, 0.25f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.4f, 0.15f, 0.15f, 1.0f));

        if (ImGui::Button(L("playalong.clear_all"), ImVec2(buttonWidth, buttonHeight))) {
            for (const auto& spell : spells_) {
                keybindSettings_.clearKeybind(spell.spellId);
            }
        }

        ImGui::PopStyleColor(3);

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        float listHeight = modalHeight - 220.0f * scale;  // Leave room for header and buttons
        listHeight = std::max(listHeight, 100.0f * scale);  // Minimum list height

        ImGui::BeginChild("##SpellList", ImVec2(0, listHeight), false);

        resetRowIndex();

        for (const auto& spell : spells_) {
            renderSpellRow(spell, iconLoader);
        }

        if (spells_.empty()) {
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.55f, 1.0f), L("playalong.no_spells_found"));
        }

        ImGui::EndChild();

        if (!isCapturing_ && ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            wasCancelled_ = true;
            isOpen_ = false;
            done = true;
        }
    }
    ImGui::End();

    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();

    if (isCapturing_) {
        renderCaptureOverlay();
    }

    return done;
}

void UIKeybindConfigModal::renderSpellRow(const SpellForKeybind& spell, SpellIconLoader* iconLoader) {
    float scale = ImGui::GetIO().FontGlobalScale;
    float iconSize = 40.0f * scale;
    float buttonWidth = 140.0f * scale;
    float clearButtonWidth = 55.0f * scale;
    float ignoreButtonWidth = 60.0f * scale;
    float rowPadding = 8.0f * scale;
    float rightPadding = 30.0f * scale;  // Extra padding on right to prevent cutoff

    ImGui::PushID(static_cast<int>(spell.spellId));

    bool isIgnored = keybindSettings_.isSpellIgnored(spell.spellId);

    ImTextureID iconTexture = iconLoader ? iconLoader->getSpellIcon(spell.spellId) : 0;

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImVec2 rowStart = ImGui::GetCursorScreenPos();
    rowStart.x -= rowPadding;
    rowStart.y -= 2.0f * scale;
    float rowWidth = ImGui::GetContentRegionAvail().x + rowPadding * 2;
    float rowHeight = iconSize + 4.0f * scale;

    bool isEvenRow = (rowIndex_ % 2 == 0);
    rowIndex_++;

    ImU32 rowColor = isEvenRow ? IM_COL32(35, 35, 45, 180) : IM_COL32(45, 45, 55, 180);
    if (isIgnored) {
        rowColor = isEvenRow ? IM_COL32(30, 30, 35, 120) : IM_COL32(35, 35, 40, 120);
    }
    drawList->AddRectFilled(rowStart, ImVec2(rowStart.x + rowWidth, rowStart.y + rowHeight), rowColor, 4.0f);

    ImGui::BeginGroup();

    if (iconTexture != 0) {
        if (isIgnored) {
            ImGui::Image(iconTexture, ImVec2(iconSize, iconSize), ImVec2(0, 0), ImVec2(1, 1),
                        ImVec4(0.4f, 0.4f, 0.4f, 0.6f));
        } else {
            ImGui::Image(iconTexture, ImVec2(iconSize, iconSize));
        }
        SpellIconLoader::checkWowheadLookup(spell.spellId);
    } else {
        ImVec2 pos = ImGui::GetCursorScreenPos();
        ImU32 placeholderColor = isIgnored ? IM_COL32(40, 40, 50, 120) : IM_COL32(60, 60, 80, 200);
        drawList->AddRectFilled(pos, ImVec2(pos.x + iconSize, pos.y + iconSize),
                               placeholderColor, 4.0f);
        ImGui::Dummy(ImVec2(iconSize, iconSize));
    }

    ImGui::SameLine();

    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (iconSize - ImGui::GetTextLineHeight()) * 0.5f);

    // Truncate long names to leave room for the keybind, Clear and Ignore buttons
    float buttonsWidth = buttonWidth + clearButtonWidth + ignoreButtonWidth + 30.0f * scale + rightPadding;
    float availableWidth = ImGui::GetContentRegionAvail().x - buttonsWidth;
    std::string displayName = spell.spellName;
    if (ImGui::CalcTextSize(displayName.c_str()).x > availableWidth) {
        while (ImGui::CalcTextSize((displayName + "...").c_str()).x > availableWidth && displayName.size() > 3) {
            displayName.pop_back();
        }
        displayName += "...";
    }

    if (isIgnored) {
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.55f, 0.7f), "%s", displayName.c_str());
    } else {
        ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.95f, 1.0f), "%s", displayName.c_str());
    }

    ImGui::SameLine(ImGui::GetContentRegionAvail().x - buttonsWidth + rightPadding);

    float buttonHeight = 30.0f * scale;

    ImGui::SetCursorPosY(ImGui::GetCursorPosY() - (iconSize - ImGui::GetTextLineHeight()) * 0.5f +
                          (iconSize - buttonHeight) * 0.5f);

    if (isIgnored) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.2f, 0.25f, 0.6f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.2f, 0.2f, 0.25f, 0.6f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.2f, 0.2f, 0.25f, 0.6f));
        ImGui::Button(L("playalong.ignored"), ImVec2(buttonWidth, buttonHeight));
        ImGui::PopStyleColor(3);

        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.4f, 0.2f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.5f, 0.25f, 1.0f));

        if (ImGui::Button(L("playalong.show_spell"), ImVec2(clearButtonWidth, buttonHeight))) {
            keybindSettings_.unignoreSpell(spell.spellId);
        }

        ImGui::PopStyleColor(2);
    } else {
        const Keybind& kb = keybindSettings_.getKeybind(spell.spellId);
        std::string buttonLabel;

        if (isCapturing_ && capturingSpellId_ == spell.spellId) {
            buttonLabel = "...";
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.4f, 0.4f, 0.1f, 1.0f));
        } else if (kb.isUnbound()) {
            buttonLabel = L("playalong.click_to_set");
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.25f, 0.25f, 0.3f, 1.0f));
        } else {
            buttonLabel = kb.toString();
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.35f, 0.5f, 1.0f));
        }

        if (ImGui::Button(buttonLabel.c_str(), ImVec2(buttonWidth, buttonHeight))) {
            if (!isCapturing_) {
                isCapturing_ = true;
                capturingSpellId_ = spell.spellId;
            }
        }
        ImGui::PopStyleColor();

        if (!kb.isUnbound() && !isCapturing_) {
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.5f, 0.2f, 0.2f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.6f, 0.25f, 0.25f, 1.0f));

            if (ImGui::Button(L("playalong.clear_keybind"), ImVec2(clearButtonWidth, buttonHeight))) {
                keybindSettings_.clearKeybind(spell.spellId);
            }

            ImGui::PopStyleColor(2);
        }

        if (!isCapturing_) {
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.3f, 0.35f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.4f, 0.4f, 0.45f, 1.0f));

            if (ImGui::Button(L("playalong.ignore_spell"), ImVec2(ignoreButtonWidth, buttonHeight))) {
                keybindSettings_.ignoreSpell(spell.spellId);
            }

            ImGui::PopStyleColor(2);
        }
    }

    ImGui::EndGroup();

    ImGui::PopID();

    ImGui::Spacing();
    ImGui::Spacing();
}

void UIKeybindConfigModal::captureKeybind() {
    ImGuiIO& io = ImGui::GetIO();

    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        isCapturing_ = false;
        capturingSpellId_ = 0;
        return;
    }

    // Keys that may be bound; anything else is ignored during capture
    static const ImGuiKey bindableKeys[] = {
        // Letters
        ImGuiKey_A, ImGuiKey_B, ImGuiKey_C, ImGuiKey_D, ImGuiKey_E, ImGuiKey_F,
        ImGuiKey_G, ImGuiKey_H, ImGuiKey_I, ImGuiKey_J, ImGuiKey_K, ImGuiKey_L,
        ImGuiKey_M, ImGuiKey_N, ImGuiKey_O, ImGuiKey_P, ImGuiKey_Q, ImGuiKey_R,
        ImGuiKey_S, ImGuiKey_T, ImGuiKey_U, ImGuiKey_V, ImGuiKey_W, ImGuiKey_X,
        ImGuiKey_Y, ImGuiKey_Z,
        // Numbers
        ImGuiKey_0, ImGuiKey_1, ImGuiKey_2, ImGuiKey_3, ImGuiKey_4,
        ImGuiKey_5, ImGuiKey_6, ImGuiKey_7, ImGuiKey_8, ImGuiKey_9,
        // Function keys
        ImGuiKey_F1, ImGuiKey_F2, ImGuiKey_F3, ImGuiKey_F4, ImGuiKey_F5, ImGuiKey_F6,
        ImGuiKey_F7, ImGuiKey_F8, ImGuiKey_F9, ImGuiKey_F10, ImGuiKey_F11, ImGuiKey_F12,
        // Special keys
        ImGuiKey_Space, ImGuiKey_Tab, ImGuiKey_Enter, ImGuiKey_Backspace,
        ImGuiKey_Delete, ImGuiKey_Insert, ImGuiKey_Home, ImGuiKey_End,
        ImGuiKey_PageUp, ImGuiKey_PageDown,
        // Arrow keys
        ImGuiKey_UpArrow, ImGuiKey_DownArrow, ImGuiKey_LeftArrow, ImGuiKey_RightArrow,
        // Punctuation
        ImGuiKey_Minus, ImGuiKey_Equal, ImGuiKey_LeftBracket, ImGuiKey_RightBracket,
        ImGuiKey_Backslash, ImGuiKey_Semicolon, ImGuiKey_Apostrophe,
        ImGuiKey_Comma, ImGuiKey_Period, ImGuiKey_Slash, ImGuiKey_GraveAccent,
        // Numpad
        ImGuiKey_Keypad0, ImGuiKey_Keypad1, ImGuiKey_Keypad2, ImGuiKey_Keypad3,
        ImGuiKey_Keypad4, ImGuiKey_Keypad5, ImGuiKey_Keypad6, ImGuiKey_Keypad7,
        ImGuiKey_Keypad8, ImGuiKey_Keypad9, ImGuiKey_KeypadDecimal,
        ImGuiKey_KeypadDivide, ImGuiKey_KeypadMultiply, ImGuiKey_KeypadSubtract,
        ImGuiKey_KeypadAdd, ImGuiKey_KeypadEnter
    };

    for (ImGuiKey key : bindableKeys) {
        if (ImGui::IsKeyPressed(key)) {
            Keybind kb;
            kb.imguiKey = static_cast<int>(key);
            kb.ctrl = io.KeyCtrl;
            kb.shift = io.KeyShift;
            kb.alt = io.KeyAlt;

            keybindSettings_.setKeybind(capturingSpellId_, kb);
            isCapturing_ = false;
            capturingSpellId_ = 0;
            return;
        }
    }
}

void UIKeybindConfigModal::renderCaptureOverlay() {
    float scale = ImGui::GetIO().FontGlobalScale;
    ImGuiViewport* viewport = ImGui::GetMainViewport();

    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize(viewport->Size);

    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 0.6f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));

    ImGuiWindowFlags overlayFlags = ImGuiWindowFlags_NoTitleBar |
                                     ImGuiWindowFlags_NoResize |
                                     ImGuiWindowFlags_NoMove |
                                     ImGuiWindowFlags_NoScrollbar |
                                     ImGuiWindowFlags_NoInputs;

    if (ImGui::Begin("##CaptureOverlay", nullptr, overlayFlags)) {
        float boxWidth = 300.0f * scale;
        float boxHeight = 100.0f * scale;
        ImVec2 boxPos = ImVec2(
            viewport->Pos.x + (viewport->Size.x - boxWidth) * 0.5f,
            viewport->Pos.y + (viewport->Size.y - boxHeight) * 0.5f
        );

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        drawList->AddRectFilled(boxPos,
                               ImVec2(boxPos.x + boxWidth, boxPos.y + boxHeight),
                               IM_COL32(30, 30, 40, 240), 10.0f);
        drawList->AddRect(boxPos,
                         ImVec2(boxPos.x + boxWidth, boxPos.y + boxHeight),
                         IM_COL32(100, 100, 150, 200), 10.0f, 0, 2.0f);

        const char* line1 = L("playalong.press_key");
        const char* line2 = L("playalong.hold_modifiers");
        const char* line3 = L("playalong.esc_cancel");

        ImVec2 text1Size = ImGui::CalcTextSize(line1);
        ImVec2 text2Size = ImGui::CalcTextSize(line2);
        ImVec2 text3Size = ImGui::CalcTextSize(line3);

        float textY = boxPos.y + 15.0f * scale;

        drawList->AddText(ImVec2(boxPos.x + (boxWidth - text1Size.x) * 0.5f, textY),
                         IM_COL32(255, 255, 255, 255), line1);
        textY += text1Size.y + 8.0f * scale;

        drawList->AddText(ImVec2(boxPos.x + (boxWidth - text2Size.x) * 0.5f, textY),
                         IM_COL32(180, 180, 200, 255), line2);
        textY += text2Size.y + 8.0f * scale;

        drawList->AddText(ImVec2(boxPos.x + (boxWidth - text3Size.x) * 0.5f, textY),
                         IM_COL32(150, 150, 170, 255), line3);
    }
    ImGui::End();

    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
}

} // namespace PlayAlong
