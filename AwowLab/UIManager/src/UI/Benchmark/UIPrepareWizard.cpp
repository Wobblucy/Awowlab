#include "UI/Benchmark/UIPrepareWizard.h"

#include "Core/Benchmark/SpellClassification.h"
#include "Core/LocalizationManager.h"
#include "Core/MobWeightSettings.h"
#include "Core/PhaseSettings.h"
#include "UI/AwlUI/Theme.h"
#include "UI/AwlUI/Widgets.h"
#include "UI/Panels/Settings/UIMobWeightPanel.h"
#include "UI/Panels/Settings/UIPhaseEditorPanel.h"

#include <imgui.h>

#include <memory>

namespace benchmark_ui {

using namespace benchmark;

UIPrepareWizard::UIPrepareWizard()
    : phasePanel_(std::make_unique<UIPhaseEditorPanel>())
    , weightPanel_(std::make_unique<UIMobWeightPanel>()) {}

UIPrepareWizard::~UIPrepareWizard() = default;

void UIPrepareWizard::open() {
    open_ = true;
    justOpened_ = true;
    step_ = Step::Spells;
}

void UIPrepareWizard::close() {
    open_ = false;
}

void UIPrepareWizard::renderStepHeader(Step current) {
    // Three labels with the current one highlighted.
    static const char* const kSteps[] = {
        "prepare.step.spells",
        "prepare.step.phases",
        "prepare.step.weights",
    };

    for (int i = 0; i < 3; ++i) {
        if (i > 0) {
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Text, theme::TextMuted);
            ImGui::TextUnformatted(">");
            ImGui::PopStyleColor();
            ImGui::SameLine();
        }

        const bool isCurrent = (static_cast<int>(current) == i);
        const bool isDone = (static_cast<int>(current) > i);
        ImGui::PushStyleColor(ImGuiCol_Text,
                              isCurrent ? theme::Accent
                              : isDone ? theme::Success
                                       : theme::TextMuted);
        if (isCurrent) ImGui::PushFont(theme::gFontMedium);
        ImGui::TextUnformatted(L(kSteps[i]));
        if (isCurrent) ImGui::PopFont();
        ImGui::PopStyleColor();
    }

    // No separator here: the caller puts the navigation on this same row and
    // draws the rule underneath both.
}

void UIPrepareWizard::renderSpellStep(const PrepareWizardContext& context) {
    const auto* spec = ClassificationStore::instance().find(context.specId);
    const size_t pending = spec
        ? spec->pendingSpellIds.size() + spec->pendingAuraIds.size() : 0;
    const size_t ruled = spec ? spec->spells.size() + spec->auras.size() : 0;

    ImGui::TextUnformatted(LF("prepare.spells_state",
                              static_cast<int>(ruled),
                              static_cast<int>(pending)).c_str());
    ImGui::Dummy(ImVec2(0, theme::SpaceS));

    // The review runs its own loop, so it is launched rather than embedded.
    // Rulings already made for this spec carry over.
    if (awlui::Button(L(pending > 0 ? "prepare.spells_review"
                                    : "prepare.spells_change"),
                      pending > 0 ? awlui::ButtonVariant::Primary
                                  : awlui::ButtonVariant::Secondary)) {
        reviewRequested_ = true;
    }

    if (pending > 0) {
        ImGui::Dummy(ImVec2(0, theme::SpaceS));
        ImGui::PushStyleColor(ImGuiCol_Text, theme::Warn);
        ImGui::TextWrapped("%s", L("prepare.spells_blocked"));
        ImGui::PopStyleColor();
    }
}

void UIPrepareWizard::renderPhaseStep(const PrepareWizardContext& context) {
    const auto* rules = PhaseSettings::instance().rulesFor(context.encounterId);
    const size_t count = rules ? rules->size() : 0;

    ImGui::TextUnformatted(LF("prepare.phases_state",
                              static_cast<int>(count)).c_str());
    ImGui::Dummy(ImVec2(0, theme::SpaceS));

    // Optional: many fights have no phase worth splitting.
    phasePanel_->setVisible(true);
    phasePanel_->render(context.phaseData);
}

void UIPrepareWizard::renderWeightStep(const PrepareWizardContext& context) {
    auto& weights = MobWeightSettings::instance();

    ImGui::TextUnformatted(LF("prepare.weights_state",
                              static_cast<int>(weights.weights.size())).c_str());
    ImGui::Dummy(ImVec2(0, theme::SpaceS));

    weightPanel_->setVisible(true);

    // Prepare runs before any database is built, so the creature list is
    // gathered straight from the reference log's events.
    weightPanel_->renderFromEvents(context.events);
}

PrepareResult UIPrepareWizard::render(const PrepareWizardContext& context) {
    if (!open_) return PrepareResult::Running;

    if (justOpened_) {
        ImGui::SetNextWindowFocus();
        justOpened_ = false;
    }

    // Fills the window, like the report card.
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);

    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoMove |
                                   ImGuiWindowFlags_NoResize |
                                   ImGuiWindowFlags_NoCollapse |
                                   ImGuiWindowFlags_NoSavedSettings;

    PrepareResult result = PrepareResult::Running;

    if (ImGui::Begin(L("prepare.title"), nullptr, flags)) {
        ImGui::TextUnformatted(LF("prepare.subtitle",
                                  context.playerName.c_str(),
                                  context.encounterName.c_str()).c_str());
        ImGui::Dummy(ImVec2(0, theme::SpaceM));

        renderStepHeader(step_);

        // Navigation sits on the step header row rather than at the bottom of
        // the full-screen window.
        {
            // Right-aligned: the three buttons and gaps are measured so the
            // row stays put as labels change between steps.
            const float buttonWidth = 92.0f;
            const float spacing = ImGui::GetStyle().ItemSpacing.x;
            const int buttonCount = (step_ == Step::Spells) ? 2 : 3;
            const float navWidth = buttonWidth * buttonCount +
                                   spacing * (buttonCount - 1);

            ImGui::SameLine(ImGui::GetContentRegionAvail().x - navWidth);

            if (step_ != Step::Spells) {
                if (awlui::Button(L("prepare.back"), awlui::ButtonVariant::Secondary,
                                  awlui::ButtonSize::Sm, ImVec2(buttonWidth, 0))) {
                    step_ = static_cast<Step>(static_cast<int>(step_) - 1);
                }
                ImGui::SameLine();
            }

            if (step_ == Step::Weights) {
                // Only the last step commits; a reference built before the
                // spells are ruled could not be used.
                const auto* spec = ClassificationStore::instance().find(context.specId);
                const bool ready = spec && spec->fullyConfirmed();

                if (!ready) ImGui::BeginDisabled();
                if (awlui::Button(L("prepare.finish"), awlui::ButtonVariant::Primary,
                                  awlui::ButtonSize::Sm, ImVec2(buttonWidth, 0))) {
                    result = PrepareResult::Confirmed;
                    open_ = false;
                }
                if (!ready) ImGui::EndDisabled();
            } else {
                if (awlui::Button(L("prepare.next"), awlui::ButtonVariant::Primary,
                                  awlui::ButtonSize::Sm, ImVec2(buttonWidth, 0))) {
                    step_ = static_cast<Step>(static_cast<int>(step_) + 1);
                }
            }

            ImGui::SameLine();
            if (awlui::Button(L("btn.cancel"), awlui::ButtonVariant::Ghost,
                              awlui::ButtonSize::Sm, ImVec2(buttonWidth, 0))) {
                result = PrepareResult::Cancelled;
                open_ = false;
            }
        }

        ImGui::Dummy(ImVec2(0, theme::SpaceS));
        ImGui::Separator();
        ImGui::Dummy(ImVec2(0, theme::SpaceS));

        ImGui::BeginChild("##prepbody", ImVec2(0, 0));
        switch (step_) {
            case Step::Spells:  renderSpellStep(context); break;
            case Step::Phases:  renderPhaseStep(context); break;
            case Step::Weights: renderWeightStep(context); break;
        }
        ImGui::EndChild();
    }
    ImGui::End();

    return result;
}

} // namespace benchmark_ui
