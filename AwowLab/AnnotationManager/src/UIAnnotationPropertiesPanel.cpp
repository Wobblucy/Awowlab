#include "UIAnnotationPropertiesPanel.h"
#include "AnnotationManager.h"
#include <imgui.h>
#include <string>

bool UIAnnotationPropertiesPanel::render(AnnotationManager* manager, [[maybe_unused]] int32_t currentTimestamp_ms) {
    if (!visible_ || !manager) {
        return false;
    }

    bool modified = false;

    ImGui::SetNextWindowSize(ImVec2(300, 400), ImGuiCond_FirstUseEver);
    if (ImGui::Begin((std::string(L("Annotation Properties")) + "###AnnotationProperties").c_str(), &visible_)) {
        if (!manager->hasSelection()) {
            ImGui::TextDisabled("%s", L("No annotation selected"));
        } else {
            Annotation* selected = manager->getSelectedAnnotation();
            if (selected) {
                // Shape is read-only once created
                const char* shapeNames[] = { L("Circle"), L("Rectangle"), L("Cone"), L("Line"), L("Star"), L("Donut") };
                int shapeIdx = static_cast<int>(selected->shape);
                if (shapeIdx >= 0 && shapeIdx < IM_ARRAYSIZE(shapeNames)) {
                    ImGui::Text("%s: %s", L("Shape"), shapeNames[shapeIdx]);
                }

                ImGui::Separator();

                if (ImGui::ColorEdit4(L("Color"), selected->color.data())) {
                    modified = true;
                }

                ImGui::Separator();

                // Striped/hatched styles mark positions that are uncertain
                const char* styleNames[] = { L("Solid"), L("Striped"), L("Hatched"), L("Ripple Rings"), L("Swirl") };
                int styleIdx = static_cast<int>(selected->visualStyle);
                if (ImGui::Combo(L("Visual Style"), &styleIdx, styleNames, 5)) {
                    selected->visualStyle = static_cast<AnnotationVisualStyle>(styleIdx);
                    modified = true;
                }

                if (selected->visualStyle != AnnotationVisualStyle::Solid) {
                    // Stripe spacing applies to all non-solid styles
                    if (ImGui::SliderFloat(L("Stripe Spacing"), &selected->stripeSpacing, 0.5f, 10.0f, L("%.1f yards"))) {
                        modified = true;
                    }

                    if (selected->visualStyle == AnnotationVisualStyle::RippleRings) {
                        if (ImGui::SliderFloat(L("Ring Speed"), &selected->rippleSpeed, 1.0f, 20.0f, L("%.1f yards/s"))) {
                            modified = true;
                        }
                        if (ImGui::SliderFloat(L("Ring Count"), &selected->rippleCount, 1.0f, 10.0f, "%.1f")) {
                            modified = true;
                        }
                    }
                }

                ImGui::Separator();

                // Edge effects
                ImGui::Text(L("Visual Polish"));
                ImGui::Spacing();

                if (ImGui::Checkbox(L("Edge Fade"), &selected->enableEdgeFade)) {
                    modified = true;
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip(L("Soft alpha fade at edges for a smoother look"));
                }
                if (selected->enableEdgeFade) {
                    ImGui::Indent();
                    if (ImGui::SliderFloat(L("Fade Width"), &selected->edgeFadeWidth, 0.01f, 0.4f, "%.2f")) {
                        modified = true;
                    }
                    ImGui::Unindent();
                }

                if (ImGui::Checkbox(L("Outline"), &selected->enableOutline)) {
                    modified = true;
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip(L("Adds a darker border around the shape"));
                }
                if (selected->enableOutline) {
                    ImGui::Indent();
                    if (ImGui::SliderFloat(L("Outline Width"), &selected->outlineWidth, 0.01f, 0.2f, "%.2f")) {
                        modified = true;
                    }
                    ImGui::Unindent();
                }

                if (ImGui::Checkbox(L("Edge Glow"), &selected->enableGlow)) {
                    modified = true;
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip(L("Adds a bright glow effect at edges"));
                }
                if (selected->enableGlow) {
                    ImGui::Indent();
                    if (ImGui::SliderFloat(L("Glow Intensity"), &selected->glowIntensity, 0.1f, 3.0f, "%.1f")) {
                        modified = true;
                    }
                    ImGui::Unindent();
                }

                if (ImGui::Checkbox(L("Gradient"), &selected->enableGradient)) {
                    modified = true;
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip(L("Color gradient from center to edge"));
                }

                ImGui::Separator();

                switch (selected->shape) {
                    case AnnotationShape::Circle:
                        renderCircleControls(*selected);
                        break;
                    case AnnotationShape::Rectangle:
                        renderRectangleControls(*selected);
                        break;
                    case AnnotationShape::Cone:
                        renderConeControls(*selected);
                        break;
                    case AnnotationShape::Line:
                        renderLineControls(*selected);
                        break;
                    default:
                        break;
                }

                if (selected->worldMarkerType != WorldMarkerType::None) {
                    ImGui::Separator();
                    ImGui::TextUnformatted(L("World Marker Settings"));

                    if (ImGui::SliderFloat(L("Symbol Height (yards)"), &selected->markerHeight, 2.0f, 20.0f, "%.1f")) {
                        modified = true;
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip(L("Height of floating symbol above ground"));
                    }
                }

                ImGui::Separator();

                const char* durationTypes[] = { L("Encounter"), L("Timed") };
                int durationType = static_cast<int>(selected->durationType);
                if (ImGui::Combo(L("Duration"), &durationType, durationTypes, 2)) {
                    selected->durationType = static_cast<AnnotationDuration>(durationType);
                    modified = true;
                }

                if (selected->durationType == AnnotationDuration::Timed) {
                    float durationSec = selected->durationMs / 1000.0f;
                    if (ImGui::SliderFloat(L("Duration (sec)"), &durationSec, 1.0f, 120.0f, "%.1f s")) {
                        selected->durationMs = static_cast<int32_t>(durationSec * 1000.0f);
                        modified = true;
                    }
                }

                ImGui::Separator();

                if (ImGui::Button(L("Delete"))) {
                    manager->deleteSelected();
                }
            }
        }
    }
    ImGui::End();

    return modified;
}

void UIAnnotationPropertiesPanel::renderCircleControls(Annotation& annotation) {
    // Circle dimensions: [radius, height, unused, unused]
    if (ImGui::SliderFloat(L("Radius (yards)"), &annotation.dimensions[0], 1.0f, 50.0f, "%.1f")) {
    }
    if (ImGui::SliderFloat(L("Height"), &annotation.dimensions[1], 0.1f, 10.0f, "%.1f")) {
    }
}

void UIAnnotationPropertiesPanel::renderRectangleControls(Annotation& annotation) {
    // Rectangle dimensions: [width, length, height, rotation]
    if (ImGui::SliderFloat(L("Width (yards)"), &annotation.dimensions[0], 1.0f, 50.0f, "%.1f")) {
    }
    if (ImGui::SliderFloat(L("Length (yards)"), &annotation.dimensions[1], 1.0f, 100.0f, "%.1f")) {
    }
    if (ImGui::SliderFloat(L("Height"), &annotation.dimensions[2], 0.1f, 5.0f, "%.1f")) {
    }
    float rotationDeg = annotation.dimensions[3] * (180.0f / 3.14159f);
    if (ImGui::SliderFloat(L("Rotation"), &rotationDeg, 0.0f, 360.0f, L("%.0f deg"))) {
        annotation.dimensions[3] = rotationDeg * (3.14159f / 180.0f);
    }
}

void UIAnnotationPropertiesPanel::renderConeControls(Annotation& annotation) {
    // Cone dimensions: [length, angle, height, rotation]
    if (ImGui::SliderFloat(L("Length (yards)"), &annotation.dimensions[0], 5.0f, 60.0f, "%.1f")) {
    }
    if (ImGui::SliderFloat(L("Angle"), &annotation.dimensions[1], 15.0f, 180.0f, L("%.0f deg"))) {
    }
    if (ImGui::SliderFloat(L("Height"), &annotation.dimensions[2], 0.1f, 5.0f, "%.1f")) {
    }
    float rotationDeg = annotation.dimensions[3] * (180.0f / 3.14159f);
    if (ImGui::SliderFloat(L("Rotation"), &rotationDeg, 0.0f, 360.0f, L("%.0f deg"))) {
        annotation.dimensions[3] = rotationDeg * (3.14159f / 180.0f);
    }
}

void UIAnnotationPropertiesPanel::renderLineControls(Annotation& annotation) {
    // Line dimensions: [length, width, height, rotation]
    if (ImGui::SliderFloat(L("Length (yards)"), &annotation.dimensions[0], 5.0f, 100.0f, "%.1f")) {
    }
    if (ImGui::SliderFloat(L("Width (yards)"), &annotation.dimensions[1], 0.5f, 10.0f, "%.1f")) {
    }
    if (ImGui::SliderFloat(L("Height"), &annotation.dimensions[2], 0.1f, 3.0f, "%.1f")) {
    }
    float rotationDeg = annotation.dimensions[3] * (180.0f / 3.14159f);
    if (ImGui::SliderFloat(L("Rotation"), &rotationDeg, 0.0f, 360.0f, L("%.0f deg"))) {
        annotation.dimensions[3] = rotationDeg * (3.14159f / 180.0f);
    }
}
