#include "fixture_generator.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <cmath>
#include <rapidjson/writer.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/prettywriter.h>

namespace fs = std::filesystem;

namespace parity {

FixtureGenerator::FixtureGenerator() = default;
FixtureGenerator::~FixtureGenerator() = default;

bool FixtureGenerator::loadLocalLog(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        std::cerr << "Failed to open local log: " << filepath << std::endl;
        return false;
    }

    std::string line;
    bool found_encounter = false;

    while (std::getline(file, line)) {
        if (line.empty()) continue;

        local_lines_.push_back(line);

        LocalEvent evt = parseLocalLine(line);
        if (evt.event_type.empty()) continue;

        if (local_log_start_ms_ == 0 && evt.timestamp_ms > 0) {
            local_log_start_ms_ = evt.timestamp_ms;
        }

        // ENCOUNTER_START is the zero point of the local timeline.
        if (!found_encounter && evt.event_type == "ENCOUNTER_START") {
            local_encounter_start_ms_ = evt.timestamp_ms;
            encounter_start_line_ = line;
            found_encounter = true;
            std::cout << "Found ENCOUNTER_START at " << local_encounter_start_ms_ << "ms" << std::endl;
        }

        if (!evt.source_name.empty() && !evt.source_guid.empty()) {
            guid_to_name_[evt.source_guid] = evt.source_name;
        }
        if (!evt.dest_name.empty() && !evt.dest_guid.empty()) {
            guid_to_name_[evt.dest_guid] = evt.dest_name;
        }

        local_events_.push_back(std::move(evt));
    }

    std::cout << "Loaded " << local_events_.size() << " local events" << std::endl;
    std::cout << "Found " << guid_to_name_.size() << " unique actors" << std::endl;

    return true;
}

bool FixtureGenerator::loadWclPages(const std::string& directory) {
    std::vector<std::string> page_files;

    for (const auto& entry : fs::directory_iterator(directory)) {
        if (entry.is_regular_file()) {
            std::string filename = entry.path().filename().string();
            if (filename.find("page") != std::string::npos &&
                filename.find(".json") != std::string::npos) {
                page_files.push_back(entry.path().string());
            }
        }
    }

    std::sort(page_files.begin(), page_files.end());

    std::cout << "Found " << page_files.size() << " WCL page files" << std::endl;

    for (size_t i = 0; i < page_files.size(); ++i) {
        std::ifstream file(page_files[i]);
        if (!file.is_open()) {
            std::cerr << "Failed to open: " << page_files[i] << std::endl;
            continue;
        }

        std::stringstream buffer;
        buffer << file.rdbuf();
        std::string content = buffer.str();

        rapidjson::Document doc;
        doc.Parse(content.c_str());

        if (doc.HasParseError()) {
            std::cerr << "JSON parse error in: " << page_files[i] << std::endl;
            continue;
        }

        if (i == 0 && wcl_fight_start_ms_ == 0) {
            // The earliest timestamp on the first page stands in for the fight start.
            if (doc.IsArray() && doc.Size() > 0) {
                for (rapidjson::SizeType j = 0; j < doc.Size(); ++j) {
                    if (doc[j].HasMember("timestamp") && doc[j]["timestamp"].IsUint64()) {
                        int64_t ts = doc[j]["timestamp"].GetInt64();
                        if (wcl_fight_start_ms_ == 0 || ts < wcl_fight_start_ms_) {
                            wcl_fight_start_ms_ = ts;
                        }
                    }
                }
            }
        }

        if (doc.IsArray()) {
            for (rapidjson::SizeType j = 0; j < doc.Size(); ++j) {
                const auto& evt = doc[j];
                if (!evt.HasMember("type") || !evt["type"].IsString()) continue;

                WclEvent wclEvt;
                wclEvt.page_index = i;
                wclEvt.event_index = j;
                wclEvt.event_type = evt["type"].GetString();

                if (evt.HasMember("timestamp") && evt["timestamp"].IsInt64()) {
                    wclEvt.timestamp_ms = static_cast<int32_t>(
                        evt["timestamp"].GetInt64() - wcl_fight_start_ms_);
                }

                if (evt.HasMember("sourceID") && evt["sourceID"].IsInt()) {
                    wclEvt.source_id = evt["sourceID"].GetInt();
                }
                if (evt.HasMember("targetID") && evt["targetID"].IsInt()) {
                    wclEvt.target_id = evt["targetID"].GetInt();
                }
                if (evt.HasMember("abilityGameID") && evt["abilityGameID"].IsInt()) {
                    wclEvt.spell_id = static_cast<uint32_t>(evt["abilityGameID"].GetInt());
                }
                if (evt.HasMember("amount")) {
                    if (evt["amount"].IsInt64()) {
                        wclEvt.amount = evt["amount"].GetInt64();
                    } else if (evt["amount"].IsInt()) {
                        wclEvt.amount = evt["amount"].GetInt();
                    }
                }
                if (evt.HasMember("tick") && evt["tick"].IsBool()) {
                    wclEvt.is_periodic = evt["tick"].GetBool();
                }
                if (evt.HasMember("hitType") && evt["hitType"].IsInt()) {
                    wclEvt.is_critical = (evt["hitType"].GetInt() == 2);
                }

                // Max HP tells players apart, which matching relies on.
                if (evt.HasMember("maxHitPoints")) {
                    if (evt["maxHitPoints"].IsInt64()) {
                        wclEvt.max_hp = evt["maxHitPoints"].GetInt64();
                    } else if (evt["maxHitPoints"].IsInt()) {
                        wclEvt.max_hp = evt["maxHitPoints"].GetInt();
                    }
                }
                if (evt.HasMember("hitPoints")) {
                    if (evt["hitPoints"].IsInt64()) {
                        wclEvt.current_hp = evt["hitPoints"].GetInt64();
                    } else if (evt["hitPoints"].IsInt()) {
                        wclEvt.current_hp = evt["hitPoints"].GetInt();
                    }
                }

                // supportID marks an Augmentation Evoker support event.
                wclEvt.has_support_id = evt.HasMember("supportID");

                wcl_events_.push_back(std::move(wclEvt));
            }
        }

        wcl_pages_.push_back(std::move(doc));
    }

    std::cout << "Loaded " << wcl_events_.size() << " WCL events" << std::endl;
    std::cout << "WCL fight start timestamp: " << wcl_fight_start_ms_ << std::endl;

    return !wcl_pages_.empty();
}

LocalEvent FixtureGenerator::parseLocalLine(const std::string& line) {
    LocalEvent evt;
    evt.raw_line = line;

    // "MM/DD/YYYY HH:MM:SS.mmm-TZ  EVENT_TYPE,...", with two spaces before the
    // event type.

    size_t double_space = line.find("  ");
    if (double_space == std::string::npos) return evt;

    std::string datetime = line.substr(0, double_space);
    std::string rest = line.substr(double_space + 2);

    size_t space_pos = datetime.find(' ');
    if (space_pos == std::string::npos) return evt;

    std::string date = datetime.substr(0, space_pos);
    std::string time = datetime.substr(space_pos + 1);

    evt.timestamp_ms = static_cast<int32_t>(parseLocalTimestamp(date, time));

    size_t comma_pos = rest.find(',');
    if (comma_pos == std::string::npos) {
        evt.event_type = rest;
        return evt;
    }

    evt.event_type = rest.substr(0, comma_pos);
    rest = rest.substr(comma_pos + 1);

    evt.is_support = (evt.event_type.find("SUPPORT") != std::string::npos);

    // After the event type: SourceGUID,"SourceName",SourceFlags,RaidFlags,
    // DestGUID,"DestName",DestFlags,RaidFlags, then the event's own fields.
    auto parseNextToken = [](std::string& str) -> std::string {
        if (str.empty()) return "";

        std::string token;
        if (str[0] == '"') {
            size_t end = str.find('"', 1);
            if (end != std::string::npos) {
                token = str.substr(1, end - 1);
                str = (end + 1 < str.size() && str[end + 1] == ',')
                    ? str.substr(end + 2)
                    : str.substr(end + 1);
            }
        } else {
            size_t comma = str.find(',');
            if (comma != std::string::npos) {
                token = str.substr(0, comma);
                str = str.substr(comma + 1);
            } else {
                token = str;
                str.clear();
            }
        }
        return token;
    };

    evt.source_guid = parseNextToken(rest);

    evt.source_name = parseNextToken(rest);

    // Source flags and raid flags.
    parseNextToken(rest);
    parseNextToken(rest);

    evt.dest_guid = parseNextToken(rest);

    evt.dest_name = parseNextToken(rest);

    // Dest flags and raid flags.
    parseNextToken(rest);
    parseNextToken(rest);

    // SPELL and RANGE events carry spell id, name and school next.
    if (evt.event_type.find("SPELL") != std::string::npos ||
        evt.event_type.find("RANGE") != std::string::npos) {
        std::string spell_id_str = parseNextToken(rest);
        if (!spell_id_str.empty()) {
            try {
                evt.spell_id = static_cast<uint32_t>(std::stoul(spell_id_str));
            } catch (...) {}
        }
        // Spell name and school.
        parseNextToken(rest);
        parseNextToken(rest);

        // Damage events: after the spell prefix comes the advanced log block
        // (InfoGUID, OwnerGUID, CurrentHP, MaxHP, then 15 more fields), then the
        // damage suffix, whose first field is the amount.
        if (evt.event_type.find("DAMAGE") != std::string::npos) {
            // InfoGUID, OwnerGUID
            parseNextToken(rest);
            parseNextToken(rest);

            // CurrentHP, MaxHP
            std::string current_hp_str = parseNextToken(rest);
            std::string max_hp_str = parseNextToken(rest);
            if (!current_hp_str.empty()) {
                try { evt.current_hp = std::stoll(current_hp_str); } catch (...) {}
            }
            if (!max_hp_str.empty()) {
                try { evt.max_hp = std::stoll(max_hp_str); } catch (...) {}
            }

            // The remaining 15 fields of the advanced block.
            for (int i = 0; i < 15; ++i) {
                parseNextToken(rest);
            }
            std::string amount_str = parseNextToken(rest);
            if (!amount_str.empty()) {
                try {
                    evt.amount = std::stoll(amount_str);
                } catch (...) {}
            }
        }
        // Heal events have the same layout, with the heal suffix after it.
        else if (evt.event_type.find("HEAL") != std::string::npos) {
            // InfoGUID, OwnerGUID
            parseNextToken(rest);
            parseNextToken(rest);

            // CurrentHP, MaxHP
            std::string current_hp_str = parseNextToken(rest);
            std::string max_hp_str = parseNextToken(rest);
            if (!current_hp_str.empty()) {
                try { evt.current_hp = std::stoll(current_hp_str); } catch (...) {}
            }
            if (!max_hp_str.empty()) {
                try { evt.max_hp = std::stoll(max_hp_str); } catch (...) {}
            }

            // The remaining 15 fields of the advanced block.
            for (int i = 0; i < 15; ++i) {
                parseNextToken(rest);
            }
            // The raw heal amount.
            std::string amount_str = parseNextToken(rest);
            if (!amount_str.empty()) {
                try {
                    evt.amount = std::stoll(amount_str);
                } catch (...) {}
            }
        }
    }
    // Melee has no spell; WCL reports it as abilityGameID 1.
    else if (evt.event_type.find("SWING") != std::string::npos) {
        evt.spell_id = 1;

        bool is_support = evt.event_type.find("SUPPORT") != std::string::npos;

        // SWING_DAMAGE_LANDED: base(8) + advanced(19) + damage suffix(10) = 37 tokens.
        // The _SUPPORT form adds the support buff's spell prefix (3) after the base
        // and a support field (1) at the end, 41 in all.
        if (evt.event_type.find("DAMAGE") != std::string::npos) {
            if (is_support) {
                parseNextToken(rest);
                parseNextToken(rest);
                parseNextToken(rest);
            }

            // InfoGUID, OwnerGUID
            parseNextToken(rest);
            parseNextToken(rest);

            // CurrentHP, MaxHP
            std::string current_hp_str = parseNextToken(rest);
            std::string max_hp_str = parseNextToken(rest);
            if (!current_hp_str.empty()) {
                try { evt.current_hp = std::stoll(current_hp_str); } catch (...) {}
            }
            if (!max_hp_str.empty()) {
                try { evt.max_hp = std::stoll(max_hp_str); } catch (...) {}
            }

            // The rest of the advanced block.
            for (int i = 0; i < 15; ++i) {
                parseNextToken(rest);
            }
            // The amount, the first field of the damage suffix.
            std::string amount_str = parseNextToken(rest);
            if (!amount_str.empty()) {
                try {
                    evt.amount = std::stoll(amount_str);
                } catch (...) {}
            }
        }
    }

    return evt;
}

int64_t FixtureGenerator::parseLocalTimestamp(const std::string& date, const std::string& time) {
    // date is MM/DD/YYYY, time HH:MM:SS.mmm-TZ.

    int month = 0, day = 0, year = 0;
    int hour = 0, minute = 0, second = 0, ms = 0;

    sscanf(date.c_str(), "%d/%d/%d", &month, &day, &year);

    size_t tz_pos = time.find('-');
    if (tz_pos == std::string::npos) tz_pos = time.find('+');

    std::string time_part = (tz_pos != std::string::npos)
        ? time.substr(0, tz_pos)
        : time;

    size_t dot_pos = time_part.find('.');
    if (dot_pos != std::string::npos) {
        sscanf(time_part.c_str(), "%d:%d:%d.%d", &hour, &minute, &second, &ms);
    } else {
        sscanf(time_part.c_str(), "%d:%d:%d", &hour, &minute, &second);
    }

    // Milliseconds since midnight plus a day term, enough for timing within
    // one encounter across a day boundary; month and year are ignored.
    int64_t total_ms = ((int64_t)hour * 3600 + (int64_t)minute * 60 + (int64_t)second) * 1000 + ms;

    total_ms += (int64_t)day * 86400000;

    return total_ms;
}

void FixtureGenerator::buildActorMapping() {
    // Local GUIDs are matched to WCL ids by actor name.

    std::unordered_map<int32_t, std::string> wcl_id_to_name;

    for (const auto& doc : wcl_pages_) {
        if (!doc.IsArray()) continue;

        for (rapidjson::SizeType i = 0; i < doc.Size(); ++i) {
            const auto& evt = doc[i];
            if (!evt.HasMember("type")) continue;

            std::string type = evt["type"].GetString();

            if (type == "combatantinfo") {
                if (evt.HasMember("sourceID") && evt["sourceID"].IsInt()) {
                    int32_t id = evt["sourceID"].GetInt();
                    wcl_id_to_name[id] = "Player_" + std::to_string(id);
                }
            }
        }
    }

    for (const auto& [guid, name] : guid_to_name_) {
        int32_t wcl_id = findWclActorByName(name);
        if (wcl_id > 0) {
            ActorMapping mapping;
            mapping.wcl_id = wcl_id;
            mapping.local_guid = guid;
            mapping.name = name;

            if (guid.find("Player-") == 0) {
                mapping.type = "Player";
            } else if (guid.find("Creature-") == 0) {
                mapping.type = "NPC";
            } else if (guid.find("Pet-") == 0) {
                mapping.type = "Pet";
            } else {
                mapping.type = "Unknown";
            }

            actor_mapping_[wcl_id] = mapping;
            name_to_wcl_id_[name] = wcl_id;
        }
    }

    std::cout << "Built " << actor_mapping_.size() << " actor mappings" << std::endl;
}

int32_t FixtureGenerator::findWclActorByName(const std::string& name) const {
    auto it = name_to_wcl_id_.find(name);
    if (it != name_to_wcl_id_.end()) {
        return it->second;
    }
    return -1;
}

std::string FixtureGenerator::findLocalGuidByWclId(int32_t wcl_id) const {
    auto it = actor_mapping_.find(wcl_id);
    if (it != actor_mapping_.end()) {
        return it->second.local_guid;
    }
    return "";
}

int32_t FixtureGenerator::getWclIdFromGuid(const std::string& guid) const {
    // guid -> name -> WCL id
    auto name_it = guid_to_name_.find(guid);
    if (name_it == guid_to_name_.end()) {
        return -1;
    }

    auto wcl_it = name_to_wcl_id_.find(name_it->second);
    if (wcl_it == name_to_wcl_id_.end()) {
        return -1;
    }

    return wcl_it->second;
}

bool FixtureGenerator::eventsMatch(
    const LocalEvent& local,
    const WclEvent& wcl,
    int32_t tolerance_ms
) {
    EventCategory local_cat = categorizeEventType(local.event_type);
    EventCategory wcl_cat = categorizeEventType(wcl.event_type);

    if (local_cat != wcl_cat) return false;

    // _SUPPORT events only match WCL events carrying supportID.
    if (local.is_support != wcl.has_support_id) return false;

    // When both sides have a spell id, they must agree.
    if (local.spell_id != 0 && wcl.spell_id != 0) {
        if (local.spell_id != wcl.spell_id) return false;
    }

    int32_t local_rel = local.timestamp_ms - static_cast<int32_t>(local_encounter_start_ms_);
    // WCL timestamps are already relative to the fight start.
    int32_t wcl_rel = wcl.timestamp_ms;

    if (std::abs(local_rel - wcl_rel) > tolerance_ms) return false;

    // Max HP is unique per player, which separates targets of the same AoE hit.
    if (local.max_hp > 0 && wcl.max_hp > 0) {
        if (local.max_hp != wcl.max_hp) {
            return false;
        }
    }

    // Amounts must agree within 1%, allowing for rounding in the raw log. This
    // is what separates simultaneous hits, melee in particular.
    if (local.amount > 0 && wcl.amount > 0) {
        double diff = std::abs(static_cast<double>(local.amount - wcl.amount));
        double larger = std::max(static_cast<double>(local.amount), static_cast<double>(wcl.amount));
        if (diff / larger > 0.01) return false;
    }

    return true;
}

std::vector<EventMatch> FixtureGenerator::matchEvents(int32_t tolerance_ms) {
    std::vector<EventMatch> matches;

    // WCL events indexed by category and spell id for faster matching.
    std::unordered_map<uint32_t, std::vector<size_t>> wcl_by_spell;
    for (size_t i = 0; i < wcl_events_.size(); ++i) {
        wcl_by_spell[wcl_events_[i].spell_id].push_back(i);
    }

    std::vector<bool> wcl_matched(wcl_events_.size(), false);

    for (const auto& local : local_events_) {
        if (local.spell_id == 0) continue;

        auto it = wcl_by_spell.find(local.spell_id);
        if (it == wcl_by_spell.end()) continue;

        size_t best_idx = SIZE_MAX;
        int32_t best_diff = INT32_MAX;

        for (size_t wcl_idx : it->second) {
            if (wcl_matched[wcl_idx]) continue;

            if (eventsMatch(local, wcl_events_[wcl_idx], tolerance_ms)) {
                int32_t local_rel = local.timestamp_ms - static_cast<int32_t>(local_encounter_start_ms_);
                int32_t diff = std::abs(local_rel - wcl_events_[wcl_idx].timestamp_ms);

                if (diff < best_diff) {
                    best_diff = diff;
                    best_idx = wcl_idx;
                }
            }
        }

        if (best_idx != SIZE_MAX) {
            wcl_matched[best_idx] = true;

            EventMatch match;
            match.local_line = local.raw_line;
            match.local_timestamp_ms = local.timestamp_ms - static_cast<int32_t>(local_encounter_start_ms_);
            match.wcl_timestamp_ms = wcl_events_[best_idx].timestamp_ms;
            match.local_event_type = local.event_type;
            match.wcl_event_type = wcl_events_[best_idx].event_type;
            match.spell_id = local.spell_id;

            EventCategory cat = categorizeEventType(local.event_type);
            match.event_type = categoryToDir(cat);

            match.wcl_page_index = wcl_events_[best_idx].page_index;
            match.wcl_event_index = wcl_events_[best_idx].event_index;

            const auto& doc = wcl_pages_[wcl_events_[best_idx].page_index];
            if (doc.IsArray() && wcl_events_[best_idx].event_index < doc.Size()) {
                rapidjson::StringBuffer buffer;
                rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
                doc[static_cast<rapidjson::SizeType>(wcl_events_[best_idx].event_index)].Accept(writer);
                match.wcl_event_json = buffer.GetString();
            }

            matches.push_back(std::move(match));
        }
    }

    std::cout << "Matched " << matches.size() << " event pairs" << std::endl;
    return matches;
}

std::vector<EventMatch> FixtureGenerator::sampleByType(
    const std::vector<EventMatch>& matches,
    size_t samples_per_type
) {
    // Grouped by exact local event type (SPELL_DAMAGE, SPELL_PERIODIC_DAMAGE, ...).
    std::unordered_map<std::string, std::vector<const EventMatch*>> by_type;

    for (const auto& match : matches) {
        by_type[match.local_event_type].push_back(&match);
    }

    std::vector<EventMatch> samples;

    for (auto& [type, type_matches] : by_type) {
        // Sorted by time so the samples spread across the fight.
        std::sort(type_matches.begin(), type_matches.end(),
            [](const EventMatch* a, const EventMatch* b) {
                return a->local_timestamp_ms < b->local_timestamp_ms;
            });

        size_t count = std::min(samples_per_type, type_matches.size());
        size_t step = type_matches.size() / count;

        for (size_t i = 0; i < count; ++i) {
            size_t idx = i * step;
            samples.push_back(*type_matches[idx]);
        }

        std::cout << "Sampled " << count << " " << type << " events" << std::endl;
    }

    return samples;
}

bool FixtureGenerator::exportFixtures(
    const std::string& output_dir,
    const std::vector<EventMatch>& samples
) {
    // Grouped by exact local event type.
    std::unordered_map<std::string, std::vector<const EventMatch*>> by_type;
    for (const auto& sample : samples) {
        by_type[sample.local_event_type].push_back(&sample);
    }

    for (const auto& [type, type_samples] : by_type) {
        std::string type_dir = output_dir + "/" + type;
        fs::create_directories(type_dir);

        std::string local_file = type_dir + "/samples.txt";
        std::ofstream local_out(local_file);
        if (!local_out.is_open()) {
            std::cerr << "Failed to create: " << local_file << std::endl;
            continue;
        }

        // The ENCOUNTER_START line goes first so OutputWriter has the same zero
        // point the WCL timestamps are relative to.
        if (!encounter_start_line_.empty()) {
            local_out << encounter_start_line_ << "\n";
        }

        for (const auto* sample : type_samples) {
            local_out << sample->local_line << "\n";
        }
        local_out.close();

        std::string wcl_file = type_dir + "/samples.json";
        std::ofstream wcl_out(wcl_file);
        if (!wcl_out.is_open()) {
            std::cerr << "Failed to create: " << wcl_file << std::endl;
            continue;
        }

        wcl_out << "[\n";
        bool first = true;
        for (const auto* sample : type_samples) {
            if (!first) wcl_out << ",\n";
            first = false;

            // Re-parsed so the file is pretty-printed.
            rapidjson::Document doc;
            doc.Parse(sample->wcl_event_json.c_str());
            if (!doc.HasParseError()) {
                rapidjson::StringBuffer buffer;
                rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(buffer);
                doc.Accept(writer);
                wcl_out << buffer.GetString();
            } else {
                wcl_out << sample->wcl_event_json;
            }
        }
        wcl_out << "\n]";
        wcl_out.close();

        std::cout << "Exported " << type_samples.size() << " samples to " << type_dir << std::endl;
    }

    std::string metadata_file = output_dir + "/metadata.json";
    std::ofstream meta_out(metadata_file);
    if (meta_out.is_open()) {
        rapidjson::Document meta;
        meta.SetObject();
        auto& alloc = meta.GetAllocator();

        meta.AddMember("fight_start_time_wcl",
            rapidjson::Value(wcl_fight_start_ms_), alloc);
        meta.AddMember("encounter_start_local_ms",
            rapidjson::Value(local_encounter_start_ms_), alloc);

        rapidjson::Value actor_map(rapidjson::kObjectType);
        for (const auto& [id, mapping] : actor_mapping_) {
            rapidjson::Value entry(rapidjson::kObjectType);
            entry.AddMember("wcl_id", rapidjson::Value(mapping.wcl_id), alloc);
            entry.AddMember("local_guid",
                rapidjson::Value(mapping.local_guid.c_str(), alloc), alloc);
            entry.AddMember("name",
                rapidjson::Value(mapping.name.c_str(), alloc), alloc);
            entry.AddMember("type",
                rapidjson::Value(mapping.type.c_str(), alloc), alloc);

            std::string key = std::to_string(id);
            actor_map.AddMember(
                rapidjson::Value(key.c_str(), alloc),
                entry,
                alloc
            );
        }
        meta.AddMember("actor_mapping", actor_map, alloc);

        rapidjson::StringBuffer buffer;
        rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(buffer);
        meta.Accept(writer);

        meta_out << buffer.GetString();
        meta_out.close();

        std::cout << "Exported metadata to " << metadata_file << std::endl;
    }

    return true;
}

} // namespace parity
