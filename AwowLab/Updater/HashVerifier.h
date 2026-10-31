#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace awow::updater {

// SHA256 through the Windows BCrypt API, so the updater needs no crypto library.
// Hashes are lowercase hex.
class HashVerifier {
public:
    // Empty string on error.
    static std::string hashFile(const std::string& filePath);

    static std::string hashData(const uint8_t* data, size_t size);

    static bool verifyFile(const std::string& filePath, const std::string& expectedHash);

    static const std::string& getLastError();

private:
    static std::string bytesToHex(const std::vector<uint8_t>& bytes);
    static thread_local std::string lastError_;
};

} // namespace awow::updater
