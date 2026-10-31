#include "UI/Benchmark/UIMentorPicker.h"

#include "Core/LocalizationManager.h"
#include "UI/AwlUI/Theme.h"
#include "UI/AwlUI/Widgets.h"

#include <imgui.h>

#include <algorithm>
#include <cstdio>
#include <ctime>

namespace benchmark_ui {

namespace {

using namespace awlui;

// Named differently from the report card's helper so the two do not collide
// in a unity build.
std::string formatPickerDuration(int32_t ms) {
    if (ms < 0) ms = 0;
    const int32_t seconds = ms / 1000;
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%d:%02d", seconds / 60, seconds % 60);
    return buffer;
}

// Absolute epoch ms to a short local date, matching the cache browser.
std::string formatPickerDate(int64_t startTimeMs) {
    if (startTimeMs <= 0) return "-";

    const std::time_t seconds = static_cast<std::time_t>(startTimeMs / 1000);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &seconds);
#else
    localtime_r(&seconds, &tm);
#endif

    char buffer[32];
    std::strftime(buffer, sizeof(buffer), "%b %d", &tm);
    return buffer;
}

// A full-width selectable row with a title and a muted second line.
//
// allowOverlap lets a button drawn over the row still receive clicks; without
// it the row takes the hover first.
bool listRow(const char* id, const std::string& title, const std::string& detail,
             bool selected, bool allowOverlap = false) {
    ImGui::PushID(id);

    const float height = detail.empty() ? 30.0f : 46.0f;
    const bool clicked = ImGui::Selectable("##row", selected,
                                           allowOverlap
                                               ? ImGuiSelectableFlags_AllowItemOverlap
                                               : ImGuiSelectableFlags_None,
                                           ImVec2(0, height));

    const ImVec2 rowMin = ImGui::GetItemRectMin();
    ImDrawList* draw = ImGui::GetWindowDrawList();

    draw->AddText(ImVec2(rowMin.x + theme::SpaceS, rowMin.y + 6.0f),
                  ImGui::ColorConvertFloat4ToU32(theme::TextPrimary),
                  title.c_str());

    if (!detail.empty()) {
        draw->AddText(ImVec2(rowMin.x + theme::SpaceS, rowMin.y + 25.0f),
                      ImGui::ColorConvertFloat4ToU32(theme::TextMuted),
                      detail.c_str());
    }

    ImGui::PopID();
    return clicked;
}

} // namespace

void UIMentorPicker::open(Mode mode) {
    open_ = true;
    popupOpened_ = false;
    mode_ = mode;
    step_ = Step::Encounter;
    selectedEncounterId_ = 0;
    selectedEncounterName_.clear();
    selectedMentor_ = {};
    selectedMentorLabel_.clear();
    result_ = {};
}

void UIMentorPicker::close() {
    open_ = false;
    popupOpened_ = false;
}

void UIMentorPicker::renderEncounterStep(const MentorPickerContext& context) {
    ImGui::TextUnformatted(L("mentorpicker.step_encounter"));
    ImGui::Dummy(ImVec2(0, theme::SpaceS));

    if (context.encounters.empty()) {
        // No references yet; say what to do next instead of an empty list.
        awlui::EmptyState("##noencounters", "?",
                          L("mentorpicker.none_prepared"),
                          L("mentorpicker.none_prepared_hint"));
        return;
    }

    ImGui::BeginChild("##encounters", ImVec2(0, 300.0f));
    auto& session = benchmark::BenchmarkSession::instance();

    int index = 0;
    for (const auto& [encounterId, name] : context.encounters) {
        const auto mentors = session.mentorsForEncounter(encounterId);

        std::string detail;
        for (const auto& mentor : mentors) {
            if (!detail.empty()) detail += ", ";
            detail += mentor.specName.empty() ? mentor.playerName
                                              : mentor.specName;
        }

        char id[32];
        std::snprintf(id, sizeof(id), "enc%d", index++);
        if (listRow(id, name, detail, encounterId == selectedEncounterId_)) {
            selectedEncounterId_ = encounterId;
            selectedEncounterName_ = name;
            step_ = Step::Mentor;
        }
    }
    ImGui::EndChild();
}

void UIMentorPicker::renderMentorStep(const MentorPickerContext& context) {
    (void)context;

    ImGui::TextUnformatted(LF("mentorpicker.step_mentor",
                              selectedEncounterName_.c_str()).c_str());
    ImGui::Dummy(ImVec2(0, theme::SpaceS));

    auto& session = benchmark::BenchmarkSession::instance();
    const auto mentors = session.mentorsForEncounter(selectedEncounterId_);

    ImGui::BeginChild("##mentors", ImVec2(0, 300.0f));
    int index = 0;
    for (const auto& mentor : mentors) {
        const std::string title =
            mentor.playerName.empty()
                ? mentor.specName
                : mentor.playerName + "  -  " + mentor.specName;

        const std::string detail =
            LF("mentorpicker.mentor_detail",
               formatPickerDuration(mentor.pullDurationMs).c_str(),
               mentor.sourceLabel.c_str());

        char id[32];
        std::snprintf(id, sizeof(id), "mentor%d", index++);
        const bool isSelected = (mentor.key == selectedMentor_);

        // Overlap allowed: the delete button below sits on this row.
        if (listRow(id, title, detail, isSelected, true)) {
            selectedMentor_ = mentor.key;
            selectedMentorLabel_ = title;

            if (mode_ == Mode::ReferenceOnly) {
                result_.confirmed = true;
                result_.mentor = mentor.key;
            } else {
                step_ = Step::Fight;
            }
        }

        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 30.0f);
        ImGui::PushID(id);
        if (awlui::IconButton("##delete", "x", awlui::ButtonVariant::Ghost)) {
            pendingDelete_ = mentor.key;
            confirmDelete_ = true;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", L("mentorpicker.delete_hint"));
        }
        ImGui::PopID();
    }
    ImGui::EndChild();

    if (confirmDelete_) {
        ImGui::OpenPopup("##confirmdelete");
        confirmDelete_ = false;
    }

    if (ImGui::BeginPopupModal("##confirmdelete", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        // Ask before deleting, since a reference takes a download and a
        // classification pass to rebuild.
        ImGui::TextUnformatted(L("mentorpicker.delete_confirm"));
        ImGui::Dummy(ImVec2(0, theme::SpaceS));

        if (awlui::Button(L("mentorpicker.delete_yes"),
                          awlui::ButtonVariant::Danger)) {
            benchmark::BenchmarkSession::instance().forgetMentor(pendingDelete_);
            if (selectedMentor_ == pendingDelete_) {
                selectedMentor_ = {};
                selectedMentorLabel_.clear();
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (awlui::Button(L("btn.cancel"), awlui::ButtonVariant::Secondary)) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    ImGui::Dummy(ImVec2(0, theme::SpaceS));
    if (awlui::Button(L("btn.back"), awlui::ButtonVariant::Ghost,
                      awlui::ButtonSize::Sm)) {
        step_ = Step::Encounter;
    }
}

void UIMentorPicker::renderFightStep(const MentorPickerContext& context) {
    ImGui::TextUnformatted(LF("mentorpicker.step_fight",
                              selectedMentorLabel_.c_str()).c_str());
    ImGui::Dummy(ImVec2(0, theme::SpaceS));

    // Only pulls of the same boss; the comparison would refuse anything else,
    // but only after the log had loaded.
    std::vector<const PickerFight*> matching;
    for (const auto& fight : context.fights) {
        if (fight.encounterId == selectedEncounterId_) {
            matching.push_back(&fight);
        }
    }

    if (matching.empty()) {
        awlui::EmptyState("##nofights", "?",
                          L("mentorpicker.no_fights"),
                          L("mentorpicker.no_fights_hint"));
    } else {
        ImGui::BeginChild("##fights", ImVec2(0, 300.0f));
        int index = 0;
        for (const auto* fight : matching) {
            // The same details the cache browser shows.
            std::string detail = formatPickerDate(fight->startTimeMs);
            if (!fight->difficulty.empty()) {
                detail += "  " + fight->difficulty;
            }
            detail += "  " + formatPickerDuration(fight->durationMs);
            if (!fight->groupLead.empty()) {
                detail += "  " + fight->groupLead;
            }
            if (!fight->cached) {
                detail += "  " + std::string(L("mentorpicker.needs_download"));
            }

            char id[32];
            std::snprintf(id, sizeof(id), "fight%d", index++);
            if (listRow(id, fight->label, detail, false)) {
                result_.confirmed = true;
                result_.mentor = selectedMentor_;
                result_.reportCode = fight->reportCode;
                result_.fightId = fight->fightId;
            }
        }
        ImGui::EndChild();
    }

    ImGui::Dummy(ImVec2(0, theme::SpaceS));
    if (awlui::Button(L("btn.back"), awlui::ButtonVariant::Ghost,
                      awlui::ButtonSize::Sm)) {
        step_ = Step::Mentor;
    }
}

bool UIMentorPicker::render(const MentorPickerContext& context) {
    if (!open_) return false;

    const char* title = L("mentorpicker.title");
    if (!popupOpened_) {
        ImGui::OpenPopup(title);
        popupOpened_ = true;
    }

    ImGui::SetNextWindowSize(ImVec2(560, 460), ImGuiCond_Appearing);

    bool finished = false;
    bool stayOpen = true;

    if (ImGui::BeginPopupModal(title, &stayOpen,
                               ImGuiWindowFlags_NoSavedSettings)) {
        switch (step_) {
            case Step::Encounter: renderEncounterStep(context); break;
            case Step::Mentor:    renderMentorStep(context); break;
            case Step::Fight:     renderFightStep(context); break;
        }

        ImGui::Separator();
        if (awlui::Button(L("btn.cancel"), awlui::ButtonVariant::Secondary,
                          awlui::ButtonSize::Sm)) {
            result_.cancelled = true;
            finished = true;
        }

        if (result_.confirmed || finished) {
            ImGui::CloseCurrentPopup();
            finished = true;
        }

        ImGui::EndPopup();
    }

    // The window's own close button counts as cancelling.
    if (!stayOpen) {
        result_.cancelled = true;
        finished = true;
    }

    if (finished) close();
    return finished;
}

} // namespace benchmark_ui
