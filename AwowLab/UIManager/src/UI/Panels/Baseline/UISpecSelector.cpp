#include "UI/Panels/Baseline/UISpecSelector.h"

#include "Core/HeroTalents.h"
#include "Core/LocalizationManager.h"
#include "Parser/SpecializationColors.h"
#include "UI/AwlUI/Widgets.h"

#include <imgui.h>

#include <algorithm>
#include <set>

namespace baseline_ui {
namespace {

// Class names in the order the game lists them, built from the spec table so
// this cannot drift away from what getClassName reports.
std::vector<std::string> allClassNames() {
    std::vector<std::string> names;
    std::set<std::string> seen;

    size_t count = 0;
    const uint16_t* ids = parser::allSpecIds(count);
    for (size_t i = 0; i < count; ++i) {
        const char* cls = parser::getClassName(ids[i]);
        if (cls && *cls && seen.insert(cls).second) names.emplace_back(cls);
    }
    std::sort(names.begin(), names.end());
    return names;
}

// The playable specs of one class. The tables carry an extra "initial" spec per
// class - a character that has not chosen yet - which is named after the class
// itself and has nothing to rank, so it is left out.
std::vector<uint16_t> specsOfClass(const std::string& className) {
    std::vector<uint16_t> specs;

    size_t count = 0;
    const uint16_t* ids = parser::allSpecIds(count);
    for (size_t i = 0; i < count; ++i) {
        const char* cls = parser::getClassName(ids[i]);
        if (!cls || className != cls) continue;

        const char* spec = parser::getSpecName(ids[i]);
        if (!spec || !*spec) continue;
        if (className == spec) continue;   // the unspecced placeholder

        specs.push_back(ids[i]);
    }
    return specs;
}

} // namespace

void UISpecSelector::open() {
    visible_ = true;
    selection_ = SpecSelection{};
    pendingClass_.clear();
    pendingSpec_ = 0;
    pendingHero_.clear();
    pendingContent_ = BaselineContent::Dungeon;
    heroTrees_.clear();
}

bool UISpecSelector::render() {
    if (!visible_) return true;

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(620.0f, 0.0f), ImGuiCond_Always);

    bool finished = false;

    if (ImGui::Begin(L("baseline.pick_title"), nullptr,
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_AlwaysAutoResize)) {

        ImGui::TextWrapped("%s", L("baseline.pick_desc"));
        ImGui::Separator();

        renderClassGrid();

        if (!pendingClass_.empty()) {
            ImGui::Separator();
            renderSpecRow();
        }

        if (pendingSpec_ != 0) {
            ImGui::Separator();
            renderHeroRow();
            ImGui::Separator();
            renderContentRow();
        }

        ImGui::Separator();

        // Confirming needs a spec; the hero tree may be left as every tree, and
        // content type always has a value.
        const bool ready = pendingSpec_ != 0;
        if (!ready) ImGui::BeginDisabled();
        if (awlui::Button(L("btn.baseline_start"), awlui::ButtonVariant::Primary)) {
            selection_.selected = true;
            selection_.specId = pendingSpec_;
            selection_.className = pendingClass_;
            selection_.apiClassName = parser::wclClassName(pendingSpec_);
            selection_.specName = parser::getSpecName(pendingSpec_);
            selection_.heroTree = pendingHero_;
            selection_.content = pendingContent_;
            visible_ = false;
            finished = true;
        }
        if (!ready) ImGui::EndDisabled();

        ImGui::SameLine();
        if (awlui::Button(L("btn.cancel"), awlui::ButtonVariant::Ghost)) {
            selection_ = SpecSelection{};
            visible_ = false;
            finished = true;
        }
    }
    ImGui::End();

    return finished;
}

void UISpecSelector::renderClassGrid() {
    ImGui::TextUnformatted(L("baseline.class"));

    static const std::vector<std::string> classes = allClassNames();

    // Four per row keeps thirteen classes to four short rows at this width
    int column = 0;
    for (const auto& name : classes) {
        if (column++ % 4 != 0) ImGui::SameLine();

        const bool active = (pendingClass_ == name);
        if (awlui::Button(name.c_str(),
                          active ? awlui::ButtonVariant::Primary
                                 : awlui::ButtonVariant::Secondary,
                          awlui::ButtonSize::Sm, ImVec2(140.0f, 0.0f))) {
            if (!active) {
                // Changing class invalidates everything downstream
                pendingClass_ = name;
                pendingSpec_ = 0;
                pendingHero_.clear();
                heroTrees_.clear();
            }
        }
    }
}

void UISpecSelector::renderSpecRow() {
    ImGui::TextUnformatted(L("baseline.spec"));

    const auto specs = specsOfClass(pendingClass_);
    int column = 0;
    for (uint16_t specId : specs) {
        if (column++ % 4 != 0) ImGui::SameLine();

        const char* name = parser::getSpecName(specId);
        const bool active = (pendingSpec_ == specId);
        if (awlui::Button(name,
                          active ? awlui::ButtonVariant::Primary
                                 : awlui::ButtonVariant::Secondary,
                          awlui::ButtonSize::Sm, ImVec2(140.0f, 0.0f))) {
            if (!active) {
                pendingSpec_ = specId;
                pendingHero_.clear();
                heroTrees_ = awow::HeroTalents::instance().treesForSpec(specId);
            }
        }
    }
}

void UISpecSelector::renderHeroRow() {
    ImGui::TextUnformatted(L("baseline.hero"));

    if (heroTrees_.empty()) {
        // Every playable spec has two, so an empty list means the shipped data
        // is missing or older than the spec - not that the spec has none.
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.65f, 0.65f, 0.68f, 1.0f));
        ImGui::TextWrapped("%s", L("baseline.hero_unknown"));
        ImGui::PopStyleColor();
        return;
    }

    // All hero trees is the default until the user picks one.
    const bool anyActive = pendingHero_.empty();
    if (awlui::Button(L("baseline.hero_any"),
                      anyActive ? awlui::ButtonVariant::Primary
                                : awlui::ButtonVariant::Secondary,
                      awlui::ButtonSize::Sm, ImVec2(140.0f, 0.0f))) {
        pendingHero_.clear();
    }

    for (const auto& tree : heroTrees_) {
        ImGui::SameLine();
        const bool active = (pendingHero_ == tree);
        if (awlui::Button(tree.c_str(),
                          active ? awlui::ButtonVariant::Primary
                                 : awlui::ButtonVariant::Secondary,
                          awlui::ButtonSize::Sm, ImVec2(140.0f, 0.0f))) {
            pendingHero_ = tree;
        }
    }
}

void UISpecSelector::renderContentRow() {
    ImGui::TextUnformatted(L("baseline.content"));

    // Dummy sits beside raid and keystones rather than under them. Its
    // configurations are separate encounters with their own rankings, so it is
    // a third source of samples, not a filter on the other two.
    struct Choice { BaselineContent value; const char* key; };
    static const Choice choices[] = {
        { BaselineContent::Dungeon, "btn.baseline_dungeon" },
        { BaselineContent::Raid,    "btn.baseline_raid"    },
        { BaselineContent::Dummy,   "btn.baseline_dummy"   },
    };

    bool first = true;
    for (const auto& choice : choices) {
        if (!first) ImGui::SameLine();
        first = false;
        const bool active = pendingContent_ == choice.value;
        if (awlui::Button(L(choice.key),
                          active ? awlui::ButtonVariant::Primary
                                 : awlui::ButtonVariant::Secondary,
                          awlui::ButtonSize::Sm, ImVec2(140.0f, 0.0f))) {
            pendingContent_ = choice.value;
        }
    }
}

} // namespace baseline_ui
