// Spell display settings: the per-frame caches have to follow every edit.
#include <catch2/catch_test_macros.hpp>
#include "UI/SpellAssignmentSettings.h"

TEST_CASE("clearing a spec brings its spells back to the GCD tracker", "[spell-assignment]") {
    // No real spec uses this id, so nothing from the user's own settings lands on it
    constexpr uint16_t kSpecId = 9999;
    constexpr uint32_t kSpellId = 12345;

    spell_assignment::SpellAssignmentSettings settings;
    settings.setSpellGCDExcluded(kSpecId, kSpellId, true);
    REQUIRE(settings.isSpellExcludedFromGCDTracker(kSpecId, kSpellId));

    settings.clearAllForSpec(kSpecId);
    REQUIRE_FALSE(settings.isSpellExcludedFromGCDTracker(kSpecId, kSpellId));
}
