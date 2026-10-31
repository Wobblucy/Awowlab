#pragma once

#include <cstdint>
#include <string>

namespace awow::updater {

// The checks the updater makes before trusting a release. They live apart
// from Updater so they can be tested without a network.
namespace ReleaseChecks {

// Checks manifest.json against its detached signature. signature is the
// content of manifest.json.sig, or null if it couldn't be downloaded; a
// missing signature fails, since anyone able to swap the manifest could also
// drop the .sig. publicKey is X then Y, 32 bytes each. On failure, error
// holds the reason.
bool checkManifestSignature(const std::string& manifest,
                            const std::string* signature,
                            const uint8_t publicKey[64],
                            std::string& error);

// True if url lies under https://github.com/<owner>/<repo>/releases/download/.
// The manifest URL comes from version.json, which is not signed, so it must
// not be able to send the updater to another host or repository. Owner and
// repository compare case-insensitively, as GitHub does.
bool isReleaseAssetUrl(const std::string& url,
                       const std::string& owner,
                       const std::string& repo);

// A message refusing the install if available is older than installed;
// empty if it isn't, or if either version is unknown.
std::string downgradeError(const std::string& available,
                           const std::string& installed);

} // namespace ReleaseChecks

} // namespace awow::updater
