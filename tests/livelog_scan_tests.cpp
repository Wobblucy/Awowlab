// Segment scan tests: the chunked scanForSegments must find the same
// boundaries at the same absolute byte offsets as a whole-file read,
// including boundary events that straddle chunk edges.

#include <catch2/catch_test_macros.hpp>

#include "Core/Overlay/LiveLogSession.h"
#include "Core/Overlay/LiveCombatStats.h"
#include "Core/Overlay/ActiveLogWatcher.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <tuple>
#include <vector>

namespace fs = std::filesystem;

namespace {

// Build a synthetic combat log big enough to span several 8 MB scan
// chunks, with encounter boundaries at recorded byte offsets.
struct SyntheticLog {
    fs::path path;
    std::vector<size_t> encounterStartOffsets;
    std::vector<std::string> encounterNames;

    ~SyntheticLog() {
        std::error_code ec;
        fs::remove(path, ec);
    }
};

std::string fillerLine(int second, int index) {
    char buf[256];
    snprintf(buf, sizeof(buf),
             "7/29/2025 22:%02d:%02d.100-6  SPELL_DAMAGE,Player-11-0E960000,\"Someone-Realm-US\","
             "0x514,0x0,Creature-0-1-1-1-231075-0000000001,\"Chrome King Gallywix\",0xa48,0x0,"
             "466752,\"Filler Spell %d\",0x4,Player-11-0E960000,0000000000000000,"
             "1000,1000,100,100,100,0,3,0,100,100,0,-491.04,-691.87,2409,3.8887,684,"
             "12345,15000,-1,4,0,0,0,nil,nil,nil,nil",
             (second / 60) % 60, second % 60, index);
    return std::string(buf) + "\n";
}

SyntheticLog writeSyntheticLog(size_t targetBytes, int encounterCount) {
    SyntheticLog log;
    log.path = fs::temp_directory_path() / "awowlab_scan_test_log.txt";

    std::ofstream file(log.path, std::ios::binary | std::ios::trunc);
    REQUIRE(file.good());

    size_t written = 0;
    int second = 0;
    int fillerIndex = 0;
    size_t perEncounter = targetBytes / static_cast<size_t>(encounterCount);

    for (int enc = 0; enc < encounterCount; ++enc) {
        // Filler before the encounter
        while (written < perEncounter * static_cast<size_t>(enc) + perEncounter / 4) {
            std::string line = fillerLine(second++, fillerIndex++);
            file << line;
            written += line.size();
        }

        std::string name = "Test Boss " + std::to_string(enc + 1);
        log.encounterNames.push_back(name);
        log.encounterStartOffsets.push_back(written);

        char start[256];
        snprintf(start, sizeof(start),
                 "7/29/2025 22:%02d:%02d.200-6  ENCOUNTER_START,%d,\"%s\",16,20,2769\n",
                 (second / 60) % 60, second % 60, 3000 + enc, name.c_str());
        file << start;
        written += strlen(start);
        second += 2;

        // Filler inside the encounter
        while (written < perEncounter * static_cast<size_t>(enc) + (perEncounter * 3) / 4) {
            std::string line = fillerLine(second++, fillerIndex++);
            file << line;
            written += line.size();
        }

        char end[256];
        snprintf(end, sizeof(end),
                 "7/29/2025 22:%02d:%02d.300-6  ENCOUNTER_END,%d,\"%s\",16,20,%d,123456\n",
                 (second / 60) % 60, second % 60, 3000 + enc, name.c_str(),
                 enc % 2 == 0 ? 1 : 0);
        file << end;
        written += strlen(end);
        second += 2;
    }

    file.close();
    return log;
}

} // namespace

TEST_CASE("Chunked segment scan finds encounters across chunk boundaries", "[livelog][scan]") {
    // 20 MB spans three 8 MB scan chunks; 5 encounters guarantee at
    // least one boundary event lands away from a chunk edge and the
    // filler forces lines to straddle edges.
    constexpr size_t kLogSize = 20 * 1024 * 1024;
    constexpr int kEncounters = 5;
    SyntheticLog log = writeSyntheticLog(kLogSize, kEncounters);

    LiveLogSession session;
    REQUIRE(session.attach(log.path, /*scanExisting=*/false));
    session.scanForSegments();

    const auto& encounters = session.getEncounters();
    REQUIRE(encounters.size() == static_cast<size_t>(kEncounters));

    // Verify each recorded start offset points at the actual
    // ENCOUNTER_START line in the file
    std::ifstream file(log.path, std::ios::binary);
    REQUIRE(file.good());

    for (int i = 0; i < kEncounters; ++i) {
        CHECK(encounters[i].encounterName == log.encounterNames[i]);
        CHECK(encounters[i].startByteOffset == log.encounterStartOffsets[i]);
        CHECK(encounters[i].endByteOffset > encounters[i].startByteOffset);
        CHECK(encounters[i].isSuccess == (i % 2 == 0));

        file.seekg(static_cast<std::streamoff>(encounters[i].startByteOffset));
        std::string line;
        std::getline(file, line);
        INFO("encounter " << i << " offset " << encounters[i].startByteOffset);
        CHECK(line.find("ENCOUNTER_START") != std::string::npos);
        CHECK(line.find(log.encounterNames[i]) != std::string::npos);
    }

    // Raid encounters (no M+) land in pull history as boss pulls
    const auto& pulls = session.getPullHistory();
    REQUIRE(pulls.size() == static_cast<size_t>(kEncounters));
    for (int i = 0; i < kEncounters; ++i) {
        CHECK(pulls[i].isEncounter);
        CHECK(pulls[i].label == log.encounterNames[i]);
    }

    // poll() must resume from the end of everything scanned
    CHECK(session.getLastParsedOffset() == fs::file_size(log.path));
}

TEST_CASE("Scan handles a partial trailing line", "[livelog][scan]") {
    // An actively-written WoW log usually ends mid-line. The scan must
    // consume it without incident (this exact case used to throw
    // out_of_range from the chunk-refill bookkeeping).
    SyntheticLog log = writeSyntheticLog(1024 * 1024, 1);

    {
        std::ofstream file(log.path, std::ios::binary | std::ios::app);
        file << "7/29/2025 23:59:59.900-6  SPELL_DAMAGE,Player-11-0E960000,\"Trunc";
        // No newline - simulates WoW mid-write
    }

    LiveLogSession session;
    REQUIRE(session.attach(log.path, /*scanExisting=*/false));
    session.scanForSegments();

    REQUIRE(session.getEncounters().size() == 1);
    CHECK(session.getEncounters()[0].startByteOffset == log.encounterStartOffsets[0]);
    CHECK(session.getLastParsedOffset() == fs::file_size(log.path));
}

TEST_CASE("Live capture records first casts, emotes and specs", "[livelog][capture]") {
    // A short boss pull with a COMBATANT_INFO line, a boss SPELL_CAST_SUCCESS
    // (repeated - only the first should be kept), and an EMOTE. Poll() must
    // surface all three in the snapshot as phase-rule / class-color inputs.
    fs::path path = fs::temp_directory_path() / "awowlab_capture_test_log.txt";
    {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        file << "5/1/2025 20:00:00.000-6  ENCOUNTER_START,3011,\"Capture Boss\",16,20,2769\n";
        // Player COMBATANT_INFO: guid, faction, 22 stats, specID 268 (Brewmaster), talents
        file << "5/1/2025 20:00:00.100-6  COMBATANT_INFO,Player-1-DEADBEEF,0,"
                "100,100,100,100,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,268,[]\n";
        // Boss cast (hostile source flags 0xa48). Two of them - keep the first.
        file << "5/1/2025 20:00:05.000-6  SPELL_CAST_SUCCESS,Creature-0-1-1-1-99999-0001,"
                "\"Capture Boss\",0xa48,0x0,0000000000000000,nil,0x80000000,0x0,"
                "88888,\"Doom Bolt\",0x20,Creature-0-1-1-1-99999-0001,0000000000000000,"
                "100,100,0,0,0,0,3,0,0,0,0,-100.0,-100.0,2000,1.5,70\n";
        file << "5/1/2025 20:00:09.000-6  SPELL_CAST_SUCCESS,Creature-0-1-1-1-99999-0001,"
                "\"Capture Boss\",0xa48,0x0,0000000000000000,nil,0x80000000,0x0,"
                "88888,\"Doom Bolt\",0x20,Creature-0-1-1-1-99999-0001,0000000000000000,"
                "100,100,0,0,0,0,3,0,0,0,0,-100.0,-100.0,2000,1.5,70\n";
        // Boss emote
        file << "5/1/2025 20:00:07.000-6  EMOTE,Creature-0-1-1-1-99999-0001,\"Capture Boss\","
                "0000000000000000,nil,You cannot hide from the dark!\n";
        file << "5/1/2025 20:00:20.000-6  ENCOUNTER_END,3011,\"Capture Boss\",16,20,1,20000\n";
    }

    LiveLogSession session;
    REQUIRE(session.attach(path, /*scanExisting=*/false));
    session.poll();

    auto snap = session.getSnapshot();

    // Spec id captured for the player
    auto specIt = snap.guidToSpecId.find("Player-1-DEADBEEF");
    REQUIRE(specIt != snap.guidToSpecId.end());
    CHECK(specIt->second == 268);

    // First cast recorded once, flagged hostile, at the earlier time
    auto castIt = snap.firstCasts.find(88888);
    REQUIRE(castIt != snap.firstCasts.end());
    CHECK(castIt->second.hostile_source);
    CHECK(castIt->second.spell_name == "Doom Bolt");
    CHECK(castIt->second.time_ms == 5000);  // relative to ENCOUNTER_START

    // Emote captured with cleaned text
    REQUIRE(snap.emotes.size() == 1);
    CHECK(snap.emotes[0].text == "You cannot hide from the dark!");
    CHECK(snap.emotes[0].timestamp_ms == 7000);

    std::error_code ec;
    fs::remove(path, ec);
}

// LIVE TRASH COLLATION - the top "live" segment slides over the active
// combat segment, and inter-boss trash collates into a single segment.

namespace {

// Minimal helper for driving LiveLogSession the way WoW writes: append
// lines to the log, then poll() to parse whatever is now on disk. Because
// parsing is timestamp-ordered and poll() only reads bytes already
// written, appending an ENCOUNTER_START before its boss's damage lines
// reproduces the real "boss records don't flush until the kill" ordering.
struct LiveFeed {
    fs::path path;
    LiveLogSession session;

    explicit LiveFeed(const std::string& name) {
        path = fs::temp_directory_path() / name;
        std::ofstream(path, std::ios::binary | std::ios::trunc);  // truncate
        REQUIRE(session.attach(path, /*scanExisting=*/false));
    }
    ~LiveFeed() {
        std::error_code ec;
        fs::remove(path, ec);
    }

    void append(const std::string& line) {
        std::ofstream file(path, std::ios::binary | std::ios::app);
        file << line;
        if (line.empty() || line.back() != '\n') file << '\n';
    }
    bool poll() { return session.poll(); }
};

// A SPELL_DAMAGE line at "20:MM:SS" (dungeon-relative minutes/seconds).
// Full advanced-info block so the parser accepts it as combat data.
std::string damageAt(int minute, int second) {
    char buf[400];
    snprintf(buf, sizeof(buf),
             "5/1/2025 20:%02d:%02d.000-6  SPELL_DAMAGE,Player-11-0E960000,\"Someone-Realm-US\","
             "0x512,0x0,Creature-0-1-1-1-231075-0000000001,\"Trash Mob\",0x10a48,0x0,"
             "466752,\"Filler Spell\",0x4,Player-11-0E960000,0000000000000000,"
             "1000,1000,0,0,42857,0,0,0,3,89,100,0,-491.04,-691.87,2409,3.8887,83,"
             "10000,8000,-1,4,0,0,0,nil,nil,nil,ST",
             minute, second);
    return buf;
}

std::string encounterStartAt(int minute, int second, int id, const char* name) {
    char buf[160];
    snprintf(buf, sizeof(buf),
             "5/1/2025 20:%02d:%02d.000-6  ENCOUNTER_START,%d,\"%s\",16,20,2769",
             minute, second, id, name);
    return buf;
}

std::string encounterEndAt(int minute, int second, int id, const char* name, int success) {
    char buf[160];
    snprintf(buf, sizeof(buf),
             "5/1/2025 20:%02d:%02d.000-6  ENCOUNTER_END,%d,\"%s\",16,20,%d,120000",
             minute, second, id, name, success);
    return buf;
}

std::string challengeStartAt(int minute, int second, const char* zone, int keyLevel) {
    char buf[200];
    snprintf(buf, sizeof(buf),
             "5/1/2025 20:%02d:%02d.000-6  CHALLENGE_MODE_START,\"%s\",2290,375,%d,[9,122,4]",
             minute, second, zone, keyLevel);
    return buf;
}

std::string challengeEndAt(int minute, int second, int keyLevel) {
    char buf[160];
    snprintf(buf, sizeof(buf),
             "5/1/2025 20:%02d:%02d.000-6  CHALLENGE_MODE_END,2290,1,%d,1800000",
             minute, second, keyLevel);
    return buf;
}

// ms since 20:00:00 for the relative timestamps LiveLogSession produces
// (the first line seen anchors log-relative zero).
int32_t ms(int minute, int second) { return (minute * 60 + second) * 1000; }

// A SPELL_AURA_APPLIED (BUFF) line: player self-applies a buff. 15 tokens:
// base(11) + spell(3) + auraType(1).
std::string auraAppliedAt(int minute, int second, uint32_t spellId,
                          const char* spellName, const char* destGuid) {
    char buf[320];
    snprintf(buf, sizeof(buf),
             "5/1/2025 20:%02d:%02d.000-6  SPELL_AURA_APPLIED,%s,\"Someone-Realm-US\","
             "0x511,0x0,%s,\"Someone-Realm-US\",0x511,0x0,%u,\"%s\",0x1,BUFF",
             minute, second, destGuid, destGuid, spellId, spellName);
    return buf;
}

std::string auraRemovedAt(int minute, int second, uint32_t spellId,
                          const char* spellName, const char* destGuid) {
    char buf[320];
    snprintf(buf, sizeof(buf),
             "5/1/2025 20:%02d:%02d.000-6  SPELL_AURA_REMOVED,%s,\"Someone-Realm-US\","
             "0x511,0x0,%s,\"Someone-Realm-US\",0x511,0x0,%u,\"%s\",0x1,BUFF",
             minute, second, destGuid, destGuid, spellId, spellName);
    return buf;
}

} // namespace

TEST_CASE("Inter-boss trash collates into one segment across idle gaps", "[livelog][trash]") {
    LiveFeed feed("awowlab_trash_collate.txt");

    // Trash burst 1, a quiet gap well past the 5s idle timeout, then
    // trash burst 2 - all before any boss. Under the old idle-splitting
    // this produced two "Trash #" pulls; now it must stay one open pull.
    feed.append(damageAt(0, 0));
    feed.append(damageAt(0, 1));
    feed.poll();
    feed.append(damageAt(0, 30));   // >5s gap since last event
    feed.append(damageAt(0, 31));
    feed.poll();
    feed.append(damageAt(2, 0));    // another long idle gap
    feed.poll();

    auto snap = feed.session.getSnapshot();
    // No pull has closed yet: no boss boundary, no dungeon/session reset.
    CHECK(snap.pullHistory.empty());
    CHECK(snap.currentPull.segmentType == PullSegmentType::TrashPull);
    CHECK(snap.currentPull.startTime_ms == ms(0, 0));
    CHECK(snap.inCombat);
}

TEST_CASE("Pre-boss trash tail is attributed to trash, not the boss", "[livelog][trash]") {
    LiveFeed feed("awowlab_trash_boundary.txt");

    // Trash up to 1:44, boss pull ENCOUNTER_START at 1:45.
    feed.append(damageAt(0, 0));
    feed.append(damageAt(1, 30));
    feed.append(damageAt(1, 44));
    feed.poll();
    feed.append(encounterStartAt(1, 45, 3001, "Boss One"));
    feed.poll();

    auto snap = feed.session.getSnapshot();
    // The trash segment closed retroactively at the boss's start (1:45),
    // owning the whole pre-boss block including the 1:30-1:44 tail.
    REQUIRE(snap.pullHistory.size() == 1);
    const auto& trash = snap.pullHistory[0];
    CHECK(trash.segmentType == PullSegmentType::TrashPull);
    CHECK(trash.startTime_ms == ms(0, 0));
    CHECK(trash.endTime_ms == ms(1, 45));  // boss ENCOUNTER_START timestamp

    // The current pull is now the boss, owning from its ENCOUNTER_START.
    CHECK(snap.currentPull.isEncounter);
    CHECK(snap.currentPull.startTime_ms == ms(1, 45));
}

TEST_CASE("Live view slides over trash, session fallback, then boss window", "[livelog][trash]") {
    LiveFeed feed("awowlab_live_slide.txt");
    LiveCombatStats stats;
    stats.attachSession(&feed.session);

    auto liveRange = [&]() {
        // Mirror the overlay: refresh the databases from the session,
        // then read the CurrentPull window.
        stats.refresh();
        return stats.getTimeRange(StatsViewMode::CurrentPull);
    };

    // --- Between bosses: trash window [trashStart, now] ---
    feed.append(damageAt(0, 0));
    feed.append(damageAt(1, 30));
    feed.poll();
    {
        auto [start, end] = liveRange();
        CHECK(start == ms(0, 0));
        // In-progress trash pull keeps INT32_MAX end (render clamps).
        CHECK(end == INT32_MAX);
    }

    // --- Boss ENCOUNTER_START parsed, boss records NOT flushed yet ---
    // (the kill burst hasn't been written). Live view falls back to
    // session-so-far: the preceding trash [min, max].
    feed.append(encounterStartAt(1, 45, 3001, "Boss One"));
    feed.poll();
    {
        auto [start, end] = liveRange();
        // Session-so-far over the still-loaded trash: 0:00 .. 1:30.
        CHECK(start == ms(0, 0));
        CHECK(end == ms(1, 30));
    }

    // --- Boss data flushes (kill burst), then ENCOUNTER_END ---
    feed.append(damageAt(1, 45));
    feed.append(damageAt(3, 0));
    feed.append(damageAt(3, 15));
    feed.append(encounterEndAt(3, 15, 3001, "Boss One", 1));
    feed.poll();
    {
        auto [start, end] = liveRange();
        // Snapped to exactly the boss window - trash tail excluded by the
        // lower bound at the boss's ENCOUNTER_START.
        CHECK(start == ms(1, 45));
        CHECK(end == ms(3, 15));
    }
}

TEST_CASE("Boss owns exactly its ENCOUNTER window", "[livelog][trash]") {
    LiveFeed feed("awowlab_boss_window.txt");

    feed.append(damageAt(0, 0));
    feed.append(encounterStartAt(1, 45, 3001, "Boss One"));
    feed.append(damageAt(2, 0));
    feed.append(encounterEndAt(3, 15, 3001, "Boss One", 1));
    feed.poll();

    auto snap = feed.session.getSnapshot();
    // History holds the closed trash (0:00-1:45) and the closed boss
    // (1:45-3:15); currentPull_ still references the just-ended boss.
    REQUIRE(snap.pullHistory.size() == 2);
    CHECK(snap.pullHistory[0].segmentType == PullSegmentType::TrashPull);
    CHECK(snap.pullHistory[0].startTime_ms == ms(0, 0));
    CHECK(snap.pullHistory[0].endTime_ms == ms(1, 45));
    CHECK(snap.pullHistory[1].segmentType == PullSegmentType::BossPull);
    CHECK(snap.currentPull.isEncounter);
    CHECK(snap.currentPull.startTime_ms == ms(1, 45));
    CHECK(snap.currentPull.endTime_ms == ms(3, 15));
    CHECK(snap.currentPull.getDurationMs() == 90 * 1000);  // 1:45 -> 3:15
}

TEST_CASE("A completed M+ key gets an Overall segment live", "[livelog][mplus]") {
    LiveFeed feed("awowlab_mplus_overall.txt");

    // Key starts, one boss, some trash, key ends.
    feed.append(challengeStartAt(0, 0, "Test Dungeon", 15));
    feed.append(damageAt(0, 10));
    feed.append(encounterStartAt(1, 0, 4001, "Dungeon Boss"));
    feed.append(damageAt(1, 30));
    feed.append(encounterEndAt(2, 0, 4001, "Dungeon Boss", 1));
    feed.append(damageAt(2, 30));
    feed.append(challengeEndAt(3, 0, 15));
    feed.poll();

    auto snap = feed.session.getSnapshot();

    // The run must produce an "Overall" segment spanning the whole key,
    // the aggregate entry that was missing from the live path.
    const PullSegment* overall = nullptr;
    for (const auto& p : snap.pullHistory) {
        if (p.segmentType == PullSegmentType::DungeonOverall) overall = &p;
    }
    REQUIRE(overall != nullptr);
    CHECK(overall->startTime_ms == ms(0, 0));   // CHALLENGE_MODE_START
    CHECK(overall->endTime_ms == ms(3, 0));     // CHALLENGE_MODE_END
    CHECK(overall->inMythicPlus);
    CHECK(overall->keystoneLevel == 15);
}

namespace {

// Every damage record the session holds, flattened into comparable tuples.
std::vector<std::tuple<int32_t, uint32_t, uint64_t, uint32_t>>
damageRecords(LiveLogSession& session) {
    std::vector<std::tuple<int32_t, uint32_t, uint64_t, uint32_t>> out;
    auto lock = session.lockActorMap();
    for (const auto& [id, table] : session.getActorMap()) {
        for (const auto& r : table.damage_dealt_table) {
            out.emplace_back(r.timestamp_ms, r.spell_id, r.amount, r.target_guid_id);
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

} // namespace

TEST_CASE("Polling a log written mid-line matches one whole read", "[livelog][poll]") {
    // WoW flushes in arbitrary chunks, so a poll often ends inside a line.
    // That line must be read once, whole, on a later poll.
    std::string content;
    for (int s = 0; s < 8; ++s) content += damageAt(0, s) + "\n";

    LiveFeed whole("awowlab_poll_whole.txt");
    {
        std::ofstream file(whole.path, std::ios::binary | std::ios::app);
        file << content;
    }
    whole.poll();
    const auto expected = damageRecords(whole.session);
    REQUIRE(expected.size() == 8);

    const size_t lineLen = content.find('\n') + 1;
    const std::vector<size_t> cuts = {
        1, 200, lineLen - 1, lineLen, lineLen + 1, 2 * lineLen + 57,
        5 * lineLen - 3, content.size()
    };

    LiveFeed chunked("awowlab_poll_chunked.txt");
    size_t written = 0;
    for (size_t cut : cuts) {
        {
            std::ofstream file(chunked.path, std::ios::binary | std::ios::app);
            file << content.substr(written, cut - written);
        }
        written = cut;
        chunked.poll();
    }

    CHECK(damageRecords(chunked.session) == expected);
    CHECK(chunked.session.getLastParsedOffset() == content.size());
}

TEST_CASE("The event that opens a trash pull is kept", "[livelog][trash]") {
    // Opening a trash pull clears the previous pull's data. The event that
    // opened it must survive that clear, after a boss as well as at the
    // start of the log.
    LiveFeed feed("awowlab_trash_first_event.txt");
    feed.append(damageAt(0, 0));
    feed.append(damageAt(0, 1));
    feed.poll();
    CHECK(damageRecords(feed.session).size() == 2);

    feed.append(encounterStartAt(1, 0, 3001, "Boss One"));
    feed.append(damageAt(1, 10));
    feed.append(encounterEndAt(1, 30, 3001, "Boss One", 1));
    feed.append(damageAt(2, 0));
    feed.poll();
    const auto records = damageRecords(feed.session);
    REQUIRE(records.size() == 1);
    CHECK(std::get<0>(records[0]) == ms(2, 0));
}

TEST_CASE("Live pull byte offsets are real file positions", "[livelog][poll]") {
    // Quoted names, the double space after the time and CRLF endings are
    // all bytes the tokens do not carry; the offsets must still land on
    // the line starts, or a re-parse of the pull reads the wrong window.
    const std::string trash = damageAt(0, 5) + "\r\n";
    const std::string start = encounterStartAt(1, 0, 3011, "Offset Boss") + "\r\n";
    const std::string boss = damageAt(1, 10) + "\r\n";
    const std::string end = encounterEndAt(2, 0, 3011, "Offset Boss", 1) + "\r\n";

    LiveFeed feed("awowlab_poll_offsets.txt");
    std::string content;
    for (int i = 0; i < 20; ++i) content += trash;
    const size_t startOffset = content.size();
    content += start;
    for (int i = 0; i < 20; ++i) content += boss;
    const size_t endOffset = content.size();
    content += end;
    {
        std::ofstream file(feed.path, std::ios::binary | std::ios::app);
        file << content;
    }
    feed.poll();

    REQUIRE(feed.session.getEncounters().size() == 1);
    CHECK(feed.session.getEncounters()[0].startByteOffset == startOffset);
    CHECK(feed.session.getEncounters()[0].endByteOffset == endOffset);

    const auto snap = feed.session.getSnapshot();
    REQUIRE(snap.pullHistory.size() == 2);
    CHECK(snap.pullHistory[0].startByteOffset == 0);
    CHECK(snap.pullHistory[0].endByteOffset == startOffset);
    CHECK(snap.pullHistory[1].startByteOffset == startOffset);
    CHECK(snap.pullHistory[1].endByteOffset == endOffset);
}

// TWO-MAP: viewing history must not pause live parsing. The live bundle keeps
// filling while a past segment is displayed; returning to live shows the
// up-to-date data and no pull is ever dropped.

TEST_CASE("Live parse keeps running while a historical segment is viewed", "[livelog][history]") {
    LiveFeed feed("awowlab_history_live_parse.txt");

    // A first boss pull, killed, so it lands in history with byte offsets
    // we can re-parse.
    feed.append(encounterStartAt(1, 0, 5001, "Boss One"));
    feed.append(damageAt(1, 10));
    feed.append(encounterEndAt(1, 30, 5001, "Boss One", 1));
    feed.poll();

    auto snap = feed.session.getSnapshot();
    REQUIRE(snap.pullHistory.size() == 1);
    const size_t offsetAfterBoss = feed.session.getLastParsedOffset();

    // Open the historical boss segment for display.
    REQUIRE(feed.session.parseSegment(snap.pullHistory[0]));
    CHECK(feed.session.getViewState() == SessionViewState::Historical);

    // While that history view is up, a NEW pull happens (trash). Feed it
    // and poll: the live parse must advance even though we're viewing
    // history.
    feed.append(damageAt(2, 0));
    feed.append(damageAt(2, 5));
    CHECK(feed.poll());  // poll() no longer early-returns while historical

    // Live offset advanced past where the boss ended: history did not
    // pause the live reader.
    CHECK(feed.session.getLastParsedOffset() > offsetAfterBoss);

    // Selecting history left the live current-pull tracking alone: the new
    // trash opened a live pull (session still in combat), and pullHistory
    // was not disturbed by the historical re-parse.
    auto histSnap = feed.session.getSnapshot();
    CHECK(histSnap.pullHistory.size() == 1);           // unchanged by the view
    CHECK(histSnap.currentPull.segmentType == PullSegmentType::TrashPull);
    // The log's first line was the boss ENCOUNTER_START at 1:00, so
    // log-relative zero sits there; the 2:00 trash damage lands at 1:00
    // relative. What matters is the live tracking advanced to a fresh
    // trash pull while history was on screen.
    CHECK(histSnap.currentPull.startTime_ms == ms(1, 0));

    // Return to live: no skip-to-EOF, the new trash is present and shown.
    feed.session.returnToLiveMode();
    CHECK(feed.session.getViewState() == SessionViewState::Live);

    // The live bundle carries the trash damage that arrived during the
    // history view - nothing dropped.
    LiveCombatStats stats;
    stats.attachSession(&feed.session);
    stats.refresh();
    auto damage = stats.getDamageRanked(StatsViewMode::SessionTotal, 20);
    CHECK_FALSE(damage.empty());
}

TEST_CASE("Returning to live does not skip data written during the view", "[livelog][history]") {
    LiveFeed feed("awowlab_history_no_skip.txt");

    feed.append(encounterStartAt(1, 0, 5002, "Boss Two"));
    feed.append(damageAt(1, 10));
    feed.append(encounterEndAt(1, 30, 5002, "Boss Two", 1));
    feed.poll();

    auto snap = feed.session.getSnapshot();
    REQUIRE(snap.pullHistory.size() == 1);

    // Enter history.
    REQUIRE(feed.session.parseSegment(snap.pullHistory[0]));
    const size_t offsetBeforeGrowth = feed.session.getLastParsedOffset();

    // Write more of the log while history is displayed but do NOT poll yet
    // (mirrors data landing on disk mid-view). The old returnToLiveMode
    // jumped lastParsedOffset_ to EOF here, which is exactly what dropped
    // the pull.
    feed.append(damageAt(2, 0));

    feed.session.returnToLiveMode();

    // returnToLiveMode must leave the offset untouched (it was already
    // current from the live poll), so the freshly written bytes are still
    // ahead of it and the next poll picks them up.
    CHECK(feed.session.getLastParsedOffset() == offsetBeforeGrowth);
    CHECK(feed.poll());  // the 2:00 damage is parsed, not skipped
    CHECK(feed.session.getLastParsedOffset() == fs::file_size(feed.path));
}

TEST_CASE("A replayed segment does not feed the live phase inputs", "[livelog][history]") {
    // The replay runs on its own thread next to poll(). It keeps its names
    // to itself until it is published, and never adds to the first casts
    // or emotes the live phase rules read.
    const std::string bossCast =
        "5/1/2025 20:01:05.000-6  SPELL_CAST_SUCCESS,Creature-0-1-1-1-99999-0001,"
        "\"Replay Boss\",0xa48,0x0,0000000000000000,nil,0x80000000,0x0,"
        "88888,\"Doom Bolt\",0x20,Creature-0-1-1-1-99999-0001,0000000000000000,"
        "100,100,0,0,0,0,3,0,0,0,0,-100.0,-100.0,2000,1.5,70";
    const std::string bossEmote =
        "5/1/2025 20:01:07.000-6  EMOTE,Creature-0-1-1-1-99999-0001,\"Replay Boss\","
        "0000000000000000,nil,You cannot hide from the dark!";

    LiveFeed feed("awowlab_history_phase_inputs.txt");
    feed.append(encounterStartAt(1, 0, 5003, "Replay Boss"));
    feed.append(bossCast);
    feed.append(bossEmote);
    feed.append(damageAt(1, 10));
    feed.append(encounterEndAt(1, 30, 5003, "Replay Boss", 1));
    feed.append(damageAt(2, 0));  // trash opens a new pull and clears them
    feed.poll();

    auto snap = feed.session.getSnapshot();
    REQUIRE(snap.pullHistory.size() == 1);
    REQUIRE(snap.firstCasts.empty());
    REQUIRE(snap.emotes.empty());

    REQUIRE(feed.session.parseSegment(snap.pullHistory[0]));
    auto after = feed.session.getSnapshot();
    CHECK(after.firstCasts.empty());
    CHECK(after.emotes.empty());

    // A session that only scanned the file learns names from the replay.
    LiveLogSession scanned;
    REQUIRE(scanned.attach(feed.path, /*scanExisting=*/true));
    REQUIRE_FALSE(scanned.getPullHistory().empty());
    CHECK(scanned.getSnapshot().guidToName.empty());
    REQUIRE(scanned.parseSegment(scanned.getPullHistory()[0]));
    auto names = scanned.getSnapshot().guidToName;
    auto it = names.find("Creature-0-1-1-1-99999-0001");
    REQUIRE(it != names.end());
    CHECK(it->second == "Replay Boss");
}

TEST_CASE("Historical view leaves live current-pull and history untouched", "[livelog][history]") {
    LiveFeed feed("awowlab_history_isolation.txt");

    // Two closed boss pulls in history, plus an in-progress trash pull.
    feed.append(encounterStartAt(0, 30, 6001, "Boss A"));
    feed.append(damageAt(0, 40));
    feed.append(encounterEndAt(1, 0, 6001, "Boss A", 1));
    feed.append(encounterStartAt(1, 30, 6002, "Boss B"));
    feed.append(damageAt(1, 40));
    feed.append(encounterEndAt(2, 0, 6002, "Boss B", 1));
    feed.append(damageAt(2, 30));  // opens a live trash pull
    feed.poll();

    auto before = feed.session.getSnapshot();
    const size_t historyCountBefore = before.pullHistory.size();
    const auto currentPullBefore = before.currentPull;
    const size_t offsetBefore = feed.session.getLastParsedOffset();

    // Select the first boss for display.
    REQUIRE(feed.session.parseSegment(before.pullHistory[0]));

    auto after = feed.session.getSnapshot();
    // History count, live current-pull identity and the parse offset are
    // all exactly as they were - the historical re-parse touched none of
    // the live tracking state.
    CHECK(after.pullHistory.size() == historyCountBefore);
    CHECK(after.currentPull.segmentType == currentPullBefore.segmentType);
    CHECK(after.currentPull.startTime_ms == currentPullBefore.startTime_ms);
    CHECK(feed.session.getLastParsedOffset() == offsetBefore);
}

TEST_CASE("Pull duration survives the midnight rollover", "[livelog][scan]") {
    // A pull running from 23:58 to 00:07 the next day must come out as
    // nine minutes, not negative. Time-of-day-only timestamps made the
    // end land "before" the start and every such pull showed "..." as
    // its duration.
    fs::path path = fs::temp_directory_path() / "awowlab_midnight_test_log.txt";
    {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        file << "7/29/2025 23:50:00.000-6  SPELL_DAMAGE,Player-11-0E960000,\"Someone\",0x514,0x0,"
                "Creature-0-1-1-1-231075-0000000001,\"Boss\",0xa48,0x0,466752,\"Spell\",0x4,"
                "Player-11-0E960000,0000000000000000,1000,1000,100,100,100,0,3,0,100,100,0,"
                "-491.04,-691.87,2409,3.8887,684,12345,15000,-1,4,0,0,0,nil,nil,nil,nil\n";
        file << "7/29/2025 23:58:00.000-6  ENCOUNTER_START,3009,\"Midnight Boss\",16,20,2769\n";
        file << "7/30/2025 0:03:00.000-6  SPELL_DAMAGE,Player-11-0E960000,\"Someone\",0x514,0x0,"
                "Creature-0-1-1-1-231075-0000000001,\"Boss\",0xa48,0x0,466752,\"Spell\",0x4,"
                "Player-11-0E960000,0000000000000000,1000,1000,100,100,100,0,3,0,100,100,0,"
                "-491.04,-691.87,2409,3.8887,684,12345,15000,-1,4,0,0,0,nil,nil,nil,nil\n";
        file << "7/30/2025 0:07:00.000-6  ENCOUNTER_END,3009,\"Midnight Boss\",16,20,1,540000\n";
    }

    LiveLogSession session;
    REQUIRE(session.attach(path, /*scanExisting=*/false));
    session.scanForSegments();

    const auto& pulls = session.getPullHistory();
    REQUIRE(pulls.size() == 1);
    // 23:58:00 -> 0:07:00 = 9 minutes. The scan anchors log start on
    // the first boundary event (filler lines are fast-rejected without
    // timestamp parsing), so the pull starts at relative zero.
    CHECK(pulls[0].getDurationMs() == 9 * 60 * 1000);
    CHECK(pulls[0].getDurationString() == "9:00");
    CHECK(pulls[0].startTime_ms == 0);
    CHECK(pulls[0].endTime_ms == 9 * 60 * 1000);

    std::error_code ec;
    fs::remove(path, ec);
}

// M+ RUN GROUPING - two runs of the same dungeon both survive in history,
// carry distinct run ids, and get group-header metadata backfilled.

TEST_CASE("Two M+ runs of the same dungeon both persist and group", "[livelog][grouping]") {
    LiveFeed feed("awowlab_mplus_grouping.txt");

    // --- Run 1: +10 Algethar, one boss, then finish the key ---
    feed.append(challengeStartAt(0, 0, "Algethar Academy", 10));
    feed.append(damageAt(0, 5));                              // trash
    feed.poll();
    feed.append(encounterStartAt(1, 0, 3001, "Echo of Doragosa"));
    feed.append(encounterEndAt(3, 0, 3001, "Echo of Doragosa", 1));
    feed.poll();
    feed.append(challengeEndAt(4, 0, 10));
    feed.poll();

    // --- Run 2: +12 Algethar, wipes on the first boss, key abandoned ---
    feed.append(challengeStartAt(10, 0, "Algethar Academy", 12));
    feed.append(damageAt(10, 5));                             // trash
    feed.poll();
    feed.append(encounterStartAt(11, 0, 3001, "Echo of Doragosa"));
    feed.append(encounterEndAt(12, 30, 3001, "Echo of Doragosa", 0));  // wipe
    feed.poll();
    feed.append(challengeEndAt(13, 0, 12));
    feed.poll();

    const auto& pulls = feed.session.getPullHistory();

    // Both runs still present - the second CHALLENGE_MODE_START must NOT
    // have wiped run 1 (the bug this fixes).
    std::set<uint32_t> runIds;
    int overalls = 0;
    for (const auto& p : pulls) {
        if (p.inMythicPlus) runIds.insert(p.dungeonRunId);
        if (p.segmentType == PullSegmentType::DungeonOverall) ++overalls;
    }
    REQUIRE(runIds.size() == 2);          // two distinct runs
    CHECK(overalls == 2);                  // one Overall per run

    // Every M+ segment carries its group header info and a backfilled
    // run end time (both runs are finished).
    for (const auto& p : pulls) {
        if (!p.inMythicPlus) continue;
        CHECK(p.dungeonName == "Algethar Academy");
        CHECK(p.dungeonEndTime_ms > p.dungeonStartTime_ms);
    }

    // The two runs carry their respective keystone levels.
    std::set<uint32_t> keys;
    for (const auto& p : pulls) if (p.inMythicPlus) keys.insert(p.keystoneLevel);
    CHECK(keys.count(10) == 1);
    CHECK(keys.count(12) == 1);
}

TEST_CASE("Abandoned keys (no combat) still show as groups", "[livelog][grouping]") {
    // WoW writes no combat for a key that's started then abandoned, so
    // back-to-back CHALLENGE_MODE_STARTs produce runs with zero segments.
    // Each should still leave a placeholder so it groups in the selector.
    LiveFeed feed("awowlab_empty_runs.txt");

    // A real run with a boss...
    feed.append(challengeStartAt(0, 0, "Algethar Academy", 10));
    feed.append(damageAt(0, 5));
    feed.poll();
    feed.append(encounterStartAt(1, 0, 3001, "Echo of Doragosa"));
    feed.append(encounterEndAt(3, 0, 3001, "Echo of Doragosa", 1));
    feed.poll();
    // ...then three instant restarts with no combat between them...
    feed.append(challengeStartAt(5, 0, "Algethar Academy", 10));
    feed.append(challengeStartAt(5, 30, "Algethar Academy", 10));
    feed.append(challengeStartAt(6, 0, "Algethar Academy", 10));
    feed.poll();
    // ...and a final real run so the last empty one is flushed at the next start.
    feed.append(damageAt(6, 30));
    feed.append(encounterStartAt(7, 0, 3001, "Echo of Doragosa"));
    feed.append(encounterEndAt(9, 0, 3001, "Echo of Doragosa", 1));
    feed.poll();

    const auto& pulls = feed.session.getPullHistory();

    std::set<uint32_t> runIds;
    int emptyRuns = 0;
    for (const auto& p : pulls) {
        if (p.inMythicPlus) runIds.insert(p.dungeonRunId);
        if (p.segmentType == PullSegmentType::EmptyRun) ++emptyRuns;
    }

    // Four key-starts: run 1 (boss) -> restart -> restart -> run 4 (boss).
    // The two middle restarts have no combat and become placeholders; the
    // first and last runs pull a boss. So four distinct runs, two empty.
    CHECK(runIds.size() == 4);
    CHECK(emptyRuns == 2);

    // Every empty placeholder still carries its group header so it renders
    // as a collapsible run, not an orphan row.
    for (const auto& p : pulls) {
        if (p.segmentType != PullSegmentType::EmptyRun) continue;
        CHECK(p.inMythicPlus);
        CHECK(p.dungeonName == "Algethar Academy");
    }

    // Every closed M+ run - with combat or empty - carries a run duration
    // (start-of-key to end-of-run), so all group headers show a time. Runs
    // 1-3 are closed by later starts; the last run (4) is still open here.
    for (const auto& p : pulls) {
        if (!p.inMythicPlus) continue;
        if (p.dungeonRunId == 4) continue;  // still-open final run
        CHECK(p.dungeonEndTime_ms > p.dungeonStartTime_ms);
    }
}

// DEFENSIVE TRACKING - buff applies are captured ONLY for the tracked spell
// ids, and the AuraDatabase then answers "active at death" the recap uses.

TEST_CASE("Tracked defensive auras are captured; untracked ones ignored", "[livelog][defensives]") {
    LiveFeed feed("awowlab_defensives.txt");
    const char* player = "Player-1-DEADBEEF";

    // Track spell 61336 (Survival Instincts). 22812 (Barkskin) is NOT tracked.
    feed.session.setTrackedDefensiveIds({61336});

    feed.append(damageAt(0, 0));                                   // anchor log start
    feed.append(auraAppliedAt(0, 5, 61336, "Survival Instincts", player));  // tracked
    feed.append(auraAppliedAt(0, 6, 22812, "Barkskin", player));            // untracked
    feed.append(auraRemovedAt(0, 9, 61336, "Survival Instincts", player));  // tracked
    feed.poll();

    const auto& auras = feed.session.getAuraEvents();
    // Only the two tracked-spell events (apply + remove) are stored.
    int tracked = 0, untracked = 0;
    for (const auto& a : auras) {
        if (a.spell_id == 61336) ++tracked;
        if (a.spell_id == 22812) ++untracked;
    }
    CHECK(tracked == 2);
    CHECK(untracked == 0);
}

TEST_CASE("AuraDatabase reports a tracked defensive active at death", "[livelog][defensives]") {
    LiveFeed feed("awowlab_defensives_active.txt");
    const char* player = "Player-1-DEADBEEF";
    feed.session.setTrackedDefensiveIds({61336});

    feed.append(damageAt(0, 0));
    feed.append(auraAppliedAt(0, 5, 61336, "Survival Instincts", player)); // pressed at 5s
    // No removal: still active when they die at 8s.
    feed.poll();

    // Build the AuraDatabase the way LiveCombatStats does and query it.
    AuraDatabase auraDb;
    auto copy = feed.session.getAuraEvents();
    auraDb.loadFromEvents(std::move(copy));

    auto instances = auraDb.getAllAurasOnTarget(player);
    bool activeAtDeath = false, pressedInWindow = false;
    int32_t deathT = ms(0, 8);
    for (const auto* inst : instances) {
        if (inst->spell_id != 61336) continue;
        if (inst->isActiveAt(deathT)) activeAtDeath = true;
        if (inst->applied_at_ms >= deathT - 10000 && inst->applied_at_ms <= deathT)
            pressedInWindow = true;
    }
    CHECK(activeAtDeath);
    CHECK(pressedInWindow);
}

TEST_CASE("Deleting or renaming away an old log does not make it active", "[livelog][watcher]") {
    const fs::path dir = fs::temp_directory_path() / "awowlab_watcher_test";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);
    const fs::path active = dir / "WoWCombatLog-050125_200000.txt";
    const fs::path old = dir / "WoWCombatLog-040125_200000.txt";
    const fs::path older = dir / "WoWCombatLog-030125_200000.txt";
    std::ofstream(old, std::ios::binary) << "old\n";
    std::ofstream(older, std::ios::binary) << "older\n";
    std::ofstream(active, std::ios::binary) << "active\n";

    std::mutex seenMutex;
    std::vector<fs::path> seen;
    {
        ActiveLogWatcher watcher;
        watcher.setWriteCallback([&](const fs::path& p) {
            std::lock_guard<std::mutex> lock(seenMutex);
            seen.push_back(p);
        });
        REQUIRE(watcher.start(dir));

        // Wait until a write to the active log has been seen `count` times.
        auto waitForWrites = [&](size_t count) {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            while (std::chrono::steady_clock::now() < deadline) {
                {
                    std::lock_guard<std::mutex> lock(seenMutex);
                    if (static_cast<size_t>(std::count(seen.begin(), seen.end(), active)) >= count) {
                        return true;
                    }
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            return false;
        };

        // The watch arms on its own thread; keep writing until one is seen,
        // so it is live before anything else changes.
        bool armed = false;
        for (int attempt = 0; attempt < 50 && !armed; ++attempt) {
            std::ofstream(active, std::ios::binary | std::ios::app) << "more\n";
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            std::lock_guard<std::mutex> lock(seenMutex);
            armed = std::find(seen.begin(), seen.end(), active) != seen.end();
        }
        REQUIRE(armed);
        const size_t writesBefore = [&] {
            std::lock_guard<std::mutex> lock(seenMutex);
            return static_cast<size_t>(std::count(seen.begin(), seen.end(), active));
        }();

        fs::remove(old);
        fs::rename(older, dir / "renamed.txt");
        // Changes arrive in order, so once this write is seen the delete
        // and the rename have been handled too.
        std::ofstream(active, std::ios::binary | std::ios::app) << "more\n";
        REQUIRE(waitForWrites(writesBefore + 1));
    }

    CHECK(std::find(seen.begin(), seen.end(), active) != seen.end());
    CHECK(std::find(seen.begin(), seen.end(), old) == seen.end());
    CHECK(std::find(seen.begin(), seen.end(), older) == seen.end());
    fs::remove_all(dir, ec);
}
