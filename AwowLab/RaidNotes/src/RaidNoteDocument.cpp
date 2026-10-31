#include "RaidNoteDocument.h"
#include "RaidNoteEntry.h"
#include <algorithm>
#include <random>
#include <sstream>
#include <iomanip>
#include <chrono>

#include <rapidjson/document.h>
#include <rapidjson/writer.h>
#include <rapidjson/stringbuffer.h>

namespace RaidNotes {

namespace {

// Hex timestamp plus a random four-digit suffix
std::string makeUniqueId(const char* prefix) {
    auto now = std::chrono::high_resolution_clock::now();
    auto nanos = std::chrono::duration_cast<std::chrono::nanoseconds>(
        now.time_since_epoch()).count();

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 0xFFFF);

    std::ostringstream oss;
    oss << prefix << std::hex << nanos << "-" << std::setw(4) << std::setfill('0') << dis(gen);
    return oss.str();
}

} // namespace

std::string RaidNoteEntry::generateId() {
    return makeUniqueId("");
}

std::vector<int32_t> RaidNoteEntry::getEffectiveTimestamps() const {
    if (is_template && !instance_timestamps.empty()) {
        return instance_timestamps;
    }
    return { timestamp_ms };
}

RaidNoteGroup RaidNoteGroup::create(RaidNoteGroupType type) {
    RaidNoteGroup group;
    group.type = type;
    group.display_name = getDefaultDisplayName(type);
    return group;
}

std::string RaidNoteGroup::getDefaultDisplayName(RaidNoteGroupType type) {
    switch (type) {
        case RaidNoteGroupType::BossCasts:
            return "-- Boss Abilities --";
        case RaidNoteGroupType::PlayerCasts:
            return "-- Player Cooldowns --";
        case RaidNoteGroupType::Manual:
            return "-- Notes --";
        default:
            return "-- Other --";
    }
}

void RaidNoteGroup::sortByTimestamp() {
    std::sort(entries.begin(), entries.end(),
        [](const RaidNoteEntry& a, const RaidNoteEntry& b) {
            return a.timestamp_ms < b.timestamp_ms;
        });
}

RaidNoteEntry* RaidNoteGroup::findEntry(const std::string& entryId) {
    for (auto& entry : entries) {
        if (entry.id == entryId) {
            return &entry;
        }
    }
    return nullptr;
}

const RaidNoteEntry* RaidNoteGroup::findEntry(const std::string& entryId) const {
    for (const auto& entry : entries) {
        if (entry.id == entryId) {
            return &entry;
        }
    }
    return nullptr;
}

bool RaidNoteGroup::hasEnabledEntries() const {
    return std::any_of(entries.begin(), entries.end(),
        [](const RaidNoteEntry& e) { return e.enabled; });
}

RaidNoteDocument::RaidNoteDocument() {
    // Always these three groups, in this order
    groups_.push_back(RaidNoteGroup::create(RaidNoteGroupType::BossCasts));
    groups_.push_back(RaidNoteGroup::create(RaidNoteGroupType::PlayerCasts));
    groups_.push_back(RaidNoteGroup::create(RaidNoteGroupType::Manual));
}

RaidNoteGroup* RaidNoteDocument::getGroup(RaidNoteGroupType type) {
    for (auto& group : groups_) {
        if (group.type == type) {
            return &group;
        }
    }
    return nullptr;
}

const RaidNoteGroup* RaidNoteDocument::getGroup(RaidNoteGroupType type) const {
    for (const auto& group : groups_) {
        if (group.type == type) {
            return &group;
        }
    }
    return nullptr;
}

void RaidNoteDocument::addEntry(RaidNoteGroupType groupType, RaidNoteEntry entry) {
    if (entry.id.empty()) {
        entry.id = RaidNoteEntry::generateId();
    }

    auto* group = getGroup(groupType);
    if (group) {
        group->entries.push_back(std::move(entry));
    }
}

void RaidNoteDocument::removeEntry(const std::string& entryId) {
    for (auto& group : groups_) {
        auto it = std::remove_if(group.entries.begin(), group.entries.end(),
            [&entryId](const RaidNoteEntry& e) { return e.id == entryId; });
        if (it != group.entries.end()) {
            group.entries.erase(it, group.entries.end());
            return;
        }
    }
}

void RaidNoteDocument::updateEntry(const std::string& entryId, const RaidNoteEntry& updated) {
    auto* entry = findEntry(entryId);
    if (entry) {
        *entry = updated;
        entry->id = entryId;
    }
}

void RaidNoteDocument::toggleEntry(const std::string& entryId, bool enabled) {
    auto* entry = findEntry(entryId);
    if (entry) {
        entry->enabled = enabled;
    }
}

std::string PhaseDelay::generateId() {
    return makeUniqueId("pd-");
}

void RaidNoteDocument::addPhaseDelay(int32_t afterMs, int32_t delayMs) {
    PhaseDelay delay;
    delay.id = PhaseDelay::generateId();
    delay.after_ms = afterMs;
    delay.delay_ms = delayMs;
    phaseDelays_.push_back(delay);
}

void RaidNoteDocument::removePhaseDelay(const std::string& delayId) {
    auto it = std::remove_if(phaseDelays_.begin(), phaseDelays_.end(),
        [&delayId](const PhaseDelay& d) { return d.id == delayId; });
    phaseDelays_.erase(it, phaseDelays_.end());
}

void RaidNoteDocument::updatePhaseDelay(const std::string& delayId, int32_t afterMs, int32_t delayMs) {
    for (auto& delay : phaseDelays_) {
        if (delay.id == delayId) {
            delay.after_ms = afterMs;
            delay.delay_ms = delayMs;
            return;
        }
    }
}

int32_t RaidNoteDocument::getAdjustedTimestamp(int32_t original_ms) const {
    // Each delay is tested against the original time, so the order they are
    // applied in doesn't change the result.
    int32_t adjusted = original_ms;
    for (const auto& delay : phaseDelays_) {
        if (original_ms >= delay.after_ms) {
            adjusted += delay.delay_ms;
        }
    }
    return adjusted;
}

void RaidNoteDocument::clear() {
    for (auto& group : groups_) {
        group.entries.clear();
    }
    phaseDelays_.clear();
    globalOffsetMs_ = 0;
}

bool RaidNoteDocument::hasEntry(uint32_t spellId, int32_t timestamp_ms) const {
    for (const auto& group : groups_) {
        for (const auto& entry : group.entries) {
            if (entry.spell_id == spellId && entry.timestamp_ms == timestamp_ms) {
                return true;
            }
        }
    }
    return false;
}

bool RaidNoteDocument::isEmpty() const {
    for (const auto& group : groups_) {
        if (!group.entries.empty()) {
            return false;
        }
    }
    return true;
}

size_t RaidNoteDocument::getEntryCount() const {
    size_t count = 0;
    for (const auto& group : groups_) {
        count += group.entries.size();
    }
    return count;
}

RaidNoteEntry* RaidNoteDocument::findEntry(const std::string& entryId) {
    for (auto& group : groups_) {
        auto* entry = group.findEntry(entryId);
        if (entry) {
            return entry;
        }
    }
    return nullptr;
}

const RaidNoteEntry* RaidNoteDocument::findEntry(const std::string& entryId) const {
    for (const auto& group : groups_) {
        auto* entry = group.findEntry(entryId);
        if (entry) {
            return entry;
        }
    }
    return nullptr;
}

std::string RaidNoteDocument::toJson() const {
    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);

    writer.StartObject();

    writer.Key("globalOffset");
    writer.Int(globalOffsetMs_);

    writer.Key("groups");
    writer.StartArray();

    for (const auto& group : groups_) {
        writer.StartObject();

        writer.Key("type");
        writer.Int(static_cast<int>(group.type));

        writer.Key("displayName");
        writer.String(group.display_name.c_str());

        writer.Key("collapsed");
        writer.Bool(group.collapsed);

        writer.Key("entries");
        writer.StartArray();

        for (const auto& entry : group.entries) {
            writer.StartObject();

            writer.Key("id");
            writer.String(entry.id.c_str());

            writer.Key("spellId");
            writer.Uint(entry.spell_id);

            writer.Key("spellName");
            writer.String(entry.spell_name.c_str());

            writer.Key("timestampMs");
            writer.Int(entry.timestamp_ms);

            writer.Key("customText");
            writer.String(entry.custom_text.c_str());

            writer.Key("enabled");
            writer.Bool(entry.enabled);

            writer.Key("isTemplate");
            writer.Bool(entry.is_template);

            if (entry.is_template && !entry.instance_timestamps.empty()) {
                writer.Key("instanceTimestamps");
                writer.StartArray();
                for (int32_t ts : entry.instance_timestamps) {
                    writer.Int(ts);
                }
                writer.EndArray();
            }

            writer.EndObject();
        }

        writer.EndArray();
        writer.EndObject();
    }

    writer.EndArray();

    writer.Key("phaseDelays");
    writer.StartArray();
    for (const auto& delay : phaseDelays_) {
        writer.StartObject();
        writer.Key("id");
        writer.String(delay.id.c_str());
        writer.Key("afterMs");
        writer.Int(delay.after_ms);
        writer.Key("delayMs");
        writer.Int(delay.delay_ms);
        writer.EndObject();
    }
    writer.EndArray();

    writer.EndObject();

    return buffer.GetString();
}

std::optional<RaidNoteDocument> RaidNoteDocument::fromJson(const std::string& json) {
    rapidjson::Document doc;
    doc.Parse(json.c_str());

    if (doc.HasParseError() || !doc.IsObject()) {
        return std::nullopt;
    }

    RaidNoteDocument result;

    if (doc.HasMember("globalOffset") && doc["globalOffset"].IsInt()) {
        result.globalOffsetMs_ = doc["globalOffset"].GetInt();
    }

    if (doc.HasMember("groups") && doc["groups"].IsArray()) {
        const auto& groups = doc["groups"].GetArray();

        for (const auto& groupVal : groups) {
            if (!groupVal.IsObject()) continue;

            int typeInt = (groupVal.HasMember("type") && groupVal["type"].IsInt())
                ? groupVal["type"].GetInt() : 0;
            auto groupType = static_cast<RaidNoteGroupType>(typeInt);

            auto* group = result.getGroup(groupType);
            if (!group) continue;

            if (groupVal.HasMember("displayName") && groupVal["displayName"].IsString()) {
                group->display_name = groupVal["displayName"].GetString();
            }

            if (groupVal.HasMember("collapsed") && groupVal["collapsed"].IsBool()) {
                group->collapsed = groupVal["collapsed"].GetBool();
            }

            if (groupVal.HasMember("entries") && groupVal["entries"].IsArray()) {
                const auto& entries = groupVal["entries"].GetArray();

                for (const auto& entryVal : entries) {
                    if (!entryVal.IsObject()) continue;

                    RaidNoteEntry entry;

                    if (entryVal.HasMember("id") && entryVal["id"].IsString()) {
                        entry.id = entryVal["id"].GetString();
                    }
                    if (entryVal.HasMember("spellId") && entryVal["spellId"].IsUint()) {
                        entry.spell_id = entryVal["spellId"].GetUint();
                    }
                    if (entryVal.HasMember("spellName") && entryVal["spellName"].IsString()) {
                        entry.spell_name = entryVal["spellName"].GetString();
                    }
                    if (entryVal.HasMember("timestampMs") && entryVal["timestampMs"].IsInt()) {
                        entry.timestamp_ms = entryVal["timestampMs"].GetInt();
                    }
                    if (entryVal.HasMember("customText") && entryVal["customText"].IsString()) {
                        entry.custom_text = entryVal["customText"].GetString();
                    }
                    if (entryVal.HasMember("enabled") && entryVal["enabled"].IsBool()) {
                        entry.enabled = entryVal["enabled"].GetBool();
                    }
                    if (entryVal.HasMember("isTemplate") && entryVal["isTemplate"].IsBool()) {
                        entry.is_template = entryVal["isTemplate"].GetBool();
                    }

                    if (entryVal.HasMember("instanceTimestamps") && entryVal["instanceTimestamps"].IsArray()) {
                        const auto& timestamps = entryVal["instanceTimestamps"].GetArray();
                        for (const auto& ts : timestamps) {
                            if (ts.IsInt()) {
                                entry.instance_timestamps.push_back(ts.GetInt());
                            }
                        }
                    }

                    group->entries.push_back(std::move(entry));
                }
            }
        }
    }

    if (doc.HasMember("phaseDelays") && doc["phaseDelays"].IsArray()) {
        const auto& delays = doc["phaseDelays"].GetArray();
        for (const auto& delayVal : delays) {
            if (!delayVal.IsObject()) continue;

            PhaseDelay delay;
            if (delayVal.HasMember("id") && delayVal["id"].IsString()) {
                delay.id = delayVal["id"].GetString();
            }
            if (delayVal.HasMember("afterMs") && delayVal["afterMs"].IsInt()) {
                delay.after_ms = delayVal["afterMs"].GetInt();
            }
            if (delayVal.HasMember("delayMs") && delayVal["delayMs"].IsInt()) {
                delay.delay_ms = delayVal["delayMs"].GetInt();
            }

            if (delay.id.empty()) {
                delay.id = PhaseDelay::generateId();
            }

            result.phaseDelays_.push_back(std::move(delay));
        }
    }

    return result;
}

} // namespace RaidNotes
