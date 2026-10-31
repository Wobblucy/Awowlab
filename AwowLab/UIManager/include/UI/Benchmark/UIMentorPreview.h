#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

// Summary of a prepared reference: the pull it came from, what was
// classified, and the cooldown windows the comparison will use. Shown after
// preparing so a bad reference (no cooldowns found, a low cast count) is
// noticed before it is used.

namespace benchmark { struct MentorProfile; }
class SpellIconLoader;

namespace benchmark_ui {

struct MentorPreviewContext {
    const benchmark::MentorProfile* profile = nullptr;

    // Display names, resolved by the caller so this panel needs no database.
    std::unordered_map<uint32_t, std::string> spellNames;

    std::string encounterName;
    SpellIconLoader* iconLoader = nullptr;
};

// What the user asked for after looking at the reference.
enum class PreviewAction : uint8_t {
    None = 0,
    ReopenPrepare,   // Back through the prepare steps, then re-cut
    Accept,          // Keep it and close
};

class UIMentorPreview {
public:
    UIMentorPreview() = default;

    PreviewAction render(const MentorPreviewContext& context);

    bool isVisible() const { return visible_; }
    void setVisible(bool visible);

private:
    void renderSummary(const MentorPreviewContext& context);
    void renderCooldowns(const MentorPreviewContext& context);
    void renderSpells(const MentorPreviewContext& context);

    std::string spellName(const MentorPreviewContext& context, uint32_t spellId) const;

    bool visible_ = false;
    bool justOpened_ = false;
    int activeTab_ = 0;
};

} // namespace benchmark_ui
