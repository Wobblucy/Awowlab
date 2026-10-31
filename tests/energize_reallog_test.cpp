#include <catch2/catch_test_macros.hpp>
#include <fstream>
#include <string>
#include "Txttokenizer/Txttokenizer.h"
#include "Database/OutputWriter.h"

// SPELL_ENERGIZE carries both the resource gained and the portion wasted
// against the cap. OutputWriter used to read only the position off these
// lines, so overflow - the most direct measure of wasted generation - was
// dropped on the floor.
TEST_CASE("real log energize events record gain and overflow", "[energize][reallog]") {
    const std::string path = REAL_LOG_PATH;
    if (path.empty()) { WARN("AWOWLAB_REAL_LOG not set; skipping"); return; }

    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) { WARN("real log not readable; skipping"); return; }

    std::string buffer(40u * 1024u * 1024u, '\0');
    in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    buffer.resize(static_cast<size_t>(in.gcount()));
    REQUIRE(buffer.size() > 0);

    size_t lastNewline = buffer.rfind('\n');
    REQUIRE(lastNewline != std::string::npos);
    buffer.resize(lastNewline + 1);

    tokenized_segment seg;
    auto lines = seg.tokenize(buffer.data(), buffer.size());
    REQUIRE(lines.size() > 0);

    auto data = OutputWriter::extractAllEvents(lines);

    int withOverflow = 0;
    int drains = 0;
    int fractional = 0;
    double totalGained = 0.0;
    double totalWasted = 0.0;
    for (const auto& e : data.energizeEvents) {
        CHECK(e.spell_id > 0);
        CHECK_FALSE(e.actor_guid.empty());
        // Overflow itself is never negative, but amount can be: the game
        // writes resource drains as SPELL_ENERGIZE with a negative amount.
        CHECK(e.overflow >= 0.0);
        if (e.amount < 0.0) ++drains;
        totalGained += e.amount;
        totalWasted += e.overflow;
        if (e.overflow > 0.0) ++withOverflow;
        // Proof the amounts are read as floating point: an integer parse
        // would floor every one of these to a whole number.
        if (e.amount != static_cast<double>(static_cast<long long>(e.amount))) ++fractional;
    }

    UNSCOPED_INFO("energize events: " << data.energizeEvents.size());
    UNSCOPED_INFO("  with overflow: " << withOverflow);
    UNSCOPED_INFO("  total gained:  " << totalGained);
    UNSCOPED_INFO("  total wasted:  " << totalWasted);
    UNSCOPED_INFO("  drains:        " << drains);
    UNSCOPED_INFO("  fractional:    " << fractional);

    CHECK(data.energizeEvents.size() > 0);
    // If these are zero the amounts are being floored somewhere upstream
    CHECK(fractional > 0);
    CHECK(withOverflow > 0);
}
