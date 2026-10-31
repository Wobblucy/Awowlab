#include "Core/BrowserHelper.h"
#include <string>

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#endif

void BrowserHelper::openUrl(const std::string& url) {
#ifdef _WIN32
    ShellExecuteA(nullptr, "open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#endif
}

void BrowserHelper::openWowheadSpell(uint32_t spellId) {
    std::string url = "https://www.wowhead.com/spell=" + std::to_string(spellId);
    openUrl(url);
}
