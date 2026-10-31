#include "PressGroups.h"

#include "Database/EventTypes.h"
#include "Database/SpellCastEvent.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace awow::analysis {
namespace {

struct Seen {
    uint32_t casts = 0;
    uint32_t alone = 0;
    std::string name;
};

// Union-find over spell ids: two ids that ever land in the same cluster belong
// to the same question. Transitive on purpose - Death Sweep travels with a
// Throw Glaive id which travels with another, and splitting that chain would
// ask the player three questions about one action.
// Two ids are joined only once they have landed together often enough. A
// single coincidence should not merge two unrelated abilities into one
// question, and a real relationship - a glaive thrown by every Blade Dance -
// clears the bar easily.
class Groups {
public:
    uint32_t find(uint32_t id) {
        auto it = parent_.find(id);
        if (it == parent_.end()) { parent_[id] = id; return id; }
        if (it->second == id) return id;
        return parent_[id] = find(it->second);
    }

    void join(uint32_t a, uint32_t b) {
        const uint32_t ra = find(a), rb = find(b);
        if (ra != rb) parent_[ra] = rb;
    }

private:
    std::unordered_map<uint32_t, uint32_t> parent_;
};

} // namespace

bool PressGroup::allExcluded() const {
    return !options.empty() &&
           std::all_of(options.begin(), options.end(),
                       [](const SpellOption& o) { return o.excludedByRate; });
}

bool PressProfile::isPress(uint32_t spellId) const {
    return std::find(pressed.begin(), pressed.end(), spellId) != pressed.end();
}

bool PressProfile::isOffGlobal(uint32_t spellId) const {
    return std::find(offGlobal.begin(), offGlobal.end(), spellId) != offGlobal.end();
}

std::vector<PressGroup> groupCasts(const std::vector<FightCasts>& fights) {
    std::unordered_map<uint32_t, Seen> seen;
    Groups groups;
    std::map<std::pair<uint32_t, uint32_t>, uint32_t> pairs;
    double totalMinutes = 0.0;

    for (const auto& fight : fights) {
        if (!fight.data) continue;
        totalMinutes += fight.durationMs / 60000.0;

        // One fight's casts by the player, in time order.
        std::vector<std::pair<int32_t, uint32_t>> casts;
        for (const auto& cast : fight.data->spellCastEvents) {
            if (!fight.playerGuid.empty() && cast.actor_guid != fight.playerGuid) continue;
            casts.emplace_back(cast.end_timestamp_ms, cast.spell_id);
            auto& record = seen[cast.spell_id];
            ++record.casts;
            if (record.name.empty()) {
                const auto it = fight.data->spellNameMap.find(cast.spell_id);
                if (it != fight.data->spellNameMap.end()) record.name = it->second;
            }
        }
        std::sort(casts.begin(), casts.end());

        // Walk the timeline once, joining every id that shares a window with
        // another and counting how often an id stands by itself. The alone
        // count is for the player to read, not for the code to act on: Death
        // Sweep is alone in 1-2% of its casts and is certainly pressed.
        for (size_t i = 0; i < casts.size(); ++i) {
            bool hadCompany = false;
            for (size_t j = i + 1;
                 j < casts.size() && casts[j].first - casts[i].first <= kClusterWindowMs;
                 ++j) {
                if (casts[j].second == casts[i].second) continue;
                ++pairs[std::minmax(casts[i].second, casts[j].second)];
                hadCompany = true;
            }
            for (size_t j = i; j-- > 0 && casts[i].first - casts[j].first <= kClusterWindowMs;) {
                if (casts[j].second == casts[i].second) continue;
                hadCompany = true;
            }
            if (!hadCompany) ++seen[casts[i].second].alone;
        }
    }

    if (totalMinutes <= 0.0) return {};

    // Join only the pairings that recur. Everything below the threshold stays
    // its own question, which is the safe failure: asking about an ability on
    // its own costs the user a click, merging two unrelated ones loses a press.
    for (const auto& [pair, matches] : pairs) {
        if (matches >= kMinPairMatches) groups.join(pair.first, pair.second);
    }

    // Collect into groups, ordered so the busiest question comes first.
    std::map<uint32_t, std::vector<SpellOption>> byRoot;
    for (const auto& [spellId, record] : seen) {
        SpellOption option;
        option.spellId = spellId;
        option.name = record.name;
        option.casts = record.casts;
        option.perMinute = record.casts / totalMinutes;
        option.aloneFraction = record.casts ? static_cast<double>(record.alone) / record.casts
                                            : 0.0;
        option.excludedByRate = option.perMinute > kMaxPressesPerMinute;
        byRoot[groups.find(spellId)].push_back(std::move(option));
    }

    std::vector<PressGroup> result;
    result.reserve(byRoot.size());
    for (auto& [root, options] : byRoot) {
        std::sort(options.begin(), options.end(),
                  [](const SpellOption& a, const SpellOption& b) {
                      return a.casts > b.casts;
                  });
        PressGroup group;
        group.options = std::move(options);
        result.push_back(std::move(group));
    }
    std::sort(result.begin(), result.end(),
              [](const PressGroup& a, const PressGroup& b) {
                  const auto weight = [](const PressGroup& g) {
                      uint32_t n = 0;
                      for (const auto& o : g.options) n += o.casts;
                      return n;
                  };
                  return weight(a) > weight(b);
              });
    return result;
}

std::string writePressProfile(const PressProfile& profile, const std::string& path) {
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);

    std::ofstream out(path, std::ios::binary);
    if (!out.is_open()) return "cannot write " + path;

    out << "{\n"
        << "  \"spec_id\": " << profile.specId << ",\n"
        << "  \"spec_name\": \"" << profile.specName << "\",\n"
        << "  \"game_build\": \"" << profile.gameBuild << "\",\n"
        << "  \"pressed\": [";
    for (size_t i = 0; i < profile.pressed.size(); ++i) {
        out << (i ? ", " : "") << profile.pressed[i];
    }
    out << "],\n  \"off_global\": [";
    for (size_t i = 0; i < profile.offGlobal.size(); ++i) {
        out << (i ? ", " : "") << profile.offGlobal[i];
    }
    out << "]\n}\n";

    out.flush();
    return out.good() ? std::string{} : "write failed for " + path;
}

bool readPressProfile(const std::string& path, PressProfile& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) return false;

    const std::string body((std::istreambuf_iterator<char>(in)),
                           std::istreambuf_iterator<char>());

    // Small enough to read without pulling a JSON parser into Database: the
    // file is written above and holds one array of numbers.
    const auto start = body.find("\"pressed\"");
    if (start == std::string::npos) return false;
    const auto open = body.find('[', start);
    const auto close = body.find(']', open);
    if (open == std::string::npos || close == std::string::npos) return false;

    out.pressed.clear();
    std::istringstream ids(body.substr(open + 1, close - open - 1));
    std::string token;
    while (std::getline(ids, token, ',')) {
        try {
            out.pressed.push_back(static_cast<uint32_t>(std::stoul(token)));
        } catch (const std::exception&) {
            // A malformed entry is skipped rather than failing the load: a
            // partially readable profile still beats treating every trigger as
            // a press.
        }
    }
    return !out.pressed.empty();
}

} // namespace awow::analysis
