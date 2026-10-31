#pragma once

#include <cstdint>
#include <string>

#include "../UnifiedSettings.h"

/**
 * WarcraftLogs values stored in the shared settings file.
 *
 * The shared UnifiedSettings code has no fields for these; they ride in
 * AllSettings::extraValues under the same entry names older builds wrote
 * ("wclTokenExpiresAt", ...), so existing settings files keep working and
 * overlay-only builds carry the values through untouched.
 *
 * The OAuth access token gets extra care: the settings file key ships in
 * the open-source overlay, so anything in the file is readable. The token
 * is therefore wrapped with the Windows user-account key (DPAPI) before it
 * goes into the file, under the "wclAccessTokenProtected" entry. Older
 * builds wrote it as plain text under "wclAccessToken"; readFrom still
 * accepts that, and writeTo drops the plain entry once the protected one
 * exists. The expiry timestamp is not a secret and stays an ordinary entry.
 *
 * Main-app only - none of this compiles into the standalone overlay.
 */
struct WclSettings {
    // OAuth token
    std::string accessToken;
    int64_t tokenExpiresAt = 0;

    /** Parse the WCL entries out of a loaded settings snapshot. */
    static WclSettings readFrom(const UnifiedSettings::AllSettings& settings);

    /** Write these values into the settings snapshot for the next save. */
    void writeTo(UnifiedSettings::AllSettings& settings) const;
};

namespace awow {

/**
 * Auth error message shared between the WCL dialog and the landing page.
 * Set when a query fails due to auth, cleared on successful auth.
 * Runtime-only - never persisted.
 */
std::string& wclAuthError();

/**
 * A WarcraftLogs refusal that is not a sign-in problem, such as the user's
 * points for the hour being spent. The WCL dialog shows it; unlike
 * wclAuthError it leaves the token alone. Runtime-only.
 */
std::string& wclNotice();

/**
 * One-time upgrade for settings files written by older builds: if a
 * plain-text WCL token is still in the cached settings, rewrite it
 * DPAPI-protected and save so the plain entry leaves the file for good.
 * Call once at startup, right after the settings cache loads.
 */
void moveWclTokenToProtectedStorage();

} // namespace awow
