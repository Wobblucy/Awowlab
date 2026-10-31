#include "Models/SeasonZones.h"

#include <rapidjson/document.h>

#include <algorithm>

namespace warcraftlogs {
namespace {

// WCL's difficulty ids. A keystone zone offers only the first; a raid offers
// the four raid tiers.
constexpr int32_t kDungeonDifficultyId = 10;
constexpr int32_t kNormalDifficultyId  = 3;
constexpr int32_t kHeroicDifficultyId  = 4;
constexpr int32_t kMythicDifficultyId  = 5;

int32_t getInt(const rapidjson::Value& v, const char* key) {
    if (!v.HasMember(key)) return 0;
    const auto& f = v[key];
    return f.IsInt() ? f.GetInt() : 0;
}

std::string getString(const rapidjson::Value& v, const char* key) {
    if (!v.HasMember(key)) return {};
    const auto& f = v[key];
    return f.IsString() ? std::string(f.GetString()) : std::string{};
}

bool offers(const std::vector<int32_t>& ids, int32_t id) {
    return std::find(ids.begin(), ids.end(), id) != ids.end();
}

// The lowest zone id WCL gives an aggregate zone. Those rank a whole instance
// as a single encounter, so their pulls run hours where a boss runs minutes.
// Both that exist sit here - 509 "Complete Raids (VS / DR / MQD)" and 510 "The
// Venomous Abyss Complete Raid" - while live season zones are two digit.
constexpr int32_t kFirstAggregateZoneId = 500;

// A PTR copy of an encounter is the live encounter id plus this. Rotmire is
// 3159 on live and 53159 on the PTR; Altar of Fangs is 12993 live and 62993 on
// the PTR. The offset holds across raid and keystone zones alike, and digit
// count does not identify it - dungeon ids are natively five digits, so a
// "longer than four digits" rule discards the live keystone zone.
//
// This matters because a PTR zone can be unfrozen while the live zone it
// copies is frozen, which is the state Sporefall is in. The two are otherwise
// identical in shape - same tiers, same boss count - so nothing else separates
// them.
constexpr int32_t kPtrEncounterIdOffset = 50000;

// Decides what a zone is.
//
// Measured against the whole Midnight listing:
//
//   55  Mythic+ Season 2                 Dungeon                    8 encounters
//   510 The Venomous Abyss Complete Raid Mythic,Heroic,Normal,LFR   1
//   53  The Venomous Abyss               Mythic,Heroic,Normal,LFR   10
//   52  Dummy Dome                       Normal                     5
//   51  Sporefall (PTR)                  Mythic,Heroic,Normal,LFR   1
//
// Nothing here reads a name, because name rules don't hold: the live raid and
// its PTR copy differ only by a parenthesis, an aggregate
// zone is not always called "Complete Raid", and "Dummy Dome" is real content
// that shipped as a rankable zone rather than something to discard.
//
// A dummy zone offers Normal and nothing harder, since a dummy has no
// difficulty to speak of. That is also what tells it apart from Sporefall, a
// genuine single-boss raid that still lists all four tiers - so boss count
// alone would misjudge it.
ZoneKind zoneKindOf(int32_t zoneId, const std::vector<int32_t>& difficultyIds) {
    if (zoneId >= kFirstAggregateZoneId) {
        return ZoneKind::Aggregate;
    }

    if (difficultyIds.size() == 1 &&
        difficultyIds.front() == kDungeonDifficultyId) {
        return ZoneKind::Dungeon;
    }

    const bool hasHarderTier = offers(difficultyIds, kHeroicDifficultyId) ||
                               offers(difficultyIds, kMythicDifficultyId);
    if (!hasHarderTier && offers(difficultyIds, kNormalDifficultyId)) {
        return ZoneKind::Dummy;
    }

    return ZoneKind::Raid;
}

// Whether a zone copies another zone in the listing at the PTR offset. Needs
// every zone rather than just this one, because an id on its own says nothing -
// only the presence of the original does.
bool copiesAtPtrOffset(const SeasonZone& candidate,
                       const std::vector<SeasonZone>& all) {
    if (candidate.encounters.empty()) return false;
    const int32_t first = candidate.encounters.front().id;
    if (first <= kPtrEncounterIdOffset) return false;

    const int32_t original = first - kPtrEncounterIdOffset;
    for (const auto& zone : all) {
        if (zone.id == candidate.id) continue;
        for (const auto& enc : zone.encounters) {
            if (enc.id == original) return true;
        }
    }
    return false;
}

// Shared reader. Returns every live zone with its kind already decided, which
// needs two passes: classifying an aggregate zone depends on how big the real
// raid is.
std::vector<SeasonZone> readLiveZones(const std::string& json) {
    std::vector<SeasonZone> zones;

    rapidjson::Document doc;
    doc.Parse(json.c_str());
    if (doc.HasParseError() || !doc.IsObject()) return zones;

    if (!doc.HasMember("data") || !doc["data"].IsObject()) return zones;
    const auto& data = doc["data"];
    if (!data.HasMember("worldData") || !data["worldData"].IsObject()) return zones;
    const auto& world = data["worldData"];
    if (!world.HasMember("expansions") || !world["expansions"].IsArray()) return zones;

    // Newest expansion by id. Relying on array order would put the current
    // season at the mercy of however WCL happens to sort them.
    const rapidjson::Value* newest = nullptr;
    int32_t newestId = -1;
    for (const auto& exp : world["expansions"].GetArray()) {
        const int32_t id = getInt(exp, "id");
        if (id > newestId) { newestId = id; newest = &exp; }
    }
    if (!newest || !newest->HasMember("zones") || !(*newest)["zones"].IsArray()) {
        return zones;
    }

    // Read every zone first, frozen included. A PTR zone is recognised by the
    // live zone it copies, and that original is usually the frozen one, so the
    // frozen entries have to be in hand before anything can be excluded.
    std::vector<SeasonZone> all;
    std::vector<bool> live;
    for (const auto& zone : (*newest)["zones"].GetArray()) {
        if (!zone.HasMember("frozen") || !zone["frozen"].IsBool()) continue;

        SeasonZone parsed;
        parsed.id = getInt(zone, "id");
        parsed.name = getString(zone, "name");

        if (zone.HasMember("difficulties") && zone["difficulties"].IsArray()) {
            for (const auto& diff : zone["difficulties"].GetArray()) {
                parsed.difficultyIds.push_back(getInt(diff, "id"));
            }
        }

        if (zone.HasMember("encounters") && zone["encounters"].IsArray()) {
            for (const auto& enc : zone["encounters"].GetArray()) {
                SeasonEncounter parsedEnc;
                parsedEnc.id = getInt(enc, "id");
                parsedEnc.name = getString(enc, "name");
                if (parsedEnc.id != 0) parsed.encounters.push_back(std::move(parsedEnc));
            }
        }

        if (parsed.encounters.empty()) continue;   // not open yet

        all.push_back(std::move(parsed));
        live.push_back(!zone["frozen"].GetBool());
    }

    for (size_t i = 0; i < all.size(); ++i) {
        // Frozen means last season, or a PTR or beta copy no longer being
        // written to. The converse does not hold, which is the next check.
        if (!live[i]) continue;

        // An unfrozen PTR zone sits beside the frozen live zone it copies -
        // the state Sporefall is in - so frozen alone does not exclude it.
        if (copiesAtPtrOffset(all[i], all)) continue;

        all[i].kind = zoneKindOf(all[i].id, all[i].difficultyIds);
        zones.push_back(std::move(all[i]));
    }

    // Most encounters first: the real raid has the full boss list.
    std::sort(zones.begin(), zones.end(),
              [](const SeasonZone& a, const SeasonZone& b) {
                  return a.encounters.size() > b.encounters.size();
              });
    return zones;
}

} // namespace

std::vector<SeasonZone> parseSeasonZones(const std::string& json, ZoneKind kind) {
    auto zones = readLiveZones(json);
    zones.erase(std::remove_if(zones.begin(), zones.end(),
                               [kind](const SeasonZone& z) { return z.kind != kind; }),
                zones.end());
    return zones;
}

std::vector<SeasonZone> parseAllSeasonZones(const std::string& json) {
    return readLiveZones(json);
}

} // namespace warcraftlogs
