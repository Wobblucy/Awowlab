#include "ResourceCsvWriter.h"

#include "../EventTypes.h"
#include "CsvUtil.h"

#include <fstream>

namespace awow::analysis {

bool writeResourcesCsv(const std::string& path,
                       const ExtractedData& data,
                       std::string_view playerGuid) {
    std::ofstream out(pathFromUtf8(path), std::ios::binary);
    if (!out.is_open()) return false;

    out << "timestamp_ms,actor_guid,actor_role,current_health,max_health,"
           "power_slot,power_type,current_power,max_power\r\n";

    const bool scoped = !playerGuid.empty();

    for (const auto& ev : data.resourceEvents) {
        const bool isPlayer = (ev.actor_guid == playerGuid);
        const bool isHostile = data.hostileGuids.count(ev.actor_guid) > 0;

        // Scoped exports keep the player plus boss health for context
        if (scoped && !isPlayer && !isHostile) continue;

        const char* role = isPlayer ? "player" : (isHostile ? "enemy" : "other");

        // Health is per event; write it on the primary row so a reader summing
        // health does not count the same snapshot once per resource.
        auto writeRow = [&](int slot, int8_t type, uint32_t current, uint32_t max) {
            out << ev.timestamp_ms << ','
                << csvField(ev.actor_guid) << ','
                << role << ',';
            if (slot == 0) {
                out << ev.current_health << ',' << ev.max_health << ',';
            } else {
                out << ",,";
            }
            out << slot << ','
                << static_cast<int>(type) << ','
                << current << ','
                << max << "\r\n";
        };

        // power_type -1 means the event carried no power data - health only
        writeRow(0, ev.power_type, ev.current_power, ev.max_power);
        int slot = 1;
        for (const auto& p : ev.secondary_powers) {
            writeRow(slot++, p.type, p.current, p.max);
        }
    }

    out.flush();
    return out.good();
}

bool writeEnergizeCsv(const std::string& path,
                      const ExtractedData& data,
                      std::string_view playerGuid,
                      const SpellNames* names) {
    std::ofstream out(pathFromUtf8(path), std::ios::binary);
    if (!out.is_open()) return false;

    out << "timestamp_ms,actor_guid,spell_id,spell_name,power_type,amount,overflow\r\n";

    const bool scoped = !playerGuid.empty();

    for (const auto& ev : data.energizeEvents) {
        if (scoped && ev.actor_guid != playerGuid) continue;

        // Caller's resolution first, then whatever the log carried
        std::string_view spellName;
        if (names) {
            auto nit = names->find(ev.spell_id);
            if (nit != names->end() && !nit->second.empty()) spellName = nit->second;
        }
        if (spellName.empty()) {
            auto it = data.spellNameMap.find(ev.spell_id);
            if (it != data.spellNameMap.end()) spellName = it->second;
        }

        out << ev.timestamp_ms << ','
            << csvField(ev.actor_guid) << ','
            << ev.spell_id << ','
            << csvField(spellName) << ','
            << static_cast<int>(ev.power_type) << ','
            << amountField(ev.amount) << ','
            << amountField(ev.overflow) << "\r\n";
    }

    out.flush();
    return out.good();
}

} // namespace awow::analysis
