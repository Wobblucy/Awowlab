// The settings file key is SHA-256 of settings_key from the builder's config.
// Earlier builds hashed two constants joined together, so the release key is
// those two joined into one string; these pin that the hash still matches.

#include <catch2/catch_test_macros.hpp>

#include "UnifiedSettings.h"

#include <openssl/evp.h>

#include <cstdint>
#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

namespace {

// SHA-256 over the parts in order, without joining them first
std::vector<uint8_t> sha256Of(std::initializer_list<std::string_view> parts) {
    std::vector<uint8_t> digest(32);
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    REQUIRE(ctx != nullptr);
    REQUIRE(EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr) == 1);
    for (std::string_view part : parts) {
        REQUIRE(EVP_DigestUpdate(ctx, part.data(), part.size()) == 1);
    }
    unsigned int length = 0;
    REQUIRE(EVP_DigestFinal_ex(ctx, digest.data(), &length) == 1);
    EVP_MD_CTX_free(ctx);
    REQUIRE(length == 32);
    return digest;
}

std::string toHex(const std::vector<uint8_t>& bytes) {
    static constexpr char digits[] = "0123456789abcdef";
    std::string out;
    for (uint8_t b : bytes) {
        out += digits[b >> 4];
        out += digits[b & 0x0F];
    }
    return out;
}

} // namespace

TEST_CASE("Settings key is SHA-256 of the configured value", "[settings][crypto]") {
    // FIPS 180-2 test vector
    CHECK(toHex(UnifiedSettings::deriveKey("abc")) ==
          "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");

    const std::string value = "3f9c0d0e5b6a41c2a7d8e9f0a1b2c3d4e5f60718293a4b5c6d7e8f9012345678";
    CHECK(UnifiedSettings::deriveKey(value) == sha256Of({value}));
    CHECK(UnifiedSettings::deriveKey(value).size() == 32);
}

TEST_CASE("Joined salt and suffix derive the key the two-part scheme did", "[settings][crypto]") {
    // The old scheme hashed a salt followed by a suffix. Configuring the two
    // joined as one value has to give the same key, or existing settings
    // files stop opening. Stand-in strings: the real ones stay private.
    const std::string salt = "Example_Settings_Salt";
    const std::string suffix = "00000000-1111-2222-3333-444444444444";
    CHECK(UnifiedSettings::deriveKey(salt + suffix) == sha256Of({salt, suffix}));
    CHECK(UnifiedSettings::deriveKey(salt + suffix) != UnifiedSettings::deriveKey(salt));
}
