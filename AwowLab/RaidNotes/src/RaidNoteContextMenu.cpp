#include "RaidNoteContextMenu.h"
#include "Core/LocalizationManager.h"
#include <algorithm>
#include <cstring>
#include <string>

namespace RaidNotes {

namespace {

// The title is translated; the part after ### keeps the popup id fixed
std::string modalTitle() {
    return std::string(L("raidnotes.ctx_add_title")) + "###AddToRaidNote";
}

} // namespace

bool RaidNoteContextMenu::s_modalOpen = false;
bool RaidNoteContextMenu::s_modalJustOpened = false;
SpellContext RaidNoteContextMenu::s_pendingContext;
int RaidNoteContextMenu::s_selectedOption = 0;
char RaidNoteContextMenu::s_customTextBuffer[256] = "";
std::vector<int32_t> RaidNoteContextMenu::s_allInstanceTimestamps;
bool RaidNoteContextMenu::s_instancesQueried = false;

bool RaidNoteContextMenu::checkAndShow(const SpellContext& context) {
    if (s_modalOpen || ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId)) {
        return false;
    }

    if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        openWithContext(context);
        return true;
    }
    return false;
}

bool RaidNoteContextMenu::checkAndShowRect(const SpellContext& context, ImVec2 min, ImVec2 max) {
    if (s_modalOpen || ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId)) {
        return false;
    }

    ImVec2 mousePos = ImGui::GetMousePos();

    if (mousePos.x >= min.x && mousePos.x <= max.x &&
        mousePos.y >= min.y && mousePos.y <= max.y &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {

        openWithContext(context);
        return true;
    }
    return false;
}

void RaidNoteContextMenu::renderModal(RaidNoteDocument* document,
                                       FindAllInstancesCallback findAllInstances) {
    if (!s_modalOpen) {
        return;
    }

    if (!s_instancesQueried) {
        if (!s_pendingContext.grouped_timestamps.empty()) {
            s_allInstanceTimestamps = s_pendingContext.grouped_timestamps;
        } else if (findAllInstances) {
            s_allInstanceTimestamps = findAllInstances(s_pendingContext.spell_id);
        }
        s_instancesQueried = true;
    }

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

    if (s_modalJustOpened) {
        ImGui::OpenPopup(modalTitle().c_str());
        s_modalJustOpened = false;
    }

    // Auto-resize so the layout holds at any UI scale
    ImGuiWindowFlags flags = ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove;

    if (ImGui::BeginPopupModal(modalTitle().c_str(), &s_modalOpen, flags)) {
        ImGui::Text(L("raidnotes.ctx_spell_fmt"), s_pendingContext.spell_name.c_str());
        if (s_pendingContext.spell_id > 0) {
            ImGui::SameLine();
            ImGui::TextDisabled("(ID: %u)", s_pendingContext.spell_id);
        }

        const bool hasTimestamp = s_pendingContext.timestamp_ms >= 0;
        const int32_t totalSeconds = s_pendingContext.timestamp_ms / 1000;
        const int32_t minutes = totalSeconds / 60;
        const int32_t seconds = totalSeconds % 60;

        if (hasTimestamp) {
            ImGui::Text(L("raidnotes.ctx_timestamp_fmt"), minutes, seconds);
        }

        ImGui::Separator();

        ImGui::TextUnformatted(L("raidnotes.ctx_add_to_note"));
        ImGui::Spacing();

        if (hasTimestamp) {
            char label[128];
            snprintf(label, sizeof(label), L("raidnotes.ctx_this_instance_fmt"), minutes, seconds);
            ImGui::RadioButton(label, &s_selectedOption, 0);
        } else {
            ImGui::BeginDisabled();
            ImGui::RadioButton(L("raidnotes.ctx_this_instance_none"), &s_selectedOption, 0);
            ImGui::EndDisabled();
            s_selectedOption = 1;
        }

        size_t instanceCount = s_allInstanceTimestamps.size();
        char allLabel[128];
        snprintf(allLabel, sizeof(allLabel), L("raidnotes.ctx_all_instances_fmt"), instanceCount);

        if (instanceCount > 0) {
            ImGui::RadioButton(allLabel, &s_selectedOption, 1);
        } else {
            ImGui::BeginDisabled();
            ImGui::RadioButton(L("raidnotes.ctx_all_instances_none"), &s_selectedOption, 1);
            ImGui::EndDisabled();
        }

        ImGui::Spacing();
        ImGui::Separator();

        ImGui::TextUnformatted(L("raidnotes.ctx_custom_text"));
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##CustomText", L("raidnotes.ctx_custom_text_hint"),
                                  s_customTextBuffer, sizeof(s_customTextBuffer));

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        float buttonWidth = 120.0f;
        float spacing = ImGui::GetStyle().ItemSpacing.x;
        float totalWidth = buttonWidth * 2 + spacing;
        float startX = (ImGui::GetContentRegionAvail().x - totalWidth) * 0.5f;

        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + startX);

        if (ImGui::Button(L("btn.cancel"), ImVec2(buttonWidth, 0))) {
            closeModal();
            ImGui::CloseCurrentPopup();
        }

        ImGui::SameLine();

        bool canAdd = (s_selectedOption == 0 && hasTimestamp) ||
                      (s_selectedOption == 1 && !s_allInstanceTimestamps.empty());

        if (!canAdd) {
            ImGui::BeginDisabled();
        }

        if (ImGui::Button(L("raidnotes.ctx_add_button"), ImVec2(buttonWidth, 0))) {
            if (document) {
                RaidNoteGroupType groupType = s_pendingContext.is_hostile_source
                    ? RaidNoteGroupType::BossCasts
                    : RaidNoteGroupType::PlayerCasts;

                auto addEntryAt = [&](int32_t timestamp_ms) {
                    RaidNoteEntry entry;
                    entry.spell_id = s_pendingContext.spell_id;
                    entry.spell_name = s_pendingContext.spell_name;
                    entry.custom_text = s_customTextBuffer;
                    entry.enabled = true;
                    entry.timestamp_ms = timestamp_ms;
                    document->addEntry(groupType, std::move(entry));
                };

                if (s_selectedOption == 0) {
                    addEntryAt(s_pendingContext.timestamp_ms);
                } else {
                    // One entry per timestamp
                    for (int32_t ts : s_allInstanceTimestamps) {
                        addEntryAt(ts);
                    }
                }
            }

            closeModal();
            ImGui::CloseCurrentPopup();
        }

        if (!canAdd) {
            ImGui::EndDisabled();
        }

        ImGui::EndPopup();
    }

    // Clicking outside closes the popup without going through closeModal
    if (!ImGui::IsPopupOpen(modalTitle().c_str())) {
        s_modalOpen = false;
    }
}

void RaidNoteContextMenu::closeModal() {
    s_modalOpen = false;
    s_modalJustOpened = false;
    s_pendingContext = SpellContext{};
    s_selectedOption = 0;
    s_customTextBuffer[0] = '\0';
    s_allInstanceTimestamps.clear();
    s_instancesQueried = false;
}

void RaidNoteContextMenu::openWithContext(const SpellContext& context) {
    s_pendingContext = context;
    s_modalOpen = true;
    s_modalJustOpened = true;
    s_selectedOption = 0;
    s_customTextBuffer[0] = '\0';
    s_allInstanceTimestamps.clear();
    s_instancesQueried = false;
}

} // namespace RaidNotes
