#include "Expectation.h"
#include "PressGroups.h"

#include "Database/AuraDatabase.h"
#include "Database/EventTypes.h"
#include "Database/SpellCastEvent.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <unordered_map>

namespace awow::analysis {
namespace {

// Gaps longer than this count as downtime. Short enough to catch a missed
// global, long enough not to flag ordinary cast time.
constexpr int32_t kIdleGapMs = 2000;

Band summarise(const std::string& measure, std::vector<double> values) {
    Band band;
    band.measure = measure;
    band.samples = static_cast<uint32_t>(values.size());
    if (values.empty()) return band;

    std::sort(values.begin(), values.end());
    const auto at = [&values](double q) {
        const size_t i = std::min(values.size() - 1,
                                  static_cast<size_t>(q * values.size()));
        return values[i];
    };
    band.p25 = at(0.25);
    band.median = at(0.50);
    band.p75 = at(0.75);
    band.best = values.front();
    band.worst = values.back();
    return band;
}

// One fight's numbers, before they are turned into bands.
struct FightMeasures {
    std::map<std::string, double> value;
};

FightMeasures measureFight(const ExpectationFight& fight, const PressProfile& presses) {
    FightMeasures out;
    const auto* data = fight.data;
    const double minutes = fight.durationMs / 60000.0;
    if (!data || minutes <= 0.0) return out;

    // Presses, by name, so a band reads as an ability rather than a spell id.
    std::vector<int32_t> pressTimes;
    std::unordered_map<std::string, uint32_t> perAbility;
    for (const auto& cast : data->spellCastEvents) {
        if (!fight.playerGuid.empty() && cast.actor_guid != fight.playerGuid) continue;
        if (!presses.empty() && !presses.isPress(cast.spell_id)) continue;
        pressTimes.push_back(cast.end_timestamp_ms);
        const auto it = data->spellNameMap.find(cast.spell_id);
        if (it != data->spellNameMap.end()) ++perAbility[it->second];
    }
    for (const auto& [name, count] : perAbility) {
        out.value["cast/" + name] = count / minutes;
    }
    out.value["presses_per_min"] = pressTimes.size() / minutes;

    // Downtime: the share of the fight with no press at all.
    //
    // Only abilities that occupy the global cooldown count. An off-global -
    // Blur, Disrupt, a racial, a trinket - can be used during a gap without
    // filling it, so counting one as activity hides the very downtime this is
    // meant to find. Vengeful Retreat is a third case: it sits on its own
    // global, shared with Fel Rush, so it neither fills a gap nor conflicts
    // with a normal press, and it is excluded for the same reason.
    std::vector<int32_t> globalTimes;
    globalTimes.reserve(pressTimes.size());
    for (const auto& cast : data->spellCastEvents) {
        if (!fight.playerGuid.empty() && cast.actor_guid != fight.playerGuid) continue;
        if (!presses.empty() && !presses.isPress(cast.spell_id)) continue;
        if (presses.isOffGlobal(cast.spell_id)) continue;
        globalTimes.push_back(cast.end_timestamp_ms);
    }
    std::sort(globalTimes.begin(), globalTimes.end());
    pressTimes.swap(globalTimes);
    int64_t idle = 0;
    for (size_t i = 1; i < pressTimes.size(); ++i) {
        const int32_t gap = pressTimes[i] - pressTimes[i - 1];
        if (gap > kIdleGapMs) idle += gap;
    }
    out.value["downtime_pct"] = fight.durationMs ? 100.0 * idle / fight.durationMs : 0.0;

    // Resource waste. Overcapping looks like a fault and is not - good players
    // sit well above zero - so the band is the whole point of reporting it.
    double gained = 0.0, overflowed = 0.0;
    for (const auto& gain : data->energizeEvents) {
        if (!fight.playerGuid.empty() && gain.actor_guid != fight.playerGuid) continue;
        gained += gain.amount;
        overflowed += gain.overflow;
    }
    if (gained + overflowed > 0.0) {
        out.value["resource_waste_pct"] = 100.0 * overflowed / (gained + overflowed);
    }

    // Aura uptime, both the player's buffs and the debuffs they applied. Over
    // 100% means the debuff sat on several targets at once, which is a spread
    // measure rather than an error.
    struct Open { int32_t at; };
    std::map<std::pair<uint32_t, std::string>, int32_t> open;
    std::map<std::string, int64_t> uptime;
    std::map<std::string, bool> mine;
    for (const auto& aura : data->auraEvents) {
        const bool onPlayer = aura.target_guid == fight.playerGuid;
        const bool byPlayer = aura.source_guid == fight.playerGuid;
        if (!onPlayer && !byPlayer) continue;

        const auto key = std::make_pair(aura.spell_id, aura.target_guid);
        if (aura.event_type == AuraEventType::Applied) {
            open.emplace(key, aura.timestamp_ms);
        } else if (aura.event_type == AuraEventType::Removed) {
            const auto it = open.find(key);
            if (it != open.end()) {
                uptime[aura.spell_name] += aura.timestamp_ms - it->second;
                mine[aura.spell_name] = onPlayer;
                open.erase(it);
            }
        }
    }
    for (const auto& [name, ms] : uptime) {
        if (name.empty() || !fight.durationMs) continue;
        out.value[(mine[name] ? "buff/" : "debuff/") + name] =
            100.0 * ms / fight.durationMs;
    }

    return out;
}

} // namespace

double Band::spread() const {
    if (median == 0.0) return 0.0;
    return (p75 - p25) / std::abs(median);
}

const Band* Expectation::band(const std::string& measure) const {
    const auto it = std::find_if(bands.begin(), bands.end(),
                                 [&measure](const Band& b) { return b.measure == measure; });
    return it == bands.end() ? nullptr : &*it;
}

Expectation buildExpectation(const std::vector<ExpectationFight>& fights,
                             const PressProfile& presses) {
    Expectation out;
    out.specId = presses.specId;
    out.specName = presses.specName;
    out.gameBuild = presses.gameBuild;
    out.fights = static_cast<uint32_t>(fights.size());

    std::map<std::string, std::vector<double>> collected;
    std::map<std::string, bool> playerSeen;
    for (const auto& fight : fights) {
        if (!fight.data) continue;
        out.minutes += fight.durationMs / 60000.0;
        if (!fight.playerName.empty()) playerSeen[fight.playerName] = true;
        for (const auto& [measure, value] : measureFight(fight, presses).value) {
            collected[measure].push_back(value);
        }
    }
    out.players = static_cast<uint32_t>(playerSeen.size());

    // A band needs enough fights behind it to mean anything. Ten is the floor
    // for a five minute encounter: an ability used once a minute appears only a
    // handful of times per pull, so a band over five fights is describing a
    // couple of dozen casts and will move with any one of them.
    //
    // Measures below this are dropped rather than reported with a caveat - a
    // reader comparing against four samples is worse off than one told nothing.
    constexpr size_t kMinSamples = 10;
    for (auto& [measure, values] : collected) {
        if (values.size() < kMinSamples) continue;
        out.bands.push_back(summarise(measure, std::move(values)));
    }
    std::sort(out.bands.begin(), out.bands.end(),
              [](const Band& a, const Band& b) { return a.measure < b.measure; });
    return out;
}

std::string writeExpectation(const Expectation& expectation, const std::string& path) {
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);

    std::ofstream out(path, std::ios::binary);
    if (!out.is_open()) return "cannot write " + path;

    const auto quoted = [](const std::string& s) {
        std::string escaped;
        for (char c : s) {
            if (c == '"' || c == '\\') escaped += '\\';
            escaped += c;
        }
        return "\"" + escaped + "\"";
    };

    out << "{\n"
        << "  \"spec_id\": " << expectation.specId << ",\n"
        << "  \"spec_name\": " << quoted(expectation.specName) << ",\n"
        << "  \"hero_tree\": " << quoted(expectation.heroTree) << ",\n"
        << "  \"difficulty\": " << quoted(expectation.difficulty) << ",\n"
        << "  \"game_build\": " << quoted(expectation.gameBuild) << ",\n"
        << "  \"fights\": " << expectation.fights << ",\n"
        << "  \"players\": " << expectation.players << ",\n"
        << "  \"minutes\": " << static_cast<int>(expectation.minutes) << ",\n"
        << "  \"bands\": [\n";
    for (size_t i = 0; i < expectation.bands.size(); ++i) {
        const auto& b = expectation.bands[i];
        out << "    {\"measure\": " << quoted(b.measure)
            << ", \"p25\": " << b.p25 << ", \"median\": " << b.median
            << ", \"p75\": " << b.p75 << ", \"best\": " << b.best
            << ", \"worst\": " << b.worst << ", \"samples\": " << b.samples << "}"
            << (i + 1 < expectation.bands.size() ? "," : "") << "\n";
    }
    out << "  ],\n  \"mistakes\": [\n";
    for (size_t i = 0; i < expectation.mistakes.size(); ++i) {
        const auto& m = expectation.mistakes[i];
        out << "    {\"key\": " << quoted(m.key)
            << ", \"description\": " << quoted(m.description)
            << ", \"p25\": " << m.p25 << ", \"median\": " << m.median
            << ", \"p75\": " << m.p75 << ", \"best\": " << m.best
            << ", \"samples\": " << m.samples
            << ", \"cost_note\": " << quoted(m.costNote) << "}"
            << (i + 1 < expectation.mistakes.size() ? "," : "") << "\n";
    }
    out << "  ]\n}\n";

    out.flush();
    return out.good() ? std::string{} : "write failed for " + path;
}

bool readExpectation(const std::string& path, Expectation& /*out*/) {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) return false;
    // Reading back is only needed when a review is built against a stored
    // expectation; the caller that does that will parse it with rapidjson,
    // which Database deliberately does not link. Left unimplemented rather
    // than half-implemented so a caller cannot mistake an empty result for a
    // successful load.
    return false;
}

} // namespace awow::analysis
