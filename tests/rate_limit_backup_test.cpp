// The rate limit backups are read back from files any user can edit, so a
// mangled value has to be ignored rather than crash the app at startup.

#include <catch2/catch_test_macros.hpp>

#include "Settings/RateLimitBackup.h"
#include "ExecutablePath.h"

#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>

namespace {

// Writes map_world_bounds.csv next to the test binary for one test and puts
// back whatever was there before.
class ScopedBoundsCsv {
public:
    explicit ScopedBoundsCsv(const std::string& content) {
        path_ = std::filesystem::path(awow::getExecutableDirectory()) / "PNG" / "Worldmaps" / "map_world_bounds.csv";
        std::filesystem::create_directories(path_.parent_path());
        if (std::filesystem::exists(path_)) {
            std::ifstream in(path_, std::ios::binary);
            std::stringstream buffer;
            buffer << in.rdbuf();
            previous_ = buffer.str();
        }
        std::ofstream out(path_, std::ios::binary | std::ios::trunc);
        out << content;
    }

    ~ScopedBoundsCsv() {
        std::error_code ec;
        if (previous_) {
            std::ofstream out(path_, std::ios::binary | std::ios::trunc);
            out << *previous_;
        } else {
            std::filesystem::remove(path_, ec);
        }
    }

    ScopedBoundsCsv(const ScopedBoundsCsv&) = delete;
    ScopedBoundsCsv& operator=(const ScopedBoundsCsv&) = delete;

private:
    std::filesystem::path path_;
    std::optional<std::string> previous_;
};

} // namespace

TEST_CASE("Malformed hidden CSV row is ignored", "[settings][ratelimit]") {
    RateLimitBackup::RateLimitData data;

    SECTION("value that is not a number") {
        ScopedBoundsCsv csv("2552,1.0,2.0,3.0,4.0,1024,768\n99999,abc.0,1.0,2.0,3.0,1024,768\n");
        CHECK_NOTHROW(RateLimitBackup::loadFromCsvHiddenRow(data));
        CHECK_FALSE(RateLimitBackup::loadFromCsvHiddenRow(data));
    }

    SECTION("value out of range") {
        ScopedBoundsCsv csv("99999,1e99,1.0,2.0,3.0,1024,768\n");
        CHECK_NOTHROW(RateLimitBackup::loadFromCsvHiddenRow(data));
        CHECK_FALSE(RateLimitBackup::loadFromCsvHiddenRow(data));
    }

    SECTION("empty fields") {
        ScopedBoundsCsv csv("99999,,,,,1024,768\n");
        CHECK_NOTHROW(RateLimitBackup::loadFromCsvHiddenRow(data));
        CHECK_FALSE(RateLimitBackup::loadFromCsvHiddenRow(data));
    }
}
