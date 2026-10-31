#include <catch2/catch_test_macros.hpp>
#include "LocalizationManager.h"
#include <filesystem>
#include <string>

// Guards the shipped en_US.csv itself: every key the wizard asks for must
// resolve to real text, not echo back as the key name.
TEST_CASE("shipped en_US.csv covers the wizard keys", "[localization][shipped]") {
    auto& mgr = LocalizationManager::instance();
    REQUIRE(std::filesystem::exists(LANG_DIR "/en_US.csv"));
    REQUIRE(mgr.loadTranslations(LANG_DIR));

    const char* keys[] = {
        "wizard.source.filter_label",
        "wizard.source.filter_any",
        "wizard.source.filter_creature",
        "wizard.source.filter_environment",
        "wizard.source.filter_any_debuff_tip",
        "wizard.source.filter_any_buff_tip",
        "wizard.visual.polish",
        "wizard.visual.use_secondary_color_tip",
        "wizard.position.anchor_at_caster",
        "wizard.advanced.pool_interval_tip",
    };
    for (const char* k : keys) {
        INFO("key: " << k);
        std::string text = mgr.get(k);
        CHECK(text != k);        // echoing the key back means the row is missing
        CHECK_FALSE(text.empty());
    }

    // The three combo entries must be distinct text, not one repeated label
    std::string a = mgr.get("wizard.source.filter_any");
    std::string b = mgr.get("wizard.source.filter_creature");
    std::string c = mgr.get("wizard.source.filter_environment");
    CHECK(a == "Any Source");
    CHECK(b == "Specific Creature");
    CHECK(c == "Environment Only");
    CHECK(a != b);
    CHECK(b != c);

    // Values containing a comma survive the split-on-first-comma parser
    CHECK(std::string(mgr.get("wizard.visual.use_secondary_color_tip"))
          == "Use a custom color for gradients, outlines, and patterns instead of auto-derived");
}
