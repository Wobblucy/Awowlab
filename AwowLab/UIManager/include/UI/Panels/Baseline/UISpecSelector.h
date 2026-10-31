#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace baseline_ui {

// Which content a baseline is taken from. Dummy is its own choice rather than
// a variant of the others: a dummy has no mechanics and no forced movement, so
// it shows the priority unobstructed, where an encounter shows the same
// priority surviving a real pull. Averaging the two would describe neither.
enum class BaselineContent : uint8_t {
    Dungeon,
    Raid,
    Dummy,
};

// What the user picked to build a baseline for.
struct SpecSelection {
    bool selected = false;
    uint16_t specId = 0;
    std::string className;      // "Demon Hunter", for showing the user
    std::string apiClassName;   // "DemonHunter", which is what the API accepts
    std::string specName;       // "Arms"
    std::string heroTree;       // "Colossus"; empty means every hero tree
    BaselineContent content = BaselineContent::Dungeon;
};

// Picks the spec, hero talent and content a baseline is for.
//
// Spec comes first because it decides everything after it: rankings are queried
// per class and spec, and the hero tree list depends on the spec. Content type
// is last because it does not narrow anything else - it only says which
// encounters to pull.
//
// Hero talent changes rotation, buffs and cooldown usage, so a baseline that
// mixes two hero trees averages two different playstyles.
class UISpecSelector {
public:
    UISpecSelector() = default;

    void open();

    // Returns true once the user has confirmed or cancelled.
    bool render();

    const SpecSelection& getSelection() const { return selection_; }

private:
    void renderClassGrid();
    void renderSpecRow();
    void renderHeroRow();
    void renderContentRow();

    SpecSelection selection_;
    bool visible_ = false;

    // Nothing is chosen until a class is, and the rows below stay empty until
    // then - showing every spec in the game at once would be a wall.
    std::string pendingClass_;
    uint16_t pendingSpec_ = 0;
    std::string pendingHero_;
    BaselineContent pendingContent_ = BaselineContent::Dungeon;

    std::vector<std::string> heroTrees_;   // for pendingSpec_, refreshed on change
};

} // namespace baseline_ui
