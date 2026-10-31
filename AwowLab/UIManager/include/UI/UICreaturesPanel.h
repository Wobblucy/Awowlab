#pragma once
#include <memory>
#include <unordered_map>
#include <string>
#include <string_view>

class ActorColorGenerator;
struct InterpolatedPosition;
struct InterpolatedResource;

class UICreaturesPanel {
public:
    UICreaturesPanel() = default;

    // Render the creatures/pets/vehicles window
    void render(const std::shared_ptr<ActorColorGenerator>& colorGen,
                const std::unordered_map<std::string_view, InterpolatedPosition>& actorPositions,
                const std::unordered_map<std::string_view, InterpolatedResource>& actorResources,
                const std::unordered_map<std::string_view, std::string_view>& guidToName,
                bool renderPets);
};
