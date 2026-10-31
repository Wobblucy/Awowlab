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

    // AwowLab's estimate of the user's WarcraftLogs points spent this hour,
    // and when they reset (unix seconds, 0 when not known)
    int pointsUsedEstimate = 0;
    int64_t pointsResetAt = 0;

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
 * Estimated user points to load a whole fight from WarcraftLogs, at least 1.
 * Cost follows the number of events, so it is charged per minute: about 7 a
 * minute for raids and 1.5 for Mythic+ (measured 2026-10-31 on a 10-minute
 * 20-player Mythic kill and a 29-minute +24 key).
 */
inline constexpr double kRaidPointsPerMinute = 7.0;
inline constexpr double kMythicPlusPointsPerMinute = 1.5;
int estimateFightPoints(int64_t durationMs, bool mythicPlus);

/**
 * Points spent this hour as far as AwowLab knows. WarcraftLogs only says
 * whether a user has points left, never how many, so this adds up the
 * estimates of what AwowLab loaded; other sites the user signs in to spend
 * from the same pool. Zero once the reset time has passed.
 */
struct WclPointUsage {
    int used = 0;
    int64_t resetsAt = 0;  // unix seconds, 0 when not known
};
WclPointUsage wclPointUsage(int64_t now);

/** Add a load's estimate. Starts an hour-long window if none is known. */
void addWclPointUsage(int points, int64_t now);

/** Record WarcraftLogs' own reset time for the user's points. */
void setWclPointsResetIn(int seconds, int64_t now);

/**
 * One-time upgrade for settings files written by older builds: if a
 * plain-text WCL token is still in the cached settings, rewrite it
 * DPAPI-protected and save so the plain entry leaves the file for good.
 * Call once at startup, right after the settings cache loads.
 */
void moveWclTokenToProtectedStorage();

} // namespace awow
