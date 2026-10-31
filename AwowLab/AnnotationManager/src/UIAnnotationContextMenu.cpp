#include "UIAnnotationContextMenu.h"
#include "AnnotationManager.h"
#include <imgui.h>
#include "Core/LocalizationManager.h"

UIAnnotationContextMenu::ActorNavigationRequest UIAnnotationContextMenu::s_pendingNavigation;

void UIAnnotationContextMenu::openAt(float worldX, float worldY,
                                      const std::string& nearbyActorGuid,
                                      const std::string& nearbyActorName,
                                      uint16_t nearbyActorSpecId) {
    pendingWorldX_ = worldX;
    pendingWorldY_ = worldY;
    pendingActorGuid_ = nearbyActorGuid;
    pendingActorName_ = nearbyActorName;
    pendingActorSpecId_ = nearbyActorSpecId;
    isOpen_ = true;
    ImGui::OpenPopup("AnnotationContextMenu");
}

UIAnnotationContextMenu::ActorNavigationRequest UIAnnotationContextMenu::consumePendingNavigation() {
    ActorNavigationRequest request = s_pendingNavigation;
    s_pendingNavigation = ActorNavigationRequest{};
    return request;
}

uint64_t UIAnnotationContextMenu::render(AnnotationManager* manager, int32_t currentTimestamp_ms) {
    if (!manager) return 0;

    uint64_t createdId = 0;

    if (ImGui::BeginPopup("AnnotationContextMenu")) {
        isOpen_ = true;

        if (ImGui::BeginMenu("Add Shape")) {
            // Circle (void zones)
            if (ImGui::MenuItem("Circle")) {
                createdId = manager->createAnnotation(AnnotationShape::Circle,
                                                       pendingWorldX_, pendingWorldY_,
                                                       currentTimestamp_ms);
            }

            // Rectangle (beam zone)
            if (ImGui::MenuItem("Rectangle")) {
                createdId = manager->createAnnotation(AnnotationShape::Rectangle,
                                                       pendingWorldX_, pendingWorldY_,
                                                       currentTimestamp_ms);
            }

            // Cone (cleave indicator)
            if (ImGui::MenuItem("Cone")) {
                createdId = manager->createAnnotation(AnnotationShape::Cone,
                                                       pendingWorldX_, pendingWorldY_,
                                                       currentTimestamp_ms);
            }

            // Line (directional indicator)
            if (ImGui::MenuItem("Line")) {
                createdId = manager->createAnnotation(AnnotationShape::Line,
                                                       pendingWorldX_, pendingWorldY_,
                                                       currentTimestamp_ms);
            }

            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu(L("Add World Marker"))) {
            if (ImGui::MenuItem(L("Star (Yellow)"))) {
                createdId = manager->createWorldMarker(WorldMarkerType::Star,
                                                        pendingWorldX_, pendingWorldY_,
                                                        currentTimestamp_ms);
            }
            if (ImGui::MenuItem(L("Circle (Orange)"))) {
                createdId = manager->createWorldMarker(WorldMarkerType::Circle,
                                                        pendingWorldX_, pendingWorldY_,
                                                        currentTimestamp_ms);
            }
            if (ImGui::MenuItem(L("Diamond (Purple)"))) {
                createdId = manager->createWorldMarker(WorldMarkerType::Diamond,
                                                        pendingWorldX_, pendingWorldY_,
                                                        currentTimestamp_ms);
            }
            if (ImGui::MenuItem(L("Triangle (Green)"))) {
                createdId = manager->createWorldMarker(WorldMarkerType::Triangle,
                                                        pendingWorldX_, pendingWorldY_,
                                                        currentTimestamp_ms);
            }
            if (ImGui::MenuItem(L("Moon (Silver)"))) {
                createdId = manager->createWorldMarker(WorldMarkerType::Moon,
                                                        pendingWorldX_, pendingWorldY_,
                                                        currentTimestamp_ms);
            }
            if (ImGui::MenuItem(L("Square (Blue)"))) {
                createdId = manager->createWorldMarker(WorldMarkerType::Square,
                                                        pendingWorldX_, pendingWorldY_,
                                                        currentTimestamp_ms);
            }
            if (ImGui::MenuItem(L("Cross (Red)"))) {
                createdId = manager->createWorldMarker(WorldMarkerType::Cross,
                                                        pendingWorldX_, pendingWorldY_,
                                                        currentTimestamp_ms);
            }
            if (ImGui::MenuItem(L("Skull (White)"))) {
                createdId = manager->createWorldMarker(WorldMarkerType::Skull,
                                                        pendingWorldX_, pendingWorldY_,
                                                        currentTimestamp_ms);
            }
            ImGui::EndMenu();
        }

        // Only offered when an actor is near the click
        if (!pendingActorGuid_.empty()) {
            ImGui::Separator();
            std::string anchorLabel = "Anchor to " + pendingActorName_;
            if (ImGui::BeginMenu(anchorLabel.c_str())) {
                if (ImGui::MenuItem("Circle")) {
                    createdId = manager->createActorAnchoredAnnotation(
                        AnnotationShape::Circle, pendingActorGuid_, currentTimestamp_ms);
                }
                if (ImGui::MenuItem("Rectangle")) {
                    createdId = manager->createActorAnchoredAnnotation(
                        AnnotationShape::Rectangle, pendingActorGuid_, currentTimestamp_ms);
                }
                if (ImGui::MenuItem("Cone")) {
                    createdId = manager->createActorAnchoredAnnotation(
                        AnnotationShape::Cone, pendingActorGuid_, currentTimestamp_ms);
                }
                if (ImGui::MenuItem("Line")) {
                    createdId = manager->createActorAnchoredAnnotation(
                        AnnotationShape::Line, pendingActorGuid_, currentTimestamp_ms);
                }
                ImGui::EndMenu();
            }

            ImGui::Separator();

            if (ImGui::MenuItem(L("Customize Appearance"))) {
                s_pendingNavigation.target = ActorNavigationRequest::Target::AppearancePanel;
                s_pendingNavigation.actorGuid = pendingActorGuid_;
                s_pendingNavigation.actorName = pendingActorName_;
                ImGui::CloseCurrentPopup();
            }

            // Players only; needs a known spec
            if (pendingActorSpecId_ > 0) {
                if (ImGui::MenuItem(L("Spell Display Settings"))) {
                    s_pendingNavigation.target = ActorNavigationRequest::Target::SpellSettings;
                    s_pendingNavigation.specId = pendingActorSpecId_;
                    ImGui::CloseCurrentPopup();
                }
            }
        }

        // Only offered when an annotation is selected
        if (manager->hasSelection()) {
            ImGui::Separator();
            if (ImGui::MenuItem("Delete Selected")) {
                manager->deleteSelected();
            }
        }

        ImGui::EndPopup();
    } else {
        isOpen_ = false;
    }

    return createdId;
}
