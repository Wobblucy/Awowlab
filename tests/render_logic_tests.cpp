// Logic from the replay view that runs without a GPU: playback timing,
// and whatever else in UIManager can be checked on plain data.

#include <catch2/catch_test_macros.hpp>
#include "Playback/PlaybackController.h"
#include "PlayAlong/PlayAlongKeybindSettings.h"
#include "Geometry/BakedModelPack.h"
#include "Animation/AngleMath.h"
#include "UI/Panels/IconTextOverlay.h"
#include "Rendering/FloorMode.h"
#include <catch2/catch_approx.hpp>
#include <imgui.h>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

// PlaybackController frame delta

TEST_CASE("playback advances by the frame delta", "[playback]") {
    PlaybackController playback;
    playback.loadTimeRange(0, 60000);
    playback.play();

    playback.update(0.016);
    REQUIRE(playback.getCurrentTime() == 16);
}

TEST_CASE("a stalled frame does not jump playback", "[playback]") {
    PlaybackController playback;
    playback.loadTimeRange(0, 60000);
    playback.play();

    // Coming back from a menu or a minimized window after 30 seconds
    playback.update(30.0);
    REQUIRE(playback.getCurrentTime() ==
            static_cast<int32_t>(PlaybackController::kMaxFrameDeltaSeconds * 1000.0));
}

TEST_CASE("a negative frame delta does not rewind playback", "[playback]") {
    PlaybackController playback;
    playback.loadTimeRange(0, 60000);
    playback.setCurrentTime(5000);
    playback.play();

    playback.update(-1.0);
    REQUIRE(playback.getCurrentTime() == 5000);
}

// Play Along keybind file loading

namespace {

// Writes a keybind file where load() looks for it, under a spec id no real
// spec uses, and removes it again afterwards
struct KeybindFile {
    static constexpr uint16_t kSpecId = 9999;
    std::filesystem::path path;

    explicit KeybindFile(const std::string& json) {
        auto dir = std::filesystem::current_path() / "User Settings" / "Playalong";
        std::filesystem::create_directories(dir);
        path = dir / ("unknown-" + std::to_string(kSpecId) + "-keybinds.json");
        std::ofstream out(path, std::ios::binary);
        out << json;
    }
    ~KeybindFile() {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }
};

std::string keyEntry(const std::string& spellId, int key) {
    return "\"" + spellId + "\": {\"key\": " + std::to_string(key) + "}";
}

}  // namespace

TEST_CASE("keybind load skips spell ids that are not numbers", "[playalong][keybinds]") {
    KeybindFile file("{\"keybinds\": {" +
                     keyEntry("12345", ImGuiKey_Q) + "," +
                     keyEntry("fireball", ImGuiKey_W) + "," +
                     keyEntry("678x", ImGuiKey_E) + "," +
                     keyEntry("", ImGuiKey_R) + "}}");

    PlayAlong::PlayAlongKeybindSettings settings;
    REQUIRE(settings.load(KeybindFile::kSpecId));

    REQUIRE(settings.getAllKeybinds().size() == 1);
    REQUIRE(settings.getKeybind(12345).imguiKey == ImGuiKey_Q);
}

TEST_CASE("keybind load drops keys ImGui does not know", "[playalong][keybinds]") {
    KeybindFile file("{\"keybinds\": {" +
                     keyEntry("1", ImGuiKey_A) + "," +
                     keyEntry("2", 7) + "," +
                     keyEntry("3", 99999) + "," +
                     keyEntry("4", -5) + "," +
                     keyEntry("5", ImGuiKey_NamedKey_END) + "}}");

    PlayAlong::PlayAlongKeybindSettings settings;
    REQUIRE(settings.load(KeybindFile::kSpecId));

    REQUIRE(settings.getAllKeybinds().size() == 1);
    REQUIRE(settings.hasKeybind(1));
}

// Baked model pack validation

namespace {

struct PackModel {
    std::string name;
    uint32_t vertexCount;
    std::vector<uint32_t> indices;
};

template <typename T>
void put(std::vector<uint8_t>& out, const T& value) {
    const auto* bytes = reinterpret_cast<const uint8_t*>(&value);
    out.insert(out.end(), bytes, bytes + sizeof(T));
}

// Builds a version 2 .awmp in the layout BakedModelPack.h describes
std::vector<uint8_t> buildPack(const std::vector<PackModel>& models) {
    size_t headerSize = 16;
    for (const auto& m : models) {
        headerSize += 2 + m.name.size() + 4 + 4 + 8 + 8;
    }
    size_t blobStart = (headerSize + 3) & ~size_t{3};

    std::vector<uint8_t> out;
    out.insert(out.end(), {'A', 'W', 'M', 'P'});
    put(out, uint32_t{2});
    put(out, static_cast<uint32_t>(models.size()));
    put(out, uint32_t{0});  // no assignments

    std::vector<uint8_t> blobs;
    for (const auto& m : models) {
        uint64_t vertOff = blobStart + blobs.size();
        blobs.resize(blobs.size() + m.vertexCount * sizeof(Vertex3D));
        uint64_t idxOff = blobStart + blobs.size();
        for (uint32_t index : m.indices) put(blobs, index);

        put(out, static_cast<uint16_t>(m.name.size()));
        out.insert(out.end(), m.name.begin(), m.name.end());
        put(out, m.vertexCount);
        put(out, static_cast<uint32_t>(m.indices.size()));
        put(out, vertOff);
        put(out, idxOff);
    }
    out.resize(blobStart, 0);
    out.insert(out.end(), blobs.begin(), blobs.end());
    return out;
}

std::string writePack(const std::string& fileName, const std::vector<uint8_t>& bytes) {
    auto path = std::filesystem::temp_directory_path() / fileName;
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return path.string();
}

}  // namespace

TEST_CASE("baked pack loads models whose indices fit", "[models][awmp]") {
    auto path = writePack("render_logic_ok.awmp", buildPack({{"tri", 3, {0, 1, 2}}}));

    BakedModelPack pack;
    REQUIRE(pack.load(path));
    REQUIRE(pack.find("tri") != nullptr);
    REQUIRE(pack.find("tri")->indexCount == 3);

    std::filesystem::remove(path);
}

TEST_CASE("baked pack rejects a model with an index past its vertices", "[models][awmp]") {
    auto path = writePack("render_logic_bad_index.awmp",
                          buildPack({{"good", 3, {0, 1, 2}}, {"bad", 3, {0, 1, 3}}}));

    BakedModelPack pack;
    REQUIRE(pack.load(path));
    REQUIRE(pack.find("good") != nullptr);
    REQUIRE(pack.find("bad") == nullptr);

    std::filesystem::remove(path);
}

TEST_CASE("a failed second pack keeps the first", "[models][awmp]") {
    auto goodPath = writePack("render_logic_first.awmp", buildPack({{"tri", 3, {0, 1, 2}}}));
    auto badPath = writePack("render_logic_second.awmp", buildPack({{"bad", 3, {0, 1, 9}}}));

    BakedModelPack pack;
    REQUIRE(pack.load(goodPath));
    REQUIRE_FALSE(pack.load(badPath));
    REQUIRE_FALSE(pack.load(goodPath + ".missing"));

    REQUIRE(pack.modelCount() == 1);
    REQUIRE(pack.find("tri") != nullptr);

    std::filesystem::remove(goodPath);
    std::filesystem::remove(badPath);
}

// Facing angle blending

namespace {

// Distance between two angles the short way round
float angleDistance(float a, float b) {
    float d = std::fabs(a - b);
    return d > angle_math::kPi ? angle_math::kTwoPi - d : d;
}

}  // namespace

TEST_CASE("averaging facings across the wrap stays near zero", "[angles]") {
    float mean = angle_math::addToAngleMean(0.0f, 6.2f, 1);
    mean = angle_math::addToAngleMean(mean, 0.1f, 2);

    // Midpoint of 6.2 and 0.1 the short way is about 0.0084, not 3.15
    REQUIRE(angleDistance(mean, 0.0084f) < 0.001f);
    REQUIRE(mean >= 0.0f);
    REQUIRE(mean < angle_math::kTwoPi);
}

TEST_CASE("averaging facings away from the wrap matches the plain mean", "[angles]") {
    float mean = angle_math::addToAngleMean(0.0f, 1.0f, 1);
    mean = angle_math::addToAngleMean(mean, 1.2f, 2);
    mean = angle_math::addToAngleMean(mean, 1.4f, 3);

    REQUIRE(mean == Catch::Approx(1.2f).margin(0.0001f));
}

TEST_CASE("interpolating facing takes the short way round", "[angles]") {
    REQUIRE(angleDistance(angle_math::lerpAngle(6.2f, 0.1f, 0.5f), 0.0084f) < 0.001f);
    REQUIRE(angle_math::lerpAngle(0.1f, 6.2f, 0.0f) == Catch::Approx(0.1f));
    REQUIRE(angle_math::lerpAngle(1.0f, 2.0f, 0.25f) == Catch::Approx(1.25f));
}

TEST_CASE("every keybind spec has a display name", "[playalong][keybinds]") {
    using PlayAlong::PlayAlongKeybindSettings;
    REQUIRE(PlayAlongKeybindSettings::specDisplayName(1480) == "Devourer Demon Hunter");
    REQUIRE(PlayAlongKeybindSettings::specDisplayName(102) == "Balance Druid");
    REQUIRE(PlayAlongKeybindSettings::specDisplayName(9999).empty());
}

// Aura icon countdowns

TEST_CASE("an aura with no end in the log shows no countdown", "[aura-icons]") {
    // AuraInstance marks a still-active aura with INT32_MAX
    REQUIRE(icon_overlay::auraRemainingMs(INT32_MAX, 5000) == 0);
}

TEST_CASE("aura countdown is the time until removal", "[aura-icons]") {
    REQUIRE(icon_overlay::auraRemainingMs(12000, 5000) == 7000);
    REQUIRE(icon_overlay::auraRemainingMs(5000, 5000) == 0);
    REQUIRE(icon_overlay::auraRemainingMs(4000, 5000) == 0);
}

TEST_CASE("countdown text switches format by remaining time", "[aura-icons]") {
    char text[16];
    icon_overlay::formatCountdown(text, sizeof(text), 65000);
    REQUIRE(std::string(text) == "1:05");
    icon_overlay::formatCountdown(text, sizeof(text), 12400);
    REQUIRE(std::string(text) == "12");
    icon_overlay::formatCountdown(text, sizeof(text), 4300);
    REQUIRE(std::string(text) == "4.3");
}

// Floor shader mode

TEST_CASE("an encounter texture draws as a texture without a world map", "[floor]") {
    REQUIRE(floorDrawsTexture(false, true, false));
}

TEST_CASE("the floor draws the grid only when there is nothing to sample", "[floor]") {
    REQUIRE(floorDrawsTexture(false, false, true));
    REQUIRE_FALSE(floorDrawsTexture(false, false, false));
    REQUIRE_FALSE(floorDrawsTexture(true, false, true));
}
