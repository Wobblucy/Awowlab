#include "Core/Benchmark/LivePullBridge.h"

#include "Database/OutputWriter.h"
#include "Txttokenizer/Txttokenizer.h"

#include <algorithm>
#include <fstream>
#include <string_view>
#include <unordered_map>

namespace benchmark {

namespace {

// Reject absurd ranges rather than trying to allocate them. A corrupt or
// half-written segment should fail here, not as a bad_alloc somewhere later.
constexpr size_t kMaxPullBytes = 512ull * 1024ull * 1024ull;

} // namespace

namespace {

// The comma-separated field at `index`, honouring quotes. Names are quoted and
// can contain neither commas nor quotes, so this stays simple: combat log
// lines never contain a double quote inside a field.
std::string_view fieldAt(std::string_view line, int index) {
    int current = 0;
    size_t start = 0;
    bool inQuotes = false;

    for (size_t i = 0; i <= line.size(); ++i) {
        const bool atEnd = (i == line.size());
        if (!atEnd && line[i] == '"') {
            inQuotes = !inQuotes;
            continue;
        }
        if (atEnd || (line[i] == ',' && !inQuotes)) {
            if (current == index) {
                std::string_view field = line.substr(start, i - start);
                if (field.size() >= 2 && field.front() == '"' && field.back() == '"') {
                    field = field.substr(1, field.size() - 2);
                }
                return field;
            }
            ++current;
            start = i + 1;
        }
    }
    return {};
}

// Everything after the timestamp. Lines are "M/D/YYYY H:MM:SS.mmm-O  EVENT,..."
// with two spaces before the event name.
std::string_view payloadOf(std::string_view line) {
    const size_t sep = line.find("  ");
    if (sep == std::string_view::npos) return {};
    return line.substr(sep + 2);
}

} // namespace

std::vector<RosterEntry> readRoster(const std::filesystem::path& logPath,
                                    size_t startByteOffset,
                                    size_t endByteOffset) {
    std::vector<RosterEntry> roster;

    if (startByteOffset >= endByteOffset) return roster;

    std::ifstream file(logPath, std::ios::binary);
    if (!file) return roster;

    file.seekg(static_cast<std::streamoff>(startByteOffset));
    if (!file) return roster;

    // COMBATANT_INFO sits at the very top of a pull and the names follow in
    // the first casts, so the roster is settled within the first fraction of
    // even a long fight. Cap the scan rather than read to the end: if the
    // names are not there by now they are not coming, and reading a 140 MB
    // pull to find out would be the very thing this avoids.
    constexpr size_t kMaxScanBytes = 4ull * 1024ull * 1024ull;
    const size_t available = endByteOffset - startByteOffset;
    const size_t toRead = std::min(available, kMaxScanBytes);

    std::string buffer(toRead, '\0');
    file.read(buffer.data(), static_cast<std::streamsize>(toRead));
    buffer.resize(static_cast<size_t>(file.gcount()));
    if (buffer.empty()) return roster;

    // guid -> index into roster, so a name can be filled in when it turns up.
    // Keyed by value rather than by a view into roster's own strings: growing
    // the vector reallocates them, and every key would dangle.
    std::unordered_map<std::string, size_t> byGuid;
    size_t named = 0;

    size_t lineStart = 0;
    while (lineStart < buffer.size()) {
        size_t lineEnd = buffer.find('\n', lineStart);
        if (lineEnd == std::string::npos) lineEnd = buffer.size();

        const std::string_view payload =
            payloadOf(std::string_view(buffer).substr(lineStart, lineEnd - lineStart));
        lineStart = lineEnd + 1;
        if (payload.empty()) continue;

        const std::string_view event = fieldAt(payload, 0);

        if (event == "COMBATANT_INFO") {
            const std::string_view guid = fieldAt(payload, 1);
            if (guid.empty()) continue;

            // Field 2 is faction; the spec id's position varies by expansion,
            // so it is left to the parser rather than guessed at here. The
            // picker shows names, and the spec is only a hint beside them.
            if (byGuid.contains(std::string(guid))) continue;

            RosterEntry entry;
            entry.guid = std::string(guid);
            roster.push_back(std::move(entry));
            byGuid.emplace(std::string(guid), roster.size() - 1);
            continue;
        }

        // Any event with a source: field 1 is the GUID, field 2 the name.
        const std::string_view guid = fieldAt(payload, 1);
        if (guid.empty() || !guid.starts_with("Player-")) continue;

        auto it = byGuid.find(std::string(guid));
        if (it == byGuid.end()) continue;
        if (!roster[it->second].name.empty()) continue;

        const std::string_view name = fieldAt(payload, 2);
        if (name.empty()) continue;

        roster[it->second].name = std::string(name);
        if (++named == roster.size()) break;   // Everyone accounted for
    }

    // Anyone who never acted has no name to show, so there is nothing to pick.
    std::erase_if(roster, [](const RosterEntry& e) { return e.name.empty(); });

    std::sort(roster.begin(), roster.end(),
              [](const RosterEntry& a, const RosterEntry& b) {
                  return a.name < b.name;
              });
    return roster;
}

void extractLivePull(LivePullEvents& out,
                     const std::filesystem::path& logPath,
                     size_t startByteOffset,
                     size_t endByteOffset) {
    if (startByteOffset >= endByteOffset) return;
    if (endByteOffset - startByteOffset > kMaxPullBytes) return;

    std::ifstream file(logPath, std::ios::binary);
    if (!file) return;

    file.seekg(static_cast<std::streamoff>(startByteOffset));
    if (!file) return;

    // Read straight into the result. The parsed events point into these bytes,
    // so they have to outlive this function - a local buffer left every
    // ActorEvent's guid dangling the moment it returned.
    const size_t bytesToRead = endByteOffset - startByteOffset;
    std::string& buffer = out.backing;
    buffer.assign(bytesToRead, '\0');
    file.read(buffer.data(), static_cast<std::streamsize>(bytesToRead));
    const size_t bytesRead = static_cast<size_t>(file.gcount());
    if (bytesRead == 0) return;

    buffer.resize(bytesRead);

    // A byte window can slice mid-line at either end. Half a line parses into
    // nonsense rather than failing, so both partials are dropped - matching
    // how the live session reads its own historical segments.
    if (startByteOffset > 0) {
        const size_t firstNewline = buffer.find('\n');
        if (firstNewline == std::string::npos) return;
        buffer.erase(0, firstNewline + 1);
    }
    const size_t lastNewline = buffer.rfind('\n');
    if (lastNewline != std::string::npos && lastNewline + 1 < buffer.size()) {
        buffer.resize(lastNewline + 1);
    }
    if (buffer.empty()) return;

    tokenized_segment tokenizer;
    auto lines = tokenizer.tokenize(buffer.data(), buffer.size());
    if (lines.empty()) return;

    // The same extractor the imported path uses, so both sides of a
    // comparison are measured by identical code.
    out.data = OutputWriter::extractAllEvents(lines);

    // Timestamps come out log-relative. The comparison works in pull-relative
    // time, so rebase on the earliest event seen: that is the pull start as
    // far as this data is concerned.
    int32_t earliest = INT32_MAX;
    const auto seen = [&earliest](int32_t t) {
        if (t < earliest) earliest = t;
    };
    for (const auto& e : out.data.spellCastEvents) seen(e.end_timestamp_ms);
    for (const auto& e : out.data.combatEvents)    seen(e.timestamp_ms);
    for (const auto& e : out.data.auraEvents)      seen(e.timestamp_ms);
    for (const auto& e : out.data.deathEvents)     seen(e.timestamp_ms);

    if (earliest == INT32_MAX) return;   // Nothing usable in the range

    const auto rebase = [earliest](int32_t t) { return t - earliest; };
    for (auto& e : out.data.spellCastEvents) {
        e.end_timestamp_ms = rebase(e.end_timestamp_ms);
        // Instant casts store 0 rather than a real start; rebasing that would
        // turn it into a negative number that reads as a pre-pull cast.
        if (e.start_timestamp_ms != 0) {
            e.start_timestamp_ms = rebase(e.start_timestamp_ms);
        }
        if (e.cooldown_ready_ms != 0) {
            e.cooldown_ready_ms = rebase(e.cooldown_ready_ms);
        }
    }
    for (auto& e : out.data.combatEvents)   e.timestamp_ms = rebase(e.timestamp_ms);
    for (auto& e : out.data.auraEvents)     e.timestamp_ms = rebase(e.timestamp_ms);
    for (auto& e : out.data.deathEvents)    e.timestamp_ms = rebase(e.timestamp_ms);
    for (auto& e : out.data.resourceEvents) e.timestamp_ms = rebase(e.timestamp_ms);
    for (auto& e : out.data.emoteEvents)    e.timestamp_ms = rebase(e.timestamp_ms);

    int32_t latest = 0;
    for (const auto& e : out.data.spellCastEvents) latest = std::max(latest, e.end_timestamp_ms);
    for (const auto& e : out.data.combatEvents)    latest = std::max(latest, e.timestamp_ms);
    out.durationMs = latest;

    out.valid = true;
}

} // namespace benchmark
