#pragma once
#include "Color/ActorColorGenerator.h"
#include <memory>
#include <string>
#include <unordered_map>
#include <map>
#include <optional>

enum class ActorType;

// Player info for static list
struct PlayerInfo {
    std::string guid;
    std::string name;
    ActorColor color;
    bool is_active;  // Currently visible in time window
};

class UIActorSelector {
public:
    UIActorSelector() = default;

    // Update the master list of all known players (called when new data is loaded)
    void updateKnownPlayers(const std::shared_ptr<ActorColorGenerator>& colorGen,
                           const std::unordered_map<std::string, std::string>& allPlayers);

    // Get the currently selected actor GUID (if any)
    std::optional<std::string> getSelectedActor() const;

    // Check if a specific actor is selected
    bool isActorSelected(const std::string& guid) const;

    // Clear the current selection
    void clearSelection();

    // Set the selected actor
    void setSelectedActor(const std::string& guid);

private:
    std::optional<std::string> m_selectedActorGuid;

    // Alphabetically sorted map of player name -> PlayerInfo
    std::map<std::string, PlayerInfo> m_allKnownPlayers;
};
