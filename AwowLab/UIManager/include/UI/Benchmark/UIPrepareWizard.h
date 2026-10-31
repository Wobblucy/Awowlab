#pragma once

#include <cstdint>
#include <string>
#include <memory>
#include <unordered_map>

#include "UI/Panels/Settings/UIPhaseEditorPanel.h"

// Wizard that walks through the three settings a reference profile is built
// with: spell rulings, phase rules and mob weights. "Change settings" reopens
// it against the same reference log.
//
// The settings are global, not per reference, so the overlay, the meters and
// every reference agree on the phases and weights for a boss. This wizard
// edits those shared settings.

class UIMobWeightPanel;
struct ExtractedData;

namespace benchmark_ui {

struct PrepareWizardContext {
    uint16_t specId = 0;
    std::string specName;
    std::string playerName;
    std::string encounterName;
    uint32_t encounterId = 0;

    // The reference log's events, which the weight step reads to list the
    // mobs in this pull. No database exists yet at this point.
    const ExtractedData* events = nullptr;

    std::unordered_map<uint32_t, std::string> spellNames;

    // Built by the caller and passed to the phase editor, which only offers
    // the trigger types whose lists are populated. Left empty, only fixed-time
    // phases are available.
    PhaseEditorData phaseData;
};

// What the user chose to do with the wizard.
enum class PrepareResult : uint8_t {
    Running = 0,   // Still open
    Confirmed,     // Cut the reference with these settings
    Cancelled,     // Leave everything as it was
};

class UIPrepareWizard {
public:
    UIPrepareWizard();
    ~UIPrepareWizard();

    void open();
    void close();
    bool isOpen() const { return open_; }

    PrepareResult render(const PrepareWizardContext& context);

private:
    enum class Step : uint8_t {
        Spells = 0,
        Phases,
        Weights,
    };

    void renderStepHeader(Step current);
    void renderSpellStep(const PrepareWizardContext& context);
    void renderPhaseStep(const PrepareWizardContext& context);
    void renderWeightStep(const PrepareWizardContext& context);

public:
    // True once after the user asked to edit spell rulings. The review runs
    // its own render loop, so the caller opens it between frames.
    bool consumeReviewRequest() {
        const bool requested = reviewRequested_;
        reviewRequested_ = false;
        return requested;
    }

private:
    bool open_ = false;
    bool justOpened_ = false;
    bool reviewRequested_ = false;
    Step step_ = Step::Spells;

    // The existing editors, drawn inline rather than as floating windows.
    std::unique_ptr<UIPhaseEditorPanel> phasePanel_;
    std::unique_ptr<UIMobWeightPanel> weightPanel_;
};

} // namespace benchmark_ui
