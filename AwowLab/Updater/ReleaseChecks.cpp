#include "ReleaseChecks.h"
#include "SignatureVerifier.h"
#include "ManifestParser.h"

#include <algorithm>
#include <cctype>

namespace awow::updater::ReleaseChecks {

bool checkManifestSignature(const std::string& manifest,
                            const std::string* signature,
                            const uint8_t publicKey[64],
                            std::string& error) {
    if (!signature) {
        error = "Release is not signed (manifest.json.sig is missing)";
        return false;
    }

    if (!SignatureVerifier::verify(
            reinterpret_cast<const uint8_t*>(manifest.data()),
            manifest.size(),
            *signature,
            publicKey)) {
        error = "Manifest signature check failed: " + SignatureVerifier::getLastError();
        return false;
    }

    return true;
}

namespace {

std::string toLower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

} // namespace

bool isReleaseAssetUrl(const std::string& url,
                       const std::string& owner,
                       const std::string& repo) {
    if (owner.empty() || repo.empty()) {
        return false;
    }

    const std::string prefix = "https://github.com/" + owner + "/" + repo + "/releases/download/";
    if (url.size() <= prefix.size() ||
        toLower(url.substr(0, prefix.size())) != toLower(prefix)) {
        return false;
    }

    // What follows is "<tag>/<file>". Refuse anything a server might
    // resolve to a different path: dot segments, escapes, backslashes,
    // and query or fragment parts.
    const std::string rest = url.substr(prefix.size());
    if (rest.find("..") != std::string::npos ||
        rest.find_first_of("%\\?#") != std::string::npos) {
        return false;
    }

    return true;
}

std::string downgradeError(const std::string& available,
                           const std::string& installed) {
    if (available.empty() || installed.empty()) {
        return {};
    }

    if (ManifestParser::compareVersions(available, installed) < 0) {
        return "Available version " + available +
               " is older than installed version " + installed +
               ". Run the updater with --allow-downgrade to install it anyway.";
    }
    return {};
}

} // namespace awow::updater::ReleaseChecks
