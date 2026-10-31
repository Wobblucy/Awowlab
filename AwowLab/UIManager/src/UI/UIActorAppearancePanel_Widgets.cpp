// UIActorAppearancePanel_Widgets.cpp
// Reusable widget helpers for appearance settings

#include "UI/UIActorAppearancePanel.h"
#include "UI/AwlUI/Widgets.h"
#include "UI/ActorAppearanceSettings.h"
#include "Geometry/CustomShapeRegistry.h"
#include "Core/LocalizationManager.h"
#include "Parser/SpecializationColors.h"
#include <imgui.h>
#include <array>
#include <string>
#include <vector>

namespace {

// Built-in shapes in ActorShape order, with their descriptions
struct ShapeInfo {
    const char* nameKey;
    const char* tooltipKey;
};
constexpr ShapeInfo shapeInfos[] = {
    {"shape.sphere", "shape.tooltip.sphere"},
    {"shape.cube", "shape.tooltip.cube"},
    {"shape.shield", "shape.tooltip.shield"},
    {"shape.medic_cross", "shape.tooltip.medic_cross"},
    {"shape.sword", "shape.tooltip.sword"},
    {"shape.bow", "shape.tooltip.bow"},
    {"shape.cone", "shape.tooltip.cone"},
    {"shape.cylinder", "shape.tooltip.cylinder"},
    {"shape.diamond", "shape.tooltip.diamond"},
    {"shape.pyramid", "shape.tooltip.pyramid"},
    {"shape.star", "shape.tooltip.star"},
    {"shape.classicon", "shape.tooltip.classicon"},
};
constexpr size_t kBuiltInShapeCount = std::size(shapeInfos);

// Localised shape names for an override combo: "Default" (no override) at
// index 0, then each built-in shape at its ActorShape value + 1
std::array<const char*, kBuiltInShapeCount + 1> shapeNamesWithDefault() {
    std::array<const char*, kBuiltInShapeCount + 1> names{};
    names[0] = L("shape.default");
    for (size_t i = 0; i < kBuiltInShapeCount; ++i) {
        names[i + 1] = L(shapeInfos[i].nameKey);
    }
    return names;
}

// The checkbox in front of an optional colour. Ticking it starts the colour
// at white; unticking clears it. Returns true when toggled.
bool optionalColorToggle(std::optional<float>& r, std::optional<float>& g,
                         std::optional<float>& b) {
    bool enableColor = r.has_value() && g.has_value() && b.has_value();
    if (!awlui::Checkbox("##enable", &enableColor)) return false;

    if (enableColor && !r.has_value()) {
        r = 1.0f;
        g = 1.0f;
        b = 1.0f;
    } else if (!enableColor) {
        r.reset();
        g.reset();
        b.reset();
    }
    return true;
}

}  // namespace

bool UIActorAppearancePanel::renderShapeCombo(const char* label, ActorShape& shape) {
    int currentShape = static_cast<int>(shape);
    bool changed = false;

    const char* previewName = (currentShape >= 0 && currentShape < static_cast<int>(std::size(shapeInfos)))
        ? L(shapeInfos[currentShape].nameKey)
        : "???";

    if (ImGui::BeginCombo(label, previewName)) {
        for (size_t i = 0; i < std::size(shapeInfos); ++i) {
            bool isSelected = (currentShape == static_cast<int>(i));
            if (ImGui::Selectable(L(shapeInfos[i].nameKey), isSelected)) {
                shape = static_cast<ActorShape>(i);
                changed = true;
            }

            // Tooltip on hover
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", L(shapeInfos[i].tooltipKey));
            }

            if (isSelected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }

    return changed;
}

bool UIActorAppearancePanel::renderOptionalShapeCombo(const char* label, std::optional<ActorShape>& shape) {
    const auto shapeNames = shapeNamesWithDefault();
    int currentShape = shape.has_value() ? (static_cast<int>(shape.value()) + 1) : 0;

    if (awlui::Combo(label, &currentShape, shapeNames.data(), static_cast<int>(shapeNames.size()))) {
        if (currentShape == 0) {
            shape.reset();
        } else {
            shape = static_cast<ActorShape>(currentShape - 1);
        }
        return true;
    }
    return false;
}

bool UIActorAppearancePanel::renderColorEdit(const char* label, std::optional<float>& r,
                                              std::optional<float>& g, std::optional<float>& b) {
    ImGui::PushID(label);

    bool changed = optionalColorToggle(r, g, b);

    ImGui::SameLine();

    // Check has_value() again since checkbox may have changed it
    if (r.has_value() && g.has_value() && b.has_value()) {
        float color[3] = { r.value(), g.value(), b.value() };
        if (ImGui::ColorEdit3(label, color, ImGuiColorEditFlags_NoInputs)) {
            r = color[0];
            g = color[1];
            b = color[2];
            changed = true;
        }
    } else {
        ImGui::TextDisabled(L("appearance.default_label"), label);
    }

    ImGui::PopID();
    return changed;
}

// Enhanced color edit with presets bar
bool UIActorAppearancePanel::renderColorEditWithPresets(const char* label, std::optional<float>& r,
                                                          std::optional<float>& g, std::optional<float>& b,
                                                          ActorAppearanceSettings& settings) {
    ImGui::PushID(label);

    bool changed = optionalColorToggle(r, g, b);

    ImGui::SameLine();

    // Check has_value() again since checkbox may have changed it
    if (r.has_value() && g.has_value() && b.has_value()) {
        float color[3] = { r.value(), g.value(), b.value() };

        // Color button that opens picker + presets popup
        if (ImGui::ColorButton(label, ImVec4(color[0], color[1], color[2], 1.0f),
                               ImGuiColorEditFlags_NoTooltip)) {
            ImGui::OpenPopup("ColorPickerWithPresets");
        }
        ImGui::SameLine();
        ImGui::TextUnformatted(label);

        // Popup with presets + picker
        if (ImGui::BeginPopup("ColorPickerWithPresets")) {
            // Presets bar at top
            float outR, outG, outB;
            if (renderColorPresetsBar(settings, outR, outG, outB, color[0], color[1], color[2])) {
                r = outR;
                g = outG;
                b = outB;
                changed = true;
            }

            ImGui::Separator();

            // Standard color picker below
            if (ImGui::ColorPicker3("##picker", color,
                                    ImGuiColorEditFlags_NoSidePreview | ImGuiColorEditFlags_NoSmallPreview)) {
                r = color[0];
                g = color[1];
                b = color[2];
                changed = true;
            }

            ImGui::EndPopup();
        }
    } else {
        ImGui::TextDisabled(L("appearance.default_label"), label);
    }

    ImGui::PopID();
    return changed;
}

bool UIActorAppearancePanel::renderScaleSlider(const char* label, std::optional<float>& scale) {
    bool changed = false;

    ImGui::PushID(label);

    bool enableScale = scale.has_value();
    if (awlui::Checkbox("##enable", &enableScale)) {
        if (enableScale && !scale.has_value()) {
            scale = 1.0f;
        } else if (!enableScale) {
            scale.reset();
        }
        changed = true;
    }

    ImGui::SameLine();

    // Check has_value() again since checkbox may have changed it
    if (scale.has_value()) {
        float value = scale.value();
        if (awlui::SliderFloat(label, &value, 0.1f, 100.0f, "%.2fx")) {
            scale = value;
            changed = true;
        }
    } else {
        ImGui::TextDisabled(L("appearance.default_label"), label);
    }

    ImGui::PopID();
    return changed;
}

bool UIActorAppearancePanel::renderScaleSlider(const char* label, float& scale, float min, float max) {
    return awlui::SliderFloat(label, &scale, min, max, "%.2fx");
}

bool UIActorAppearancePanel::renderOptionalShapeComboWithCustom(const char* label,
                                                                  std::optional<ActorShape>& shape,
                                                                  std::optional<std::string>& customShapeName) {
    bool changed = false;

    // Built-in shape names (with "Default" option at index 0)
    const auto builtInNames = shapeNamesWithDefault();
    constexpr int builtInCount = static_cast<int>(kBuiltInShapeCount + 1);

    // Determine current selection index
    // 0 = Default (no override)
    // 1-12 = Built-in shapes (Sphere through ClassIcon)
    // 13+ = Custom models
    int currentIdx = 0;
    if (customShapeName.has_value() && !customShapeName->empty()) {
        for (size_t i = 0; i < cachedCustomShapeNames_.size(); ++i) {
            if (cachedCustomShapeNames_[i] == *customShapeName) {
                currentIdx = builtInCount + static_cast<int>(i);
                break;
            }
        }
        // If not found (e.g., model was deleted), fall back to showing name
        if (currentIdx == 0 && !customShapeName->empty()) {
            currentIdx = -1;  // Special: custom model no longer exists
        }
    } else if (shape.has_value()) {
        currentIdx = static_cast<int>(shape.value()) + 1;
    }

    std::string previewStr;
    if (currentIdx == -1) {
        // Missing custom model
        previewStr = *customShapeName + " (!)";
    } else if (currentIdx == 0) {
        previewStr = builtInNames[0];
    } else if (currentIdx < builtInCount) {
        previewStr = builtInNames[currentIdx];
    } else {
        int customIdx = currentIdx - builtInCount;
        if (customIdx < static_cast<int>(cachedCustomShapeNames_.size())) {
            previewStr = cachedCustomShapeNames_[customIdx];
        } else {
            previewStr = "???";
        }
    }

    // Refresh custom shapes when combo is opened
    if (ImGui::BeginCombo(label, previewStr.c_str())) {
        // Refresh on first open of this combo
        if (customShapesNeedRefresh_) {
            refreshCustomShapes();
        }

        // Section: Built-in shapes
        ImGui::TextDisabled("%s", L("shape.builtin_header"));
        ImGui::Separator();

        for (int i = 0; i < builtInCount; ++i) {
            bool isSelected = (currentIdx == i);
            if (ImGui::Selectable(builtInNames[i], isSelected)) {
                if (i == 0) {
                    // Default - clear both overrides
                    shape.reset();
                    customShapeName.reset();
                } else {
                    // Built-in shape
                    shape = static_cast<ActorShape>(i - 1);
                    customShapeName.reset();
                }
                changed = true;
            }
            if (isSelected) {
                ImGui::SetItemDefaultFocus();
            }
        }

        // Section: Custom models (if any)
        if (!cachedCustomShapeNames_.empty()) {
            ImGui::Separator();
            ImGui::TextDisabled("%s", L("shape.custom_header"));
            ImGui::Separator();

            for (size_t i = 0; i < cachedCustomShapeNames_.size(); ++i) {
                int itemIdx = builtInCount + static_cast<int>(i);
                bool isSelected = (currentIdx == itemIdx);

                const std::string& modelName = cachedCustomShapeNames_[i];

                std::string errorMsg;
                if (customRegistry_) {
                    const auto* customShape = customRegistry_->getShape(modelName);
                    if (customShape && !customShape->isValid) {
                        errorMsg = L("shape.custom_load_failed");
                    }
                }

                std::string displayName = modelName;
                if (!errorMsg.empty()) {
                    displayName += " (!)";
                }

                if (ImGui::Selectable(displayName.c_str(), isSelected)) {
                    customShapeName = modelName;
                    shape.reset();  // Clear built-in shape when using custom
                    changed = true;
                }

                // Tooltip for errors
                if (!errorMsg.empty() && ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("%s", errorMsg.c_str());
                }

                if (isSelected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
        } else if (customRegistry_) {
            // Registry exists but no models found
            ImGui::Separator();
            ImGui::TextDisabled("%s", L("shape.no_custom_models"));
        }

        ImGui::EndCombo();
    } else {
        // Combo was closed - mark for refresh on next open
        customShapesNeedRefresh_ = true;
    }

    return changed;
}

namespace {

// One level of the appearance hierarchy, as a row in the override trace
struct OverrideSource {
    const char* levelName;      // e.g., "Role", "Class", "GUID"
    std::string levelValue;     // e.g., "Tank", "Warrior", "...Illidan-0A1B2C3D"
    bool hasShape = false;
    bool hasColor = false;
    bool hasScale = false;
    ImVec4 color = ImVec4(0.7f, 0.7f, 0.7f, 1.0f);  // Display color
};

// Adds a row for an override level when that level sets anything
void addOverrideSource(std::vector<OverrideSource>& sources,
                       const AppearanceOverride* level,
                       const char* levelName,
                       std::string levelValue,
                       const ImVec4& color) {
    if (!level || !level->hasAnyOverride()) return;

    OverrideSource src;
    src.levelName = levelName;
    src.levelValue = std::move(levelValue);
    src.hasShape = level->shape.has_value() || level->customShapeName.has_value();
    src.hasColor = level->r.has_value();
    src.hasScale = level->scale.has_value();
    src.color = color;
    sources.push_back(std::move(src));
}

}  // namespace

void UIActorAppearancePanel::renderOverrideTrace(const ActorAppearanceSettings& settings,
                                                   const std::string& guid,
                                                   const std::string& actorName,
                                                   uint16_t specId) {
    // Collapsible section
    if (!ImGui::TreeNode(L("appearance.override_trace"))) {
        return;
    }

    ActorRole role = (specId > 0) ? ActorAppearanceSettings::getRoleFromSpecId(specId) : ActorRole::Unknown;
    std::string className = (specId > 0) ? ActorAppearanceSettings::getClassFromSpecId(specId) : "";

    // Build list of active override levels (from lowest to highest priority)
    std::vector<OverrideSource> sources;

    // 1. Role defaults (always present)
    {
        OverrideSource src;
        src.levelName = L("appearance.override_level_default");
        src.levelValue = ActorAppearanceSettings::getRoleName(role);
        src.hasShape = true;   // Defaults always provide shape
        src.hasColor = false;  // Color comes from system
        src.hasScale = true;   // Default scale is 1.0
        src.color = ImVec4(0.5f, 0.5f, 0.5f, 1.0f);
        sources.push_back(src);
    }

    // 2. Role override
    if (role != ActorRole::Unknown) {
        addOverrideSource(sources, settings.getRoleOverride(role),
                          L("appearance.override_level_role"),
                          ActorAppearanceSettings::getRoleName(role),
                          ImVec4(0.6f, 0.6f, 0.9f, 1.0f));  // Blue-ish
    }

    // 3. Class override
    if (!className.empty()) {
        addOverrideSource(sources, settings.getClassOverride(className),
                          L("appearance.override_level_class"), className,
                          ImVec4(0.9f, 0.6f, 0.9f, 1.0f));  // Purple-ish
    }

    // 4. Spec override
    if (specId > 0) {
        addOverrideSource(sources, settings.getSpecOverride(specId),
                          L("appearance.override_level_spec"), std::to_string(specId),
                          ImVec4(0.9f, 0.9f, 0.6f, 1.0f));  // Yellow-ish
    }

    // 5. Name group override
    if (!actorName.empty()) {
        addOverrideSource(sources, settings.getNameGroupOverride(actorName),
                          L("appearance.override_level_name"), actorName,
                          ImVec4(0.6f, 0.9f, 0.9f, 1.0f));  // Cyan-ish
    }

    // 6. GUID override (highest priority), shortened to its tail for display
    {
        std::string guidDisplay = guid.length() > 20
            ? "..." + guid.substr(guid.length() - 16)
            : guid;
        addOverrideSource(sources, settings.getGuidOverride(guid),
                          L("appearance.override_level_guid"), std::move(guidDisplay),
                          ImVec4(0.4f, 1.0f, 0.4f, 1.0f));  // Green (same as override indicator)
    }

    ImGui::BeginGroup();

    // Header row
    ImGui::TextDisabled("%-12s %-15s %s  %s  %s",
                        L("appearance.trace_level"),
                        L("appearance.trace_value"),
                        L("appearance.trace_shape"),
                        L("appearance.trace_color"),
                        L("appearance.trace_scale"));

    ImGui::Separator();

    // Data rows
    for (size_t i = 0; i < sources.size(); ++i) {
        const auto& src = sources[i];

        // Level name (colored)
        ImGui::TextColored(src.color, "%-12s", src.levelName);

        ImGui::SameLine(100);
        ImGui::Text("%-15s", src.levelValue.c_str());

        // Shape indicator
        ImGui::SameLine(220);
        if (src.hasShape) {
            ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "[S]");
        } else {
            ImGui::TextDisabled("[ ]");
        }

        // Color indicator
        ImGui::SameLine(250);
        if (src.hasColor) {
            ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "[C]");
        } else {
            ImGui::TextDisabled("[ ]");
        }

        // Scale indicator
        ImGui::SameLine(280);
        if (src.hasScale) {
            ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "[X]");
        } else {
            ImGui::TextDisabled("[ ]");
        }

        // Arrow to show flow (except for last item)
        if (i < sources.size() - 1) {
            ImGui::SameLine(310);
            ImGui::TextDisabled("->");
        }
    }

    ImGui::Separator();

    // Determine which source "wins" for each property
    const char* shapeWinner = L("appearance.override_level_default");
    const char* colorWinner = L("appearance.trace_system");
    const char* scaleWinner = L("appearance.override_level_default");

    for (const auto& src : sources) {
        if (src.hasShape) shapeWinner = src.levelName;
        if (src.hasColor) colorWinner = src.levelName;
        if (src.hasScale) scaleWinner = src.levelName;
    }

    ImGui::Text("%s: %s  |  %s: %s  |  %s: %s",
                L("appearance.trace_shape"), shapeWinner,
                L("appearance.trace_color"), colorWinner,
                L("appearance.trace_scale"), scaleWinner);

    ImGui::EndGroup();

    ImGui::TreePop();
}

// Helper to render a single color swatch button
static bool renderColorSwatch(const char* id, float r, float g, float b, const char* tooltip, bool allowSave = false) {
    bool clicked = false;

    ImGui::PushID(id);

    ImVec4 color(r, g, b, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_Button, color);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(r * 1.1f, g * 1.1f, b * 1.1f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(r * 0.9f, g * 0.9f, b * 0.9f, 1.0f));

    // Small color button (18x18)
    if (ImGui::Button("##swatch", ImVec2(18, 18))) {
        clicked = true;
    }

    ImGui::PopStyleColor(3);

    // Tooltip on hover
    if (ImGui::IsItemHovered()) {
        if (allowSave) {
            ImGui::SetTooltip("%s\n%s", tooltip, L("appearance.color_right_click_save"));
        } else {
            ImGui::SetTooltip("%s", tooltip);
        }
    }

    ImGui::PopID();
    return clicked;
}

bool UIActorAppearancePanel::renderColorPresetsBar(ActorAppearanceSettings& settings,
                                                     float& outR, float& outG, float& outB,
                                                     float currentR, float currentG, float currentB) {
    bool colorSelected = false;

    ImGui::BeginGroup();
    ImGui::TextDisabled("%s", L("appearance.color_presets"));

    ImGui::Text("%s:", L("appearance.preset_classes"));
    ImGui::SameLine();

    struct ClassPreset {
        const char* name;
        parser::ClassColor color;
    };

    static const ClassPreset classPresets[] = {
        {"Death Knight", parser::ClassColors::DeathKnight},
        {"Demon Hunter", parser::ClassColors::DemonHunter},
        {"Druid", parser::ClassColors::Druid},
        {"Evoker", parser::ClassColors::Evoker},
        {"Hunter", parser::ClassColors::Hunter},
        {"Mage", parser::ClassColors::Mage},
        {"Monk", parser::ClassColors::Monk},
        {"Paladin", parser::ClassColors::Paladin},
        {"Priest", parser::ClassColors::Priest},
        {"Rogue", parser::ClassColors::Rogue},
        {"Shaman", parser::ClassColors::Shaman},
        {"Warlock", parser::ClassColors::Warlock},
        {"Warrior", parser::ClassColors::Warrior},
    };

    for (size_t i = 0; i < std::size(classPresets); ++i) {
        if (i > 0) ImGui::SameLine(0, 2);
        const auto& preset = classPresets[i];
        char id[32];
        snprintf(id, sizeof(id), "class_%zu", i);
        if (renderColorSwatch(id, preset.color.rf(), preset.color.gf(), preset.color.bf(), preset.name)) {
            outR = preset.color.rf();
            outG = preset.color.gf();
            outB = preset.color.bf();
            colorSelected = true;
        }
    }

    ImGui::Text("%s:", L("appearance.preset_roles"));
    ImGui::SameLine();

    struct RolePreset {
        const char* nameKey;  // Localization key, resolved at render time
        float r, g, b;
    };

    static const RolePreset rolePresets[] = {
        {"role.tank",       0.3f, 0.5f, 1.0f},    // Blue
        {"role.healer",     0.3f, 1.0f, 0.3f},    // Green
        {"role.melee_dps",  1.0f, 0.3f, 0.3f},    // Red
        {"role.ranged_dps", 1.0f, 0.8f, 0.3f},    // Orange/Yellow
    };

    for (size_t i = 0; i < std::size(rolePresets); ++i) {
        if (i > 0) ImGui::SameLine(0, 2);
        const auto& preset = rolePresets[i];
        char id[32];
        snprintf(id, sizeof(id), "role_%zu", i);
        if (renderColorSwatch(id, preset.r, preset.g, preset.b, L(preset.nameKey))) {
            outR = preset.r;
            outG = preset.g;
            outB = preset.b;
            colorSelected = true;
        }
    }

    ImGui::Text("%s:", L("appearance.preset_custom"));
    ImGui::SameLine();

    auto& defaults = settings.getRoleDefaults();
    for (size_t i = 0; i < RoleDefaults::MAX_CUSTOM_COLORS; ++i) {
        if (i > 0) ImGui::SameLine(0, 2);

        const auto& preset = defaults.customColors[i];
        char id[32];
        snprintf(id, sizeof(id), "custom_%zu", i);
        char tooltip[64];

        ImGui::PushID(id);

        if (preset.isSet) {
            snprintf(tooltip, sizeof(tooltip), "%s %zu", L("appearance.preset_slot"), i + 1);
            if (renderColorSwatch(id, preset.r, preset.g, preset.b, tooltip, true)) {
                outR = preset.r;
                outG = preset.g;
                outB = preset.b;
                colorSelected = true;
            }

            // Right-click to save current color
            if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
                defaults.customColors[i].r = currentR;
                defaults.customColors[i].g = currentG;
                defaults.customColors[i].b = currentB;
            }
        } else {
            // Empty slot - show dashed border / gray
            snprintf(tooltip, sizeof(tooltip), "%s %zu (%s)", L("appearance.preset_slot"), i + 1, L("appearance.preset_empty"));

            ImVec4 emptyColor(0.3f, 0.3f, 0.3f, 1.0f);
            ImGui::PushStyleColor(ImGuiCol_Button, emptyColor);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.4f, 0.4f, 0.4f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.35f, 0.35f, 0.35f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);

            if (ImGui::Button("+##empty", ImVec2(18, 18))) {
                // Left-click on empty slot saves current color
                defaults.customColors[i].r = currentR;
                defaults.customColors[i].g = currentG;
                defaults.customColors[i].b = currentB;
                defaults.customColors[i].isSet = true;
            }

            ImGui::PopStyleVar();
            ImGui::PopStyleColor(4);

            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", tooltip);
            }
        }

        // Context menu for clearing custom slots
        if (preset.isSet && ImGui::BeginPopupContextItem("clear_slot")) {
            if (ImGui::MenuItem(L("appearance.preset_clear_slot"))) {
                defaults.customColors[i].isSet = false;
            }
            ImGui::EndPopup();
        }

        ImGui::PopID();
    }

    ImGui::EndGroup();

    return colorSelected;
}

void UIActorAppearancePanel::renderOverrideBadges(const ActorAppearanceSettings& settings,
                                                    const std::string& guid,
                                                    const std::string& actorName,
                                                    uint16_t specId) {
    ActorRole role = (specId > 0) ? ActorAppearanceSettings::getRoleFromSpecId(specId) : ActorRole::Unknown;
    std::string className = (specId > 0) ? ActorAppearanceSettings::getClassFromSpecId(specId) : "";

    // Spec, class and role badges match their override trace colours; GUID
    // and name have their own so they stand out
    const ImVec4 colorGuid(1.0f, 0.85f, 0.0f, 1.0f);   // Gold for GUID
    const ImVec4 colorName(0.9f, 0.6f, 0.3f, 1.0f);    // Orange for Name
    const ImVec4 colorSpec(0.9f, 0.9f, 0.6f, 1.0f);    // Yellow for Spec
    const ImVec4 colorClass(0.9f, 0.6f, 0.9f, 1.0f);   // Purple for Class
    const ImVec4 colorRole(0.6f, 0.6f, 0.9f, 1.0f);    // Blue for Role

    // Badges sit on one line, highest priority first
    bool anyBadge = false;
    const auto badge = [&anyBadge](const ImVec4& color, const char* textKey,
                                   const char* tooltipKey) {
        if (anyBadge) ImGui::SameLine(0, 2);
        ImGui::TextColored(color, "%s", L(textKey));
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", L(tooltipKey));
        }
        anyBadge = true;
    };
    const auto isSet = [](const AppearanceOverride* level) {
        return level && level->hasAnyOverride();
    };

    if (settings.hasGuidOverride(guid)) {
        badge(colorGuid, "appearance.badge_guid", "appearance.badge_guid_tooltip");
    }
    if (!actorName.empty() && settings.hasNameGroupOverride(actorName)) {
        badge(colorName, "appearance.badge_name", "appearance.badge_name_tooltip");
    }
    if (specId > 0 && isSet(settings.getSpecOverride(specId))) {
        badge(colorSpec, "appearance.badge_spec", "appearance.badge_spec_tooltip");
    }
    if (!className.empty() && isSet(settings.getClassOverride(className))) {
        badge(colorClass, "appearance.badge_class", "appearance.badge_class_tooltip");
    }
    if (role != ActorRole::Unknown && isSet(settings.getRoleOverride(role))) {
        badge(colorRole, "appearance.badge_role", "appearance.badge_role_tooltip");
    }
}

