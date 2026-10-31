// Updater security tests: manifest signature verification, version
// comparison (downgrade protection) and the fresh-install folder checks.
//
// Fixtures live in fixtures/updater/ and were produced by a throwaway
// test keypair - signing format matches tools/release/generate-manifest.py.

#include <catch2/catch_test_macros.hpp>

#include "SignatureVerifier.h"
#include "ManifestParser.h"
#include "ReleaseChecks.h"
#include "PathValidator.h"
#include "HttpDownloader.h"
#include "FileReplacer.h"
#include "HashVerifier.h"
#include "UpdateSigningKey.h"
#include "fixtures/updater/test_signing_key.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

using namespace awow::updater;

namespace {

std::string readFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    REQUIRE(file.good());
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

// The manifest fixture and its detached signature, as the updater downloads them.
struct SignedManifest {
    std::string manifest = readFile("fixtures/updater/test_manifest.json");
    std::string signature = readFile("fixtures/updater/test_manifest.json.sig");
};

bool verifies(const std::string& manifest, const std::string& signature,
              const uint8_t publicKey[64] = kTestPublicKey) {
    return SignatureVerifier::verify(
        reinterpret_cast<const uint8_t*>(manifest.data()), manifest.size(),
        signature, publicKey);
}

} // namespace

TEST_CASE("Valid manifest signature is accepted", "[updater][signature]") {
    SignedManifest release;
    CHECK(verifies(release.manifest, release.signature));
}

TEST_CASE("Tampered manifest is rejected", "[updater][signature]") {
    auto [manifest, signature] = SignedManifest{};

    SECTION("single byte changed") {
        manifest[manifest.size() / 2] ^= 0x01;
        CHECK_FALSE(verifies(manifest, signature));
    }

    SECTION("content appended") {
        manifest += " ";
        CHECK_FALSE(verifies(manifest, signature));
    }

    SECTION("content truncated") {
        manifest.pop_back();
        CHECK_FALSE(verifies(manifest, signature));
    }
}

TEST_CASE("Corrupt signatures are rejected", "[updater][signature]") {
    auto [manifest, signature] = SignedManifest{};

    SECTION("signature bytes flipped") {
        // Flip a character that stays valid base64 so the length check passes
        signature[10] = (signature[10] == 'A') ? 'B' : 'A';
        CHECK_FALSE(verifies(manifest, signature));
    }

    SECTION("not base64") {
        CHECK_FALSE(verifies(manifest, "!!!not-base64!!!"));
        CHECK_FALSE(SignatureVerifier::getLastError().empty());
    }

    SECTION("wrong length") {
        CHECK_FALSE(verifies(manifest, "AAAA"));
    }

    SECTION("empty signature") {
        CHECK_FALSE(verifies(manifest, ""));
    }
}

TEST_CASE("Signature from a different key is rejected", "[updater][signature]") {
    SignedManifest release;

    uint8_t wrongKey[64];
    memcpy(wrongKey, kTestPublicKey, 64);
    wrongKey[5] ^= 0xFF;  // Almost certainly not a valid curve point, and
                          // definitely not the signing key

    CHECK_FALSE(verifies(release.manifest, release.signature, wrongKey));
}

TEST_CASE("A release without a signature is refused", "[updater][signature]") {
    auto [manifest, signature] = SignedManifest{};
    std::string error;

    SECTION("missing .sig fails with a clear error") {
        CHECK_FALSE(ReleaseChecks::checkManifestSignature(manifest, nullptr, kTestPublicKey, error));
        CHECK(error.find("not signed") != std::string::npos);
    }

    SECTION("valid .sig passes") {
        CHECK(ReleaseChecks::checkManifestSignature(manifest, &signature, kTestPublicKey, error));
    }

    SECTION("bad .sig fails") {
        std::string empty;
        CHECK_FALSE(ReleaseChecks::checkManifestSignature(manifest, &empty, kTestPublicKey, error));
        CHECK(error.find("signature check failed") != std::string::npos);
    }
}

TEST_CASE("Manifest URL must be a release asset of the update repository", "[updater][url]") {
    const std::string owner = "Wobblucy";
    const std::string repo = "WoWLogreplayer-Public";

    SECTION("release assets of the repository are accepted") {
        CHECK(ReleaseChecks::isReleaseAssetUrl(
            "https://github.com/Wobblucy/WoWLogreplayer-Public/releases/download/v0.30.0/manifest.json",
            owner, repo));
        CHECK(ReleaseChecks::isReleaseAssetUrl(
            "https://github.com/wobblucy/woWLogreplayer-public/releases/download/v0.30.0/manifest.json",
            owner, repo));
    }

    SECTION("other hosts are rejected") {
        CHECK_FALSE(ReleaseChecks::isReleaseAssetUrl(
            "https://example.com/Wobblucy/WoWLogreplayer-Public/releases/download/v1/manifest.json",
            owner, repo));
        CHECK_FALSE(ReleaseChecks::isReleaseAssetUrl(
            "https://github.com.example.com/Wobblucy/WoWLogreplayer-Public/releases/download/v1/manifest.json",
            owner, repo));
        CHECK_FALSE(ReleaseChecks::isReleaseAssetUrl(
            "http://github.com/Wobblucy/WoWLogreplayer-Public/releases/download/v1/manifest.json",
            owner, repo));
    }

    SECTION("other repositories are rejected") {
        CHECK_FALSE(ReleaseChecks::isReleaseAssetUrl(
            "https://github.com/someone/WoWLogreplayer-Public/releases/download/v1/manifest.json",
            owner, repo));
        CHECK_FALSE(ReleaseChecks::isReleaseAssetUrl(
            "https://github.com/Wobblucy/WoWLogreplayer-Public-fork/releases/download/v1/manifest.json",
            owner, repo));
        CHECK_FALSE(ReleaseChecks::isReleaseAssetUrl(
            "https://github.com/Wobblucy/WoWLogreplayer-Public/archive/v1/manifest.json",
            owner, repo));
    }

    SECTION("paths that could resolve elsewhere are rejected") {
        CHECK_FALSE(ReleaseChecks::isReleaseAssetUrl(
            "https://github.com/Wobblucy/WoWLogreplayer-Public/releases/download/../../../../evil/repo/releases/download/v1/manifest.json",
            owner, repo));
        CHECK_FALSE(ReleaseChecks::isReleaseAssetUrl(
            "https://github.com/Wobblucy/WoWLogreplayer-Public/releases/download/%2e%2e/manifest.json",
            owner, repo));
        CHECK_FALSE(ReleaseChecks::isReleaseAssetUrl(
            "https://github.com/Wobblucy/WoWLogreplayer-Public/releases/download/v1\\manifest.json",
            owner, repo));
        CHECK_FALSE(ReleaseChecks::isReleaseAssetUrl(
            "https://github.com/Wobblucy/WoWLogreplayer-Public/releases/download/",
            owner, repo));
    }
}

TEST_CASE("Downgrade check uses the version it is given", "[updater][version]") {
    // The updater runs this against the unsigned tag name and again
    // against the signed manifest version, so each must refuse on its own.
    SECTION("older release is refused") {
        std::string error = ReleaseChecks::downgradeError("0.29.0", "0.30.0");
        CHECK_FALSE(error.empty());
        CHECK(error.find("--allow-downgrade") != std::string::npos);
    }

    SECTION("same or newer release is allowed") {
        CHECK(ReleaseChecks::downgradeError("0.30.0", "0.30.0").empty());
        CHECK(ReleaseChecks::downgradeError("0.31.0", "0.30.0").empty());
    }

    SECTION("unknown versions are not treated as a downgrade") {
        CHECK(ReleaseChecks::downgradeError("", "0.30.0").empty());
        CHECK(ReleaseChecks::downgradeError("0.29.0", "").empty());
    }
}

TEST_CASE("Optional pack paths must stay inside the install folder", "[updater][path]") {
    // downloadOptionalPack resolves pack.path the same way as every other
    // manifest path; these are the shapes a hostile manifest would use.
    const std::string appDir = std::filesystem::current_path().string();

    CHECK(PathValidator::validateAndCanonicalize(appDir, "../AwowLab.exe").empty());
    CHECK(PathValidator::validateAndCanonicalize(appDir, "data/spellnames/../../../evil.dll").empty());
    CHECK(PathValidator::validateAndCanonicalize(appDir, "..\\evil.dll").empty());
    CHECK(PathValidator::validateAndCanonicalize(appDir, "C:\\Windows\\evil.dll").empty());
    CHECK(PathValidator::validateAndCanonicalize(appDir, "\\\\server\\share\\evil.dll").empty());
    CHECK(PathValidator::validateAndCanonicalize(appDir, "").empty());

    CHECK_FALSE(PathValidator::validateAndCanonicalize(
        appDir, "data/spellnames/spell_names_ko_KR.csv.gz").empty());
}

TEST_CASE("Download URLs with a bad port are an error", "[updater][http]") {
    // Only parses the URL - nothing here opens a connection.
    HttpDownloader downloader;
    HttpDownloader::UrlParts parts;

    SECTION("port that is not a number") {
        CHECK_FALSE(downloader.parseUrl("https://github.com:abc/x", parts));
        CHECK_FALSE(downloader.getLastError().empty());
    }

    SECTION("port with trailing junk") {
        CHECK_FALSE(downloader.parseUrl("https://github.com:443abc/x", parts));
    }

    SECTION("port out of range") {
        CHECK_FALSE(downloader.parseUrl("https://github.com:70000/x", parts));
        CHECK_FALSE(downloader.parseUrl("https://github.com:99999999999/x", parts));
        CHECK_FALSE(downloader.parseUrl("https://github.com:0/x", parts));
    }

    SECTION("empty port") {
        CHECK_FALSE(downloader.parseUrl("https://github.com:/x", parts));
    }

    SECTION("valid URLs still parse") {
        REQUIRE(downloader.parseUrl("https://github.com:8443/a/b", parts));
        CHECK(parts.host == "github.com");
        CHECK(parts.port == 8443);
        CHECK(parts.path == "/a/b");

        REQUIRE(downloader.parseUrl("https://github.com/a", parts));
        CHECK(parts.port == 443);
    }
}

TEST_CASE("Damaged .archive_hashes file does not stop the updater", "[updater][local]") {
    SECTION("well-formed file") {
        auto hashes = ManifestParser::parseChunkHashes(
            R"({"group": "icons", "chunks": {"1": "aaa", "2": "bbb"}})");
        CHECK(hashes.size() == 2);
        CHECK(hashes[1] == "aaa");
        CHECK(hashes[2] == "bbb");
    }

    SECTION("part numbers that are not numbers are skipped") {
        std::unordered_map<int, std::string> hashes;
        CHECK_NOTHROW(hashes = ManifestParser::parseChunkHashes(
            R"({"chunks": {"abc": "x", "": "y", "3x": "z", "99999999999": "w", "4": "ok"}})"));
        CHECK(hashes.size() == 1);
        CHECK(hashes[4] == "ok");
    }

    SECTION("not JSON at all") {
        CHECK(ManifestParser::parseChunkHashes("garbage").empty());
        CHECK(ManifestParser::parseChunkHashes("").empty());
        CHECK(ManifestParser::parseChunkHashes(R"({"chunks": []})").empty());
    }
}

TEST_CASE("Version comparison orders releases correctly", "[updater][version]") {
    // Basic ordering
    CHECK(ManifestParser::compareVersions("0.17.0", "0.18.0") < 0);
    CHECK(ManifestParser::compareVersions("0.18.0", "0.17.0") > 0);
    CHECK(ManifestParser::compareVersions("0.18.0", "0.18.0") == 0);

    // Multi-digit components must compare numerically, not as strings
    CHECK(ManifestParser::compareVersions("0.9.0", "0.10.0") < 0);
    CHECK(ManifestParser::compareVersions("0.10.0", "0.9.0") > 0);

    // Patch level
    CHECK(ManifestParser::compareVersions("1.0.1", "1.0.2") < 0);

    // Major version dominates
    CHECK(ManifestParser::compareVersions("1.99.99", "2.0.0") < 0);
}

namespace {

// A fresh, empty folder under the temp directory, removed again on scope exit.
struct ScratchFolder {
    std::filesystem::path path;

    explicit ScratchFolder(const std::string& name) {
        path = std::filesystem::temp_directory_path() /
               ("awowlab-install-test-" + name + "-" + std::to_string(GetCurrentProcessId()));
        std::filesystem::remove_all(path);
        std::filesystem::create_directories(path);
    }

    ~ScratchFolder() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
};

} // namespace

TEST_CASE("Install folder picker refuses unsafe folders", "[updater][install]") {
    using Status = InstallFolder::Status;
    ScratchFolder downloads("downloads");
    const std::string downloadsPath = downloads.path.string();

    SECTION("the Downloads folder itself") {
        CHECK(InstallFolder::check(downloadsPath, downloadsPath) == Status::DownloadsFolder);
        CHECK(InstallFolder::check(downloadsPath + "\\", downloadsPath) == Status::DownloadsFolder);

        std::string otherCase = downloadsPath;
        std::transform(otherCase.begin(), otherCase.end(), otherCase.begin(),
                       [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
        CHECK(InstallFolder::check(otherCase, downloadsPath) == Status::DownloadsFolder);
    }

    SECTION("the user's real Downloads folder") {
        std::string real = InstallFolder::knownDownloadsFolder();
        if (!real.empty() && std::filesystem::is_directory(real)) {
            CHECK(InstallFolder::check(real, real) == Status::DownloadsFolder);
        }
    }

    SECTION("a folder inside Downloads is allowed") {
        std::filesystem::create_directories(downloads.path / "AwowLab");
        CHECK(InstallFolder::check((downloads.path / "AwowLab").string(), downloadsPath) ==
              Status::Usable);
    }

    SECTION("a drive root") {
        std::string systemRoot = downloads.path.root_path().string();
        CHECK(InstallFolder::check(systemRoot, downloadsPath) == Status::DriveRoot);
        CHECK(InstallFolder::check(downloads.path.root_name().string() + "/", downloadsPath) ==
              Status::DriveRoot);
    }

    SECTION("a folder that doesn't exist") {
        CHECK(InstallFolder::check((downloads.path / "missing").string(), downloadsPath) ==
              Status::Missing);
        CHECK(InstallFolder::check("", downloadsPath) == Status::Missing);
    }
}

TEST_CASE("Install folder picker accepts a normal folder", "[updater][install]") {
    ScratchFolder downloads("downloads");
    ScratchFolder target("target");

    CHECK(InstallFolder::isWritable(target.path.string()));
    CHECK(InstallFolder::check(target.path.string(), downloads.path.string()) ==
          InstallFolder::Status::Usable);

    // The writability probe must not leave anything behind.
    CHECK(std::filesystem::is_empty(target.path));
}

TEST_CASE("Install folder picker spots an existing install", "[updater][install]") {
    ScratchFolder downloads("downloads");
    ScratchFolder target("existing");

    CHECK_FALSE(InstallFolder::isExistingInstall(target.path.string()));

    std::ofstream(target.path / "AwowLab.exe") << "x";

    CHECK(InstallFolder::isExistingInstall(target.path.string()));
    CHECK(InstallFolder::check(target.path.string(), downloads.path.string()) ==
          InstallFolder::Status::ExistingInstall);
}

// The release key is generated from awowlab.config.json at configure time.
// These pin the conversion, and pin the official feed to the official key:
// a build that points at the official releases but checks them against some
// other key would refuse every update it is offered.
TEST_CASE("Generated release key matches the configured hex", "[updater][signature][config]") {
    constexpr std::string_view hex = AWOWLAB_SIGNING_PUBLIC_KEY;
    if (!kSigningKeyConfigured) {
        CHECK(hex.empty());
        SKIP("this build has no signing key");
    }
    REQUIRE(hex.size() == 128);

    auto nibble = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    for (size_t i = 0; i < 64; ++i) {
        int hi = nibble(hex[i * 2]);
        int lo = nibble(hex[i * 2 + 1]);
        REQUIRE(hi >= 0);
        REQUIRE(lo >= 0);
        CHECK(kReleasePublicKey[i] == static_cast<uint8_t>(hi * 16 + lo));
    }
}

TEST_CASE("The official feed is checked against the official key", "[updater][signature][config]") {
    if (std::string_view(AWOWLAB_UPDATE_REPO_OWNER) != "Wobblucy" ||
        std::string_view(AWOWLAB_UPDATE_REPO_NAME) != "WoWLogreplayer-Public") {
        SKIP("this build installs releases from another repository");
    }

    // The key UpdateSigningKey.h carried as literal bytes before the
    // config file existed
    static constexpr uint8_t official[64] = {
        0xFF, 0x1E, 0xEA, 0x5C, 0x40, 0xAC, 0xB7, 0xB3,
        0x6B, 0x48, 0x9D, 0xAC, 0x76, 0x9E, 0xF7, 0xBD,
        0x77, 0x88, 0x91, 0x73, 0xEC, 0xCA, 0xE8, 0xE3,
        0x54, 0x90, 0x55, 0xE6, 0xDE, 0x51, 0x07, 0xA7,
        0xE2, 0x06, 0x57, 0x36, 0x5E, 0x70, 0xFC, 0x90,
        0x13, 0x6E, 0x59, 0xA7, 0x59, 0xDC, 0x74, 0xCE,
        0x3A, 0xEC, 0x26, 0x78, 0xCA, 0xDF, 0xC0, 0xE4,
        0x20, 0x30, 0xF6, 0x5C, 0x74, 0x5A, 0x80, 0x1E,
    };

    REQUIRE(kSigningKeyConfigured);
    for (size_t i = 0; i < 64; ++i) {
        CHECK(kReleasePublicKey[i] == official[i]);
    }
}

namespace {

void writeText(const std::filesystem::path& path, const std::string& body) {
    std::ofstream out(path, std::ios::binary);
    out << body;
}

// Holds a file open the way another program might, sharing only what is
// asked for, until it goes out of scope.
struct HeldFile {
    HANDLE handle;
    HeldFile(const std::filesystem::path& path, DWORD share)
        : handle(CreateFileW(path.c_str(), GENERIC_READ, share, nullptr,
                             OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr)) {}
    ~HeldFile() { if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle); }
    HeldFile(const HeldFile&) = delete;
    HeldFile& operator=(const HeldFile&) = delete;
};

} // namespace

TEST_CASE("A file held open by another program fails the replace without throwing", "[updater][replace]") {
    const auto dir = std::filesystem::temp_directory_path() / "awowlab_replacer_test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    const auto target = dir / "data.bin";
    const std::string targetStr = target.string();
    const std::string tempStr = FileReplacer::getTempPath(targetStr);
    const std::string newBody = "new contents";
    const std::string newHash = HashVerifier::hashData(
        reinterpret_cast<const uint8_t*>(newBody.data()), newBody.size());
    REQUIRE_FALSE(newHash.empty());

    writeText(target, "old contents");
    writeText(tempStr, newBody);

    SECTION("target in use") {
        HeldFile held(target, FILE_SHARE_READ);
        REQUIRE(held.handle != INVALID_HANDLE_VALUE);
        bool ok = true;
        CHECK_NOTHROW(ok = FileReplacer::atomicReplace(targetStr, tempStr, newHash));
        CHECK_FALSE(ok);
        CHECK_FALSE(FileReplacer::getLastError().empty());
    }

    SECTION("download in use: the original is put back") {
        // Readable, so the hash check passes, but it can't be moved.
        HeldFile held(tempStr, FILE_SHARE_READ);
        REQUIRE(held.handle != INVALID_HANDLE_VALUE);
        bool ok = true;
        CHECK_NOTHROW(ok = FileReplacer::atomicReplace(targetStr, tempStr, newHash));
        CHECK_FALSE(ok);
        CHECK(readFile(targetStr) == "old contents");
    }

    SECTION("bad download that can't be deleted") {
        HeldFile held(tempStr, FILE_SHARE_READ);
        REQUIRE(held.handle != INVALID_HANDLE_VALUE);
        bool ok = true;
        CHECK_NOTHROW(ok = FileReplacer::atomicReplace(targetStr, tempStr, std::string(64, '0')));
        CHECK_FALSE(ok);
    }

    SECTION("rollback over a target in use") {
        writeText(FileReplacer::getBackupPath(targetStr), "backup");
        HeldFile held(target, FILE_SHARE_READ);
        REQUIRE(held.handle != INVALID_HANDLE_VALUE);
        bool ok = true;
        CHECK_NOTHROW(ok = FileReplacer::rollback(targetStr));
        CHECK_FALSE(ok);
    }

    SECTION("nothing in use still replaces") {
        CHECK(FileReplacer::atomicReplace(targetStr, tempStr, newHash));
        CHECK(readFile(targetStr) == newBody);
    }

    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}
