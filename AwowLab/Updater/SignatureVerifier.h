#pragma once

#include <cstdint>
#include <string>

namespace awow::updater {

// ECDSA P-256 verification through Windows BCrypt, like HashVerifier.
//
// tools/release/generate-manifest.py hashes the manifest bytes with SHA-256 and signs
// them with the release key. The .sig file holds base64 of the raw 64-byte
// r || s signature.
class SignatureVerifier {
public:
    // signatureBase64 may have surrounding whitespace. publicKey is the
    // uncompressed P-256 key, X then Y, 32 bytes each.
    static bool verify(const uint8_t* data, size_t size,
                       const std::string& signatureBase64,
                       const uint8_t publicKey[64]);

    static const std::string& getLastError();

private:
    static thread_local std::string lastError_;
};

} // namespace awow::updater
