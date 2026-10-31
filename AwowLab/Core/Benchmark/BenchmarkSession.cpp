#include "Core/Benchmark/BenchmarkSession.h"

#include "Core/Benchmark/BenchmarkStore.h"
#include "Core/MobWeightSettings.h"
#include "Core/Benchmark/ClassificationSuggester.h"
#include "Core/Benchmark/LivePullBridge.h"
#include "Core/PhaseSettings.h"
#include "Core/UnifiedSettings.h"
#include "Parser/SpecializationColors.h"
#include "Core/LocalizationManager.h"
#include "Core/SpellNameDatabase.h"
#include "Core/ErrorLogger.h"

#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

#include <algorithm>
#include <charconv>
#include <unordered_set>

namespace benchmark {

namespace {

// Pull the confirmed window aura and fallback duration for each major
// cooldown, so the extractor measures a window that follows the real buff.
void fillCooldownWindows(const SpecClassification& spec,
                         ExtractionRequest& request) {
    for (uint32_t spellId : request.majorCooldownSpellIds) {
        const auto* cls = spec.findSpell(spellId);
        if (!cls) continue;

        if (const uint32_t windowAura = spec.windowAuraFor(spellId)) {
            request.cooldownWindowAuras[spellId] = windowAura;
        }

        if (cls->assumedWindowMs > 0) {
            request.cooldownWindowMs[spellId] = cls->assumedWindowMs;
        }
    }
}

} // namespace

BenchmarkSession& BenchmarkSession::instance() {
    static BenchmarkSession session;
    return session;
}

ExtractionRequest BenchmarkSession::buildRequest(const std::string& actorGuid,
                                                 const std::string& playerName,
                                                 uint16_t specId,
                                                 const std::string& sourceLabel,
                                                 uint32_t encounterId,
                                                 uint32_t difficulty) const {
    ExtractionRequest request;
    request.actorGuid = actorGuid;
    request.playerName = playerName;
    request.specId = specId;
    request.sourceLabel = sourceLabel;
    request.encounterId = encounterId;
    request.difficulty = difficulty;

    // Both sides are cut with the same rules, taken live rather than stored,
    // so a change to the user's phase splits shows up as a mismatch the
    // comparison can report instead of silently misaligning the windows.
    if (const auto* rules = PhaseSettings::instance().rulesFor(encounterId)) {
        request.phaseRules = *rules;
    }

    const auto* spec = ClassificationStore::instance().find(specId);
    if (spec) {
        request.majorCooldownSpellIds = spec->majorCooldownSpellIds();
        request.buffOnlyCooldownIds = spec->buffOnlyCooldownIds();
        request.burstWindowSpellIds = spec->burstWindowSpellIds();
        request.defensiveSpellIds = spec->defensiveSpellIds();
        request.channelAuraIds = spec->channelAuraIds();
        request.ignoredSpellIds = spec->ignoredSpellIds();
        request.ignoredAuraIds = spec->ignoredAuraIds();
        request.offGlobalSpellIds = spec->offGlobalSpellIds();
        fillCooldownWindows(*spec, request);
    }

    return request;
}

bool BenchmarkSession::recutMentor(const ExtractedData& data,
                                   std::string actorGuid) {
    // Identity is carried over rather than re-derived. The user is changing
    // what a spell MEANS, not which pull is the reference, and re-resolving
    // the actor from a freshly loaded log risks landing on a different player
    // with the same spec. No suggestion pass either: everything here is
    // already ruled on, and re-suggesting would put confirmed spells back in
    // the queue.
    const MentorProfile::Origin origin = mentor_.origin;
    const uint16_t specId = mentor_.specId;

    const ExtractionRequest request = buildRequest(actorGuid, mentor_.playerName,
                                                   specId, mentor_.sourceLabel,
                                                   mentor_.encounterId,
                                                   mentor_.difficulty);
    mentor_ = extractProfile(data, request);
    mentor_.specId = specId;
    mentor_.origin = origin;
    if (!mentor_.isValid()) return false;

    saveMentorsFile();
    return true;
}

bool BenchmarkSession::refreshMentorAfterReview(const ExtractedData& data) {
    if (!mentor_.isValid()) return false;

    // Only when a ruling actually changed what extraction would produce.
    // Re-cutting on every review close would be wasted work in the common
    // case where the user confirmed the suggestions as they stood.
    if (!needsRebuild()) return false;

    if (!recutMentor(data, mentor_.actorGuid)) return false;
    refreshStage();
    return true;
}

bool BenchmarkSession::rebuildMentorFrom(const ExtractedData& data) {
    if (!mentor_.isValid()) return false;

    if (!recutMentor(data, mentor_.origin.actorGuid)) return false;

    // The user's side was cut under the same old rulings, so it is dropped
    // too and the card falls back to "pick a pull".
    user_ = MentorProfile();
    result_ = BenchmarkResult();
    refreshStage();
    return true;
}

size_t BenchmarkSession::setMentor(const ExtractedData& data,
                                   const std::string& actorGuid,
                                   const std::string& playerName,
                                   uint16_t specId,
                                   const std::string& specName,
                                   const std::string& sourceLabel,
                                   uint32_t encounterId,
                                   uint32_t difficulty) {
    clear();

    // Pull length first: the suggester reads cadence as casts per minute, so
    // it needs the window before it can say anything.
    int32_t pullEnd = 0;
    for (const auto& cast : data.spellCastEvents) {
        pullEnd = std::max(pullEnd, cast.end_timestamp_ms);
    }
    for (const auto& combat : data.combatEvents) {
        pullEnd = std::max(pullEnd, combat.timestamp_ms);
    }

    // Suggest classifications before extracting, because the extraction's
    // burst/filler split depends on what counts as a major cooldown.
    // Anything already ruled Ignored stays out of the suggestions, so a spell
    // the user dismissed does not come back asking to be classified again.
    std::unordered_set<uint32_t> alreadyIgnoredSpells;
    std::unordered_set<uint32_t> alreadyIgnoredAuras;
    if (const auto* existing = ClassificationStore::instance().find(specId)) {
        const auto spellIds = existing->ignoredSpellIds();
        const auto auraIds = existing->ignoredAuraIds();
        alreadyIgnoredSpells.insert(spellIds.begin(), spellIds.end());
        alreadyIgnoredAuras.insert(auraIds.begin(), auraIds.end());
    }

    const SuggesterInput input = buildSuggesterInput(data, actorGuid, specId,
                                                     specName, pullEnd,
                                                     alreadyIgnoredSpells,
                                                     alreadyIgnoredAuras);
    SpecClassification suggested = suggestClassifications(input);
    suggested.specId = specId;
    suggested.specName = specName;

    auto& store = ClassificationStore::instance();
    const size_t pending = store.mergeSuggestions(suggested);

    // Built after the merge above, so the request sees the freshly suggested
    // rulings rather than whatever the store held before this import.
    const ExtractionRequest request = buildRequest(actorGuid, playerName, specId,
                                                   sourceLabel, encounterId,
                                                   difficulty);

    mentor_ = extractProfile(data, request);
    mentor_.specId = specId;
    // Where this reference can be found again, taken from whatever
    // setLoadedSource recorded when the log was opened. A later ruling change
    // re-cuts the same pull from here instead of sending the user back
    // through the picker.
    mentor_.origin = loadedOrigin(actorGuid);

    // The log's own names first, then the shipped spell data for the rest.
    // Imported reports carry almost no names, so without the fallback the
    // whole classification list reads as bare ids.
    for (const auto& [id, name] : data.spellNameMap) {
        if (!name.empty()) spellNames_[id] = name;
    }
    resolveSpellNames(data);
    resolveNpcNames(data);

    // Persist immediately: choosing a reference is deliberate work, and live
    // mode on a later night needs it to still be there.
    saveMentorsFile();

    refreshStage();
    return pending;
}

const BenchmarkResult& BenchmarkSession::compareAgainst(const ExtractedData& data,
                                                        const std::string& actorGuid,
                                                        const std::string& playerName) {
    userGuid_ = actorGuid;
    userName_ = playerName;
    userDeathMs_ = findDeathTime(data, actorGuid);

    ExtractionRequest request = buildRequest(actorGuid, playerName,
                                             mentor_.specId, std::string(),
                                             mentor_.encounterId,
                                             mentor_.difficulty);
    user_ = extractProfile(data, request);
    user_.specId = mentor_.specId;
    user_.playerName = playerName;

    for (const auto& [id, name] : data.spellNameMap) {
        if (!name.empty()) spellNames_.try_emplace(id, name);
    }
    resolveSpellNames(data);
    resolveNpcNames(data);

    recompare();
    return result_;
}

std::vector<RosterEntry> BenchmarkSession::rosterForPull(
    const std::filesystem::path& logPath,
    size_t startByteOffset,
    size_t endByteOffset) const {

    return readRoster(logPath, startByteOffset, endByteOffset);
}

bool BenchmarkSession::compareLivePull(const std::filesystem::path& logPath,
                                       size_t startByteOffset,
                                       size_t endByteOffset,
                                       uint32_t encounterId,
                                       const std::string& selfName) {
    liveResultReady_ = false;

    // The reference was chosen when live mode started, so it is already
    // active. All this checks is that it is for the boss that just wiped -
    // comparing against a reference for a different encounter would be
    // rejected by the engine anyway, but only after doing all the work.
    if (!mentor_.isValid()) {
        AWOW_LOG_WARNING("BENCHMARK", "No reference active for this pull");
        return false;
    }
    if (mentor_.encounterId != encounterId) {
        AWOW_LOG_INFO("BENCHMARK",
            "Pull was encounter " + std::to_string(encounterId) +
            ", reference is for " + std::to_string(mentor_.encounterId));
        return false;
    }

    LivePullEvents pull;
    extractLivePull(pull, logPath, startByteOffset, endByteOffset);
    if (!pull.valid) {
        AWOW_LOG_WARNING("BENCHMARK",
            "Could not re-read the pull from the log (bytes " +
            std::to_string(startByteOffset) + "-" +
            std::to_string(endByteOffset) + ")");
        return false;
    }

    // Live logs give no "this is you" marker, so the user's character name is
    // matched against the actors seen in the pull. Names are unique within one
    // log, which is all that matters here.
    std::string selfGuid;
    std::string resolvedName = selfName;

    if (!selfName.empty()) {
        for (const auto& [guid, name] : pull.data.guidToNameMap) {
            if (name == selfName) {
                selfGuid = guid;
                break;
            }
        }
    }

    if (selfGuid.empty()) {
        // Whoever owns this session has to say who they are. The caller shows
        // the pull's roster and calls setSelfPlayerName, then the next pull
        // resolves here.
        AWOW_LOG_INFO("BENCHMARK",
            selfName.empty()
                ? "No character name set yet - the roster needs picking from"
                : "\"" + selfName + "\" was not in this pull");
        return false;
    }

    compareAgainst(pull.data, selfGuid, resolvedName);

    liveResultReady_ = result_.valid;
    return result_.valid;
}

bool BenchmarkSession::recompare() {
    ComparisonRequest request;
    request.user = &user_;
    request.mentor = &mentor_;
    request.classification = ClassificationStore::instance().find(mentor_.specId);
    request.userDeathMs = userDeathMs_;

    result_ = compare(request);
    refreshStage();
    return result_.valid;
}

bool BenchmarkSession::recompareAfterRulingChange() {
    // userGuid_ can name an actor from a live pull that is not the data
    // currently loaded; re-extracting the wrong log would produce an empty
    // side that looks like a valid report.
    if (liveData_ && !userGuid_.empty() &&
        liveData_->guidToNameMap.count(userGuid_) > 0) {
        compareAgainst(*liveData_, userGuid_, userName_);
        return result_.valid;
    }
    return recompare();
}

bool BenchmarkSession::needsRebuild() const {
    const auto* spec = ClassificationStore::instance().find(mentor_.specId);
    if (!spec) return false;

    // The stored split only means what a fresh one would if the same spells
    // still define burst. Comparing across two different definitions would
    // look fine and be wrong.
    if (spec->majorCooldownSpellIds() != mentor_.majorCooldownSpellIds) {
        return true;
    }

    // Marking a spell Ignored changes the numbers just as much: a profile cut
    // before that ruling still counts its casts, so it would be measured
    // against a fresh side that does not.
    if (spec->ignoredSpellIds() != mentor_.ignoredSpellIds) return true;

    // Same again for off-global rulings: the globals count is built during
    // extraction, so a cantrip ruling made afterwards does not reach it.
    if (spec->offGlobalSpellIds() != mentor_.offGlobalSpellIds) return true;

    // Blacklisting a buff is applied during extraction, so a ruling made after
    // the profile was cut never reaches the windows it was meant to clean up.
    if (spec->ignoredAuraIds() != mentor_.ignoredAuraIds) return true;

    // Where the burst windows ARE. Ruling a trinket a cooldown, or a buff a
    // cooldown in its own right, moves every window in the pull - and unlike
    // the checks above this one is easy to miss, because the profile still
    // looks complete and simply has no window where the user just said one
    // should be.
    if (spec->burstWindowSpellIds() != mentor_.burstWindowSpellIds) return true;
    if (spec->buffOnlyCooldownIds() != mentor_.buffOnlyCooldownIds) return true;

    // Mob weights shape every damage figure in the profile, so changing one
    // makes the stored numbers describe a weighting that no longer applies.
    return MobWeightSettings::instance().fingerprint() != mentor_.mobWeightFingerprint;
}

void BenchmarkSession::refreshStage() {
    if (!mentor_.isValid()) {
        stage_ = SessionStage::Empty;
        return;
    }

    const auto* spec = ClassificationStore::instance().find(mentor_.specId);
    if (!spec || !spec->fullyConfirmed()) {
        stage_ = SessionStage::NeedsClassification;
        return;
    }

    stage_ = result_.valid ? SessionStage::Compared : SessionStage::Ready;
}

void BenchmarkSession::clear() {
    mentor_ = MentorProfile();
    user_ = MentorProfile();
    result_ = BenchmarkResult();
    stage_ = SessionStage::Empty;
    userGuid_.clear();
    userName_.clear();
    userDeathMs_ = -1;
    spellNames_.clear();
    npcNames_.clear();
    // savedMentors_ deliberately survives: this ends the active comparison,
    // it does not throw away references the user chose on other nights.
}

std::string BenchmarkSession::mentorToJson() const {
    return mentor_.isValid() ? mentor_.toJson() : std::string();
}

bool BenchmarkSession::loadMentorFromJson(const std::string& json) {
    auto restored = MentorProfile::fromJson(json);
    if (!restored) return false;

    mentor_ = std::move(*restored);
    user_ = MentorProfile();
    result_ = BenchmarkResult();
    refreshStage();
    return true;
}

void BenchmarkSession::resolveNpcNames(const ExtractedData& data) {
    // The log knows each actor by GUID and each GUID by name; the profile
    // records damage against creature ids. This is the one place both halves
    // are in hand at once, so the join happens here.
    for (const auto& [guid, name] : data.guidToNameMap) {
        if (name.empty()) continue;
        const uint32_t npcId = resolveNpcIdForGuid(guid, data.actorIdToNpcId);
        if (npcId == 0) continue;
        npcNames_.emplace(npcId, name);
    }
}

void BenchmarkSession::resolveSpellNames(const ExtractedData& data) {
    auto& names = SpellNameDatabase::instance();

    // The name database loads on demand, and the ids it holds are whatever
    // some earlier caller asked for. Preparing a reference skips the encounter
    // loader that normally does this, so without asking here every lookup
    // falls through to the "Spell 12345" placeholder.
    std::unordered_set<uint32_t> needed;
    for (const auto& cast : data.spellCastEvents) needed.insert(cast.spell_id);
    for (const auto& aura : data.auraEvents)      needed.insert(aura.spell_id);
    for (const auto& event : data.combatEvents)   needed.insert(event.spell_id);
    needed.erase(0);

    // Auto-attacks are synthesized with spell id 1, which in the game's own
    // data is an unrelated retired spell. Looking it up yields "Word of Recall
    // (OLD)" wherever the log did not supply a name of its own, so the id is
    // named here and kept out of the lookup entirely.
    needed.erase(kMeleeSpellId);
    if (spellNames_[kMeleeSpellId].empty()) {
        spellNames_[kMeleeSpellId] = L("benchmark.melee");
    }
    if (!needed.empty()) {
        names.loadSpellIds(needed);
    }

    // Every id the classification list can show. Auras are included because
    // the Buffs tab names them the same way.
    const auto remember = [&](uint32_t spellId) {
        if (spellId == 0) return;
        auto existing = spellNames_.find(spellId);
        if (existing != spellNames_.end() && !existing->second.empty()) return;

        if (const char* name = names.getSpellName(spellId);
            name && name[0] != '\0') {
            spellNames_[spellId] = name;
        }
    };

    for (const auto& cast : data.spellCastEvents) remember(cast.spell_id);
    for (const auto& aura : data.auraEvents)      remember(aura.spell_id);
    for (const auto& event : data.combatEvents)   remember(event.spell_id);
}

std::string BenchmarkSession::mentorKeyString(const MentorKey& key) {
    return std::to_string(key.encounterId) + ":" + std::to_string(key.specId);
}

std::optional<BenchmarkSession::MentorKey>
BenchmarkSession::mentorKeyFromString(std::string_view text) {
    const size_t colon = text.find(':');
    if (colon == std::string_view::npos) return std::nullopt;

    MentorKey key;
    const auto encounterText = text.substr(0, colon);
    const auto specText = text.substr(colon + 1);

    std::from_chars(encounterText.data(),
                    encounterText.data() + encounterText.size(), key.encounterId);
    uint32_t spec = 0;
    std::from_chars(specText.data(), specText.data() + specText.size(), spec);
    key.specId = static_cast<uint16_t>(spec);

    if (key.encounterId == 0) return std::nullopt;
    return key;
}

void BenchmarkSession::saveMentorsFile() const {
    if (!mentor_.isValid() || mentor_.encounterId == 0) return;

    // Rebuild the whole map each save. One reference changing is rare, and
    // writing it wholesale keeps the stored form and the in-memory one from
    // drifting apart.
    auto snapshot = savedMentors_;
    snapshot[mentorKeyString({mentor_.encounterId, mentor_.specId})] =
        mentor_.toJson();

    rapidjson::Document doc;
    doc.SetObject();
    auto& alloc = doc.GetAllocator();
    for (const auto& [key, json] : snapshot) {
        doc.AddMember(rapidjson::Value(key.c_str(), alloc),
                      rapidjson::Value(json.c_str(), alloc), alloc);
    }

    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    doc.Accept(writer);

    // A plain file rather than the encrypted settings, so a reference can be
    // shared or deleted without touching credentials.
    store::writeFile(store::mentorsPath(), buffer.GetString());

    const_cast<BenchmarkSession*>(this)->savedMentors_ = std::move(snapshot);
}

void BenchmarkSession::loadSavedMentors() {
    savedMentors_.clear();

    auto& values = SettingsCache::instance().get().extraValues;

    if (auto self = values.find("benchmarkSelfPlayer"); self != values.end()) {
        selfPlayerName_ = self->second;
    }

    std::string mentorsJson = store::readFile(store::mentorsPath());
    if (mentorsJson.empty()) {
        if (auto legacy = values.find("benchmarkMentors");
            legacy != values.end() && !legacy->second.empty()) {
            mentorsJson = legacy->second;
            store::writeFile(store::mentorsPath(), mentorsJson);
        }
    }
    if (mentorsJson.empty()) return;

    rapidjson::Document doc;
    if (doc.Parse(mentorsJson.c_str()).HasParseError() || !doc.IsObject()) {
        return;
    }

    for (auto m = doc.MemberBegin(); m != doc.MemberEnd(); ++m) {
        if (!m->value.IsString()) continue;

        const std::string_view rawKey(m->name.GetString(), m->name.GetStringLength());
        std::string json(m->value.GetString(), m->value.GetStringLength());

        // Entries written before references were keyed by spec carry a bare
        // encounter id. Read the spec back out of the profile so they survive
        // rather than being dropped on upgrade.
        if (auto key = mentorKeyFromString(rawKey)) {
            savedMentors_[mentorKeyString(*key)] = std::move(json);
            continue;
        }

        uint32_t encounterId = 0;
        std::from_chars(rawKey.data(), rawKey.data() + rawKey.size(), encounterId);
        if (encounterId == 0) continue;

        uint16_t specId = 0;
        if (auto profile = MentorProfile::fromJson(json)) {
            specId = profile->specId;
        }
        savedMentors_[mentorKeyString({encounterId, specId})] = std::move(json);
    }
}

void BenchmarkSession::setSelfPlayerName(const std::string& name) {
    selfPlayerName_ = name;

    auto& cache = SettingsCache::instance();
    cache.get().extraValues["benchmarkSelfPlayer"] = name;
    cache.markDirty();
}

std::vector<BenchmarkSession::MentorEntry> BenchmarkSession::storedMentors() const {
    std::vector<MentorEntry> entries;
    entries.reserve(savedMentors_.size());

    for (const auto& [keyText, json] : savedMentors_) {
        const auto key = mentorKeyFromString(keyText);
        if (!key) continue;

        // Parsing every stored profile to list them is affordable because the
        // list is small and only built when a picker opens.
        const auto profile = MentorProfile::fromJson(json);
        if (!profile) continue;

        MentorEntry entry;
        entry.key = *key;
        entry.playerName = profile->playerName;
        entry.sourceLabel = profile->sourceLabel;
        entry.pullDurationMs = profile->pullDurationMs;
        entry.kill = profile->kill;
        entry.specName = parser::getSpecName(profile->specId);
        entries.push_back(std::move(entry));
    }

    std::sort(entries.begin(), entries.end(),
              [](const MentorEntry& a, const MentorEntry& b) {
                  if (a.key.encounterId != b.key.encounterId) {
                      return a.key.encounterId < b.key.encounterId;
                  }
                  return a.key.specId < b.key.specId;
              });
    return entries;
}

std::vector<BenchmarkSession::MentorEntry>
BenchmarkSession::mentorsForEncounter(uint32_t encounterId) const {
    std::vector<MentorEntry> matching;
    for (auto& entry : storedMentors()) {
        if (entry.key.encounterId == encounterId) {
            matching.push_back(std::move(entry));
        }
    }
    return matching;
}

bool BenchmarkSession::hasMentorFor(uint32_t encounterId) const {
    for (const auto& [keyText, json] : savedMentors_) {
        const auto key = mentorKeyFromString(keyText);
        if (key && key->encounterId == encounterId) return true;
    }
    return false;
}

void BenchmarkSession::forgetMentor(const MentorKey& key) {
    // Deleting the reference currently loaded has to clear it from memory too,
    // or the comparison keeps running against something the user just removed
    // and it reappears on the next save.
    if (mentor_.isValid() && mentor_.encounterId == key.encounterId &&
        mentor_.specId == key.specId) {
        mentor_ = MentorProfile();
        user_ = MentorProfile();
        result_ = BenchmarkResult();
        refreshStage();
    }

    if (savedMentors_.erase(mentorKeyString(key)) == 0) return;

    rapidjson::Document doc;
    doc.SetObject();
    auto& alloc = doc.GetAllocator();
    for (const auto& [keyText, json] : savedMentors_) {
        doc.AddMember(rapidjson::Value(keyText.c_str(), alloc),
                      rapidjson::Value(json.c_str(), alloc), alloc);
    }

    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    doc.Accept(writer);

    store::writeFile(store::mentorsPath(), buffer.GetString());
}

bool BenchmarkSession::activateMentor(const MentorKey& key) {
    if (mentor_.isValid() && mentor_.encounterId == key.encounterId &&
        mentor_.specId == key.specId) {
        return true;   // Already active
    }

    auto it = savedMentors_.find(mentorKeyString(key));
    if (it == savedMentors_.end()) return false;

    return loadMentorFromJson(it->second);
}

} // namespace benchmark
