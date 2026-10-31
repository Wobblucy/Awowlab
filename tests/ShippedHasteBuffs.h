#pragma once

// The app loads data/haste_buffs_{version}.json at startup; a test executable
// has no startup, so suites that parse logs would otherwise see no haste buffs
// at all. Including this header registers a listener that loads the shipped
// file before the first test runs.
//
// Include it from exactly one source file per test executable: the listener
// registers once per inclusion. Needs HASTE_BUFFS_DIR defined to the data folder.

#include "Core/UnifiedSettings.h"
#include "Database/HasteBuffData.h"

#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>

#include <string>

namespace test_support {

// The file for the spell data version the app uses by default
inline std::string shippedHasteBuffsPath() {
    return std::string(HASTE_BUFFS_DIR) + "/haste_buffs_" +
           UnifiedSettings::AllSettings{}.spellDataVersion + ".json";
}

class ShippedHasteBuffsListener : public Catch::EventListenerBase {
public:
    using Catch::EventListenerBase::EventListenerBase;

    void testRunStarting(Catch::TestRunInfo const&) override {
        awow::loadHasteBuffs(shippedHasteBuffsPath());
    }
};

} // namespace test_support

CATCH_REGISTER_LISTENER(test_support::ShippedHasteBuffsListener)
