#include "UIAutoAnnotationWizard.h"
#include "AutoAnnotationTriggerManager.h"
#include "AnnotationManager.h"
#include "AutoAnnotationTypes.h"
#include "Core/LocalizationManager.h"
#include "Core/SpellNameDatabase.h"
#include "imgui.h"
#include <algorithm>

UIAutoAnnotationWizard::UIAutoAnnotationWizard() = default;
UIAutoAnnotationWizard::~UIAutoAnnotationWizard() = default;

void UIAutoAnnotationWizard::setDependencies(
    AutoAnnotationTriggerManager* triggerManager,
    AnnotationManager* annotationManager,
    const SpellCastDatabase* spellCasts,
    const CombatDatabase* combat,
    const AuraDatabase* auras,
    const EventDatabase* events,
    awow::ISpellIconRenderer* iconLoader,
    const std::vector<HostileCreatureSpells>* hostileSpells)
{
    triggerManager_ = triggerManager;
    annotationManager_ = annotationManager;
    spellCastDb_ = spellCasts;
    combatDb_ = combat;
    auraDb_ = auras;
    eventDb_ = events;
    iconLoader_ = iconLoader;
    hostileSpells_ = hostileSpells;
}

void UIAutoAnnotationWizard::open() {
    state_.reset();
    isOpen_ = true;
}

void UIAutoAnnotationWizard::reset() {
    state_.reset();
}

WizardResult UIAutoAnnotationWizard::render(int32_t currentTimestamp_ms) {
    (void)currentTimestamp_ms;

    if (!isOpen_) return WizardResult::Completed;

    float scale = ImGui::GetIO().FontGlobalScale;
    ImGuiViewport* viewport = ImGui::GetMainViewport();

    float modalWidth = std::min(650.0f * scale, viewport->Size.x * 0.9f);
    float modalHeight = std::min(700.0f * scale, viewport->Size.y * 0.9f);

    ImVec2 modalPos = ImVec2(
        viewport->Pos.x + (viewport->Size.x - modalWidth) * 0.5f,
        viewport->Pos.y + (viewport->Size.y - modalHeight) * 0.5f
    );

    ImGui::SetNextWindowPos(modalPos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(modalWidth, modalHeight), ImGuiCond_Always);

    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.12f, 0.12f, 0.16f, 0.98f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10.0f * scale);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(20.0f * scale, 20.0f * scale));

    WizardResult result = WizardResult::InProgress;

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar |
                             ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_NoCollapse;

    if (ImGui::Begin("##AnnotationWizard", nullptr, flags)) {
        ImGui::PushFont(ImGui::GetIO().Fonts->Fonts.Size > 1 ? ImGui::GetIO().Fonts->Fonts[1] : nullptr);
        ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.95f, 1.0f), L("wizard.title"));
        ImGui::PopFont();
        ImGui::Spacing();

        renderStepIndicator();
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // No scrollbar here; the step's own child windows scroll.
        float footerHeight = 70.0f * scale;
        ImGui::BeginChild("StepContent", ImVec2(0, -footerHeight), false, ImGuiWindowFlags_NoScrollbar);

        switch (state_.getCurrentStep()) {
            case WizardStep::TriggerSource:
                renderStep1_TriggerSource();
                break;
            case WizardStep::EventType:
                renderStep2_EventType();
                break;
            case WizardStep::Positioning:
                renderStep3_Positioning();
                break;
            case WizardStep::ShapeSize:
                renderStep4_ShapeSize();
                break;
            case WizardStep::VisualStyle:
                renderStep5_VisualStyle();
                break;
            case WizardStep::DurationAdvanced:
                renderStep6_DurationAdvanced();
                break;
            default:
                break;
        }

        ImGui::EndChild();

        renderValidationError();

        result = renderNavigationButtons();

        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            result = WizardResult::Cancelled;
        }
    }
    ImGui::End();

    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();

    if (result != WizardResult::InProgress) {
        isOpen_ = false;
    }

    return result;
}

void UIAutoAnnotationWizard::renderStepIndicator() {
    const float scale = ImGui::GetIO().FontGlobalScale;
    int currentStep = static_cast<int>(state_.getCurrentStep()) + 1;

    const char* stepNames[] = {
        L("wizard.step.source"),
        L("wizard.step.event"),
        L("wizard.step.position"),
        L("wizard.step.shape"),
        L("wizard.step.visual"),
        L("wizard.step.advanced")
    };

    float width = ImGui::GetContentRegionAvail().x;
    float stepWidth = width / 6.0f;
    float circleRadius = 14.0f * scale;

    ImVec2 startPos = ImGui::GetCursorScreenPos();
    ImDrawList* drawList = ImGui::GetWindowDrawList();

    for (int i = 0; i < 6; ++i) {
        float centerX = startPos.x + stepWidth * (i + 0.5f);
        float centerY = startPos.y + circleRadius + 2.0f * scale;
        ImVec2 circleCenter = ImVec2(centerX, centerY);

        ImU32 circleColor;
        ImU32 textColor = IM_COL32(255, 255, 255, 255);
        if (i + 1 < currentStep) {
            circleColor = IM_COL32(50, 180, 50, 255);   // Completed: green
        } else if (i + 1 == currentStep) {
            circleColor = IM_COL32(100, 150, 255, 255); // Current: blue
        } else {
            circleColor = IM_COL32(80, 80, 100, 255);   // Future: gray
            textColor = IM_COL32(180, 180, 180, 255);
        }

        drawList->AddCircleFilled(circleCenter, circleRadius, circleColor);

        char num[3];
        snprintf(num, sizeof(num), "%d", i + 1);
        ImVec2 numSize = ImGui::CalcTextSize(num);
        drawList->AddText(
            ImVec2(circleCenter.x - numSize.x * 0.5f, circleCenter.y - numSize.y * 0.5f),
            textColor, num
        );

        if (i < 5) {
            ImU32 lineColor = (i + 1 < currentStep)
                ? IM_COL32(50, 180, 50, 255)
                : IM_COL32(80, 80, 100, 255);

            float lineStartX = circleCenter.x + circleRadius + 2.0f * scale;
            float lineEndX = startPos.x + stepWidth * (i + 1.5f) - circleRadius - 2.0f * scale;

            drawList->AddLine(
                ImVec2(lineStartX, circleCenter.y),
                ImVec2(lineEndX, circleCenter.y),
                lineColor, 2.0f * scale
            );
        }

        ImVec2 labelSize = ImGui::CalcTextSize(stepNames[i]);
        drawList->AddText(
            ImVec2(circleCenter.x - labelSize.x * 0.5f, circleCenter.y + circleRadius + 4.0f * scale),
            textColor, stepNames[i]
        );
    }

    // Reserve space for the indicator
    ImGui::Dummy(ImVec2(0, circleRadius * 2 + 20.0f * scale));
}

WizardResult UIAutoAnnotationWizard::renderNavigationButtons() {
    const float scale = ImGui::GetIO().FontGlobalScale;
    float buttonWidth = 100.0f * scale;
    float buttonHeight = 35.0f * scale;

    WizardResult result = WizardResult::InProgress;

    ImGui::Separator();
    ImGui::Spacing();

    if (ImGui::Button(L("wizard.cancel"), ImVec2(buttonWidth, buttonHeight))) {
        result = WizardResult::Cancelled;
    }

    float contentWidth = ImGui::GetContentRegionAvail().x;
    ImGui::SameLine(contentWidth - buttonWidth * 2 - 10.0f * scale);

    bool canGoBack = state_.canGoBack();
    if (!canGoBack) {
        ImGui::BeginDisabled();
    }
    if (ImGui::Button(L("wizard.back"), ImVec2(buttonWidth, buttonHeight))) {
        state_.goBack();
    }
    if (!canGoBack) {
        ImGui::EndDisabled();
    }

    ImGui::SameLine();

    bool isFinalStep = (state_.getCurrentStep() == WizardStep::DurationAdvanced);
    bool canAdvance = state_.canAdvance();

    const char* buttonLabel = isFinalStep ? L("wizard.finish") : L("wizard.next");

    if (!canAdvance) {
        ImGui::BeginDisabled();
    }

    if (isFinalStep) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.5f, 0.2f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.6f, 0.25f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.15f, 0.45f, 0.15f, 1.0f));
    }

    if (ImGui::Button(buttonLabel, ImVec2(buttonWidth, buttonHeight))) {
        if (isFinalStep) {
            result = WizardResult::Completed;
        } else {
            state_.advance();
        }
    }

    if (isFinalStep) {
        ImGui::PopStyleColor(3);
    }

    if (!canAdvance) {
        ImGui::EndDisabled();
    }

    return result;
}

void UIAutoAnnotationWizard::renderValidationError() {
    std::string error = state_.validateCurrentStep();
    if (!error.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.4f, 0.4f, 1.0f));
        ImGui::TextWrapped("%s", error.c_str());
        ImGui::PopStyleColor();
    }
}

std::string UIAutoAnnotationWizard::getSpellName(uint32_t spellId) const {
    return SPELL_NAME(spellId);
}
