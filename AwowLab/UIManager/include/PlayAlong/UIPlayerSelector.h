#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace PlayAlong {

// Simplified player info for selection
struct PlayerInfo {
    std::string guid;
    std::string name;
    uint16_t specId = 0;
    // Class as the report states it ("DemonHunter"). Reports give a class for
    // every player but a spec only for some, so filter on this.
    std::string className;
};

// Result of player selection
struct PlayerSelection {
    bool selected = false;
    std::string playerGuid;
    std::string playerName;
    uint16_t specId = 0;
};

// UI panel for selecting which player to practice as
class UIPlayerSelector {
public:
    UIPlayerSelector() = default;

    void setPlayers(const std::vector<PlayerInfo>& players);

    // Returns true once a player was picked or the selector was cancelled
    bool render();

    const PlayerSelection& getSelection() const { return selection_; }

    void reset();

    void open() { isOpen_ = true; }

    bool wasCancelled() const { return cancelled_; }

private:
    const char* getSpecName(uint16_t specId) const;

    std::vector<PlayerInfo> players_;
    PlayerSelection selection_;
    bool isOpen_ = false;
    bool cancelled_ = false;

    // UI state
    int hoveredPlayer_ = -1;
    float animationTime_ = 0.0f;
};

} // namespace PlayAlong
