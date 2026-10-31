#pragma once
#include <cstdint>
#include <string>

class AuraDatabase;
class SpellIconLoader;
class ActorColorGenerator;

// Panel displaying the focused actor's active buffs
// Uses SpellAssignmentSettings exclusions to filter which buffs to show
class UIPlayerBuffPanel {
public:
    UIPlayerBuffPanel();

    void render(const AuraDatabase* auraDb,
                SpellIconLoader* iconLoader,
                const ActorColorGenerator* colorGen,
                const std::string& targetGuid,
                uint32_t currentTimeMs);

    bool isVisible() const { return visible_; }
    void setVisible(bool v) { visible_ = v; }
    bool* getVisiblePtr() { return &visible_; }

private:
    bool visible_ = false;
    float iconSize_ = 32.0f;
    float iconSpacing_ = 4.0f;
};
