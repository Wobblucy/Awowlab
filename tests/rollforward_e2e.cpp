// End-to-end check that a real combat log still parses against the current
// spell data, and that the spell table answers for the abilities that log
// actually contains.
//
// The unit tests run on small fixtures, so they never exercise the shipped
// spell_data_{version}.bin. A DB2 column shift produces plausible-but-wrong
// cooldowns rather than an error, so the useful question is not "does it parse"
// but "do the numbers look like a real game".
//
// Point AWOWLAB_E2E_LOG at a combat log to run it:
//   set AWOWLAB_E2E_LOG=C:\...\WoWCombatLog-071826_165415.txt
//   rollforward_e2e.exe

#include <catch2/catch_all.hpp>

#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "../AwowLab/Database/OutputWriter.h"
#include "../AwowLab/Database/SpellDataTable.h"
#include "../AwowLab/Txttokenizer/include/Txttokenizer/Txttokenizer.h"
#include "../AwowLab/Parser/Include/Actor/ActorMap.h"
#include "../AwowLab/Parser/Include/Actor/ActorTable.h"

namespace {

std::string envLogPath() {
    const char* p = std::getenv("AWOWLAB_E2E_LOG");
    return p ? std::string(p) : std::string();
}

std::string envDataDir() {
    const char* p = std::getenv("AWOWLAB_E2E_DATA");
    return p ? std::string(p) : std::string("data");
}

// Read a bounded prefix; these logs run to hundreds of megabytes and the parse
// path is the same for the first chunk as the last.
std::string readPrefix(const std::string& path, size_t maxBytes) {
    std::ifstream in(path, std::ios::binary);
    if (!in.good()) return {};
    std::string buf;
    buf.resize(maxBytes);
    in.read(buf.data(), static_cast<std::streamsize>(maxBytes));
    buf.resize(static_cast<size_t>(in.gcount()));
    // Trim a partial trailing line so the tokenizer sees whole records.
    auto nl = buf.find_last_of('\n');
    if (nl != std::string::npos) buf.resize(nl + 1);
    return buf;
}

} // namespace

TEST_CASE("A real combat log parses against the shipped spell data", "[e2e][rollforward]") {
    const std::string logPath = envLogPath();
    if (logPath.empty()) {
        WARN("AWOWLAB_E2E_LOG not set - skipping end-to-end log check");
        return;
    }

    std::string content = readPrefix(logPath, 64u * 1024u * 1024u);
    REQUIRE_FALSE(content.empty());

    tokenized_segment tokenizer;
    auto lines = tokenizer.tokenize(content.data(), content.size());
    REQUIRE(lines.size() > 1000);

    auto extracted = OutputWriter::extractAllEvents(lines);
    ActorMap actorMap = OutputWriter::buildActorMap(extracted);

    // A log that tokenizes but yields no events means the parse silently failed.
    REQUIRE(extracted.spellCastEvents.size() > 0);
    REQUIRE(extracted.combatEvents.size() > 0);

    INFO("lines=" << lines.size()
         << " casts=" << extracted.spellCastEvents.size()
         << " combat=" << extracted.combatEvents.size());
    SUCCEED();
}

TEST_CASE("Spell data answers for abilities the log actually uses", "[e2e][rollforward]") {
    const std::string logPath = envLogPath();
    if (logPath.empty()) {
        WARN("AWOWLAB_E2E_LOG not set - skipping spell coverage check");
        return;
    }

    awow::SpellDataTable table;
    const std::string binDir = envDataDir();
    bool loaded = false;
    for (const char* ver : {"12.1", "12.0"}) {
        if (table.loadFromBinary(binDir + "/spell_data_" + ver + ".bin")) {
            loaded = true;
            break;
        }
    }
    REQUIRE(loaded);

    // Take the spell ids from the parsed cast events rather than scraping text,
    // so this checks the same ids the app itself will look up.
    std::string content = readPrefix(logPath, 64u * 1024u * 1024u);
    REQUIRE_FALSE(content.empty());

    tokenized_segment tokenizer;
    auto lines = tokenizer.tokenize(content.data(), content.size());
    auto extracted = OutputWriter::extractAllEvents(lines);

    std::map<uint32_t, int> castCounts;
    for (const auto& ev : extracted.spellCastEvents) {
        castCounts[ev.spell_id]++;
    }

    REQUIRE(castCounts.size() > 50);

    // How many cast abilities does the table know about? A column shift would
    // wreck this: ids would resolve to the wrong rows or not at all.
    size_t known = 0, withGcd = 0, withCooldown = 0, absurd = 0;
    for (const auto& [spellId, count] : castCounts) {
        const auto* d = table.getSpellInfo(spellId);
        if (!d) continue;
        ++known;
        if (d->gcd_ms > 0) ++withGcd;
        if (d->cooldown_ms > 0) ++withCooldown;
        // Sanity bounds: a GCD over 5s or a cooldown over an hour is not real.
        if (d->gcd_ms > 5000 || d->cooldown_ms > 3600000 || d->cast_time_ms > 60000) {
            ++absurd;
        }
    }

    INFO("distinct cast spells=" << castCounts.size()
         << " known=" << known
         << " withGcd=" << withGcd
         << " withCooldown=" << withCooldown
         << " absurd=" << absurd);

    // Most abilities a player casts should be in the table.
    CHECK(known * 100 / castCounts.size() >= 50);
    // Real values, not garbage from a misread column.
    CHECK(absurd == 0);
    CHECK(withGcd > 0);
    CHECK(withCooldown > 0);
}
