#include "UI/UICreaturesPanel.h"
#include "Color/ActorColorGenerator.h"
#include "Animation/ActorPositionInterpolator.h"
#include "Core/LocalizationManager.h"
#include "../../Database/OutputWriter.h"  // For ActorType enum
#include <imgui.h>
#include <map>

void UICreaturesPanel::render(const std::shared_ptr<ActorColorGenerator>& colorGen,
                               const std::unordered_map<std::string_view, InterpolatedPosition>& actorPositions,
                               const std::unordered_map<std::string_view, InterpolatedResource>& actorResources,
                               [[maybe_unused]] const std::unordered_map<std::string_view, std::string_view>& guidToName,
                               bool renderPets) {
    ImGui::Begin("Creatures/Vehicles/Pets");

    std::vector<std::pair<std::string_view, ActorColor>> others;

    for (const auto& [guid, interpPos] : actorPositions) {
        if (!interpPos.hasPosition) {
            continue;
        }

        const ActorType* typePtr = colorGen->getActorType(guid);
        if (!typePtr) {
            continue;
        }

        ActorType type = *typePtr;

        // Skip players - they're shown in the Player Focus panel
        if (type == ActorType::Player) {
            continue;
        }

        if (type == ActorType::Pet && !renderPets) {
            continue;
        }

        ActorColor color = colorGen->getCachedColor(guid);
        others.push_back({guid, color});
    }

    if (others.empty()) {
        ImGui::TextDisabled("%s", L("status.no_creatures"));
    } else {
        // Group creatures by their base ID and assign sequential numbers
        std::map<std::string, int> creatureCounter;
        std::map<std::string_view, std::string> creatureIDs;

        // First pass: extract creature IDs
        for (const auto& [guid, color] : others) {
            size_t lastDash = guid.rfind('-');
            if (lastDash != std::string_view::npos && lastDash > 0) {
                size_t secondLastDash = guid.rfind('-', lastDash - 1);
                if (secondLastDash != std::string_view::npos) {
                    std::string creatureID(guid.substr(secondLastDash + 1, lastDash - secondLastDash - 1));
                    creatureIDs[guid] = creatureID;
                    if (creatureCounter.find(creatureID) == creatureCounter.end()) {
                        creatureCounter[creatureID] = 1;
                    }
                }
            }
        }

        // Second pass: display with sequential numbers and health bars
        std::map<std::string, int> currentNumbers;
        for (const auto& [guid, color] : others) {
            ImVec4 colorVec(color.r, color.g, color.b, 1.0f);
            std::string guidStr(guid);  // Convert to string for ImGui
            ImGui::ColorButton(guidStr.c_str(), colorVec, ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoBorder);
            ImGui::SameLine();

            auto it = creatureIDs.find(guid);
            if (it != creatureIDs.end()) {
                std::string creatureID = it->second;

                if (currentNumbers.find(creatureID) == currentNumbers.end()) {
                    currentNumbers[creatureID] = 1;
                }

                int number = currentNumbers[creatureID]++;

                std::string typePrefix = "Creature";
                if (guid.substr(0, 8) == "Vehicle-") {
                    typePrefix = "Vehicle";
                } else if (guid.substr(0, 4) == "Pet-") {
                    typePrefix = "Pet";
                }

                ImGui::Text("%s-%s #%d", typePrefix.c_str(), creatureID.c_str(), number);
            } else {
                ImGui::Text("%s", guidStr.c_str());
            }

            // Display health bar if resource data is available
            auto resIt = actorResources.find(guid);
            if (resIt != actorResources.end() && resIt->second.hasResource) {
                const auto& res = resIt->second;
                ImGui::SameLine();

                // Health bar with percentage
                ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.0f, 0.8f, 0.0f, 1.0f));  // Green
                char healthText[64];
                snprintf(healthText, sizeof(healthText), "%llu/%llu",
                         static_cast<unsigned long long>(res.currentHealth),
                         static_cast<unsigned long long>(res.maxHealth));
                ImGui::ProgressBar(res.healthPercent, ImVec2(100, 0), healthText);
                ImGui::PopStyleColor();
            }
        }
    }

    ImGui::End();
}
