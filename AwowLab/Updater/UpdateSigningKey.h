#pragma once

#include "AwowLabBuildOptions.h"

#include <cstdint>

namespace awow::updater {

// Public half of the release signing key (ECDSA P-256).
// Layout: X coordinate (32 bytes) followed by Y coordinate (32 bytes).
//
// The key is signing_public_key in awowlab.config.json at the repository
// root, turned into bytes at configure time. To set up signing:
//   1. Run: python tools/release/generate-signing-key.py
//      (keeps the private key outside both repos - follow the prompts; it
//      offers to write the public key into awowlab.config.json)
//   2. Re-run CMake and rebuild the updater
//
// While a key is configured, every release must carry a valid
// manifest.json.sig; one without it is refused. A fork that publishes its
// own releases generates its own key pair and puts its public key in its
// awowlab.config.json. See tools/release/SIGNING.md.
inline constexpr bool kSigningKeyConfigured = AWOWLAB_SIGNING_KEY_CONFIGURED != 0;

inline constexpr const uint8_t (&kReleasePublicKey)[64] = awow::build::kReleasePublicKey;

} // namespace awow::updater
