#pragma once

#include <cstdint>
#include <string>

namespace BrowserHelper {
    // Open a URL in the default browser
    void openUrl(const std::string& url);

    // Open Wowhead spell page in the default browser
    void openWowheadSpell(uint32_t spellId);
}
