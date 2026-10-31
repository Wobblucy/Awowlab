#include "LiveCombatStats.h"

LiveCombatStats::LiveCombatStats() {
    combatDb_ = std::make_unique<CombatDatabase>();
    dispelDb_ = std::make_unique<DispelDatabase>();
    deathDb_ = std::make_unique<DeathDatabase>();
    resourceDb_ = std::make_unique<ResourceDatabase>();
    auraDb_ = std::make_unique<AuraDatabase>();
    absorbDb_ = std::make_unique<AbsorbDatabase>();
    avoidanceDb_ = std::make_unique<AvoidanceDatabase>();
}

LiveCombatStats::~LiveCombatStats() = default;

void LiveCombatStats::attachSession(LiveLogSession* session) {
    session_ = session;
    sessionStartTime_ms_ = 0;
    dungeonStartTime_ms_ = 0;
    selectedPullIndex_ = SIZE_MAX;
    refresh();
}

void LiveCombatStats::detachSession() {
    session_ = nullptr;
    combatDb_ = std::make_unique<CombatDatabase>();
    dispelDb_ = std::make_unique<DispelDatabase>();
    deathDb_ = std::make_unique<DeathDatabase>();
    resourceDb_ = std::make_unique<ResourceDatabase>();
    auraDb_ = std::make_unique<AuraDatabase>();
    absorbDb_ = std::make_unique<AbsorbDatabase>();
    avoidanceDb_ = std::make_unique<AvoidanceDatabase>();
    selectedPullIndex_ = SIZE_MAX;
}

void LiveCombatStats::refresh() {
    if (!session_) return;

    // Rebuilds every database from the bundle on display: live data for
    // Current, or the re-parsed bundle for a past segment or an M+ Overall.
    // The session getters already return whichever one is shown.
    //
    // The SPELL_SUMMON lineage lets summoned Creature/Vehicle pets (a Death
    // Knight's army, say) merge into their player even when their events
    // carry no owner in the advanced block.
    auto summonPetToOwner = session_->getSummonPetToOwnerMap();
    combatDb_->loadFromActorMap(&session_->getActorMap(), &summonPetToOwner);

    // ResourceDatabase reads the same ActorMap, so only its pointer changes.
    // The death recap's HP timeline comes from resource_status_table.
    resourceDb_->loadFromActorMap(&session_->getActorMap());

    // The event streams are copied, not moved, because the session keeps
    // appending to them. The copy is small next to the ranking queries.
    absorbDb_->loadFromEvents(session_->getAbsorbEvents());
    dispelDb_->loadFromEvents(session_->getDispelEvents());
    avoidanceDb_->loadFromEvents(session_->getMissedEvents());

    // The live session does not parse SPELL_RESURRECT, so no resurrects
    static const std::vector<ResurrectEvent> kEmptyResurrects;
    deathDb_->loadEvents(session_->getDeathEvents(), kEmptyResurrects);

    // loadFromEvents takes an rvalue, so hand it a copy
    std::vector<AuraEvent> auraCopy = session_->getAuraEvents();
    auraDb_->loadFromEvents(std::move(auraCopy));
}

std::vector<ActorCombatStats> LiveCombatStats::getDamageRanked(
    StatsViewMode mode,
    size_t limit
) const {
    if (!combatDb_ || combatDb_->empty()) {
        return {};
    }

    auto [startTime, endTime] = getTimeRange(mode);

    return combatDb_->getRankedByActorWithPets(
        CombatMetricType::DamageDealt,
        startTime,
        endTime,
        limit,
        false  // No spell breakdowns for overlay (too expensive)
    );
}

std::vector<ActorCombatStats> LiveCombatStats::getHealingRanked(
    StatsViewMode mode,
    size_t limit
) const {
    if (!combatDb_ || combatDb_->empty()) {
        return {};
    }

    auto [startTime, endTime] = getTimeRange(mode);

    return combatDb_->getRankedByActorWithPets(
        CombatMetricType::HealingDone,
        startTime,
        endTime,
        limit,
        false
    );
}

std::vector<ActorCombatStats> LiveCombatStats::getDamageTakenRanked(
    StatsViewMode mode,
    size_t limit
) const {
    if (!combatDb_ || combatDb_->empty()) {
        return {};
    }

    auto [startTime, endTime] = getTimeRange(mode);

    return combatDb_->getRankedByDamageTakenWithPets(
        startTime,
        endTime,
        limit,
        false
    );
}

std::vector<ActorDispelStats> LiveCombatStats::getInterruptsRanked(
    StatsViewMode mode,
    size_t limit
) const {
    if (!dispelDb_ || dispelDb_->empty()) {
        return {};
    }

    auto [startTime, endTime] = getTimeRange(mode);

    return dispelDb_->getRankedByActor(
        startTime,
        endTime,
        limit,
        false,
        DispelDatabase::FilterType::InterruptsOnly
    );
}

std::vector<ActorDispelStats> LiveCombatStats::getDispelsRanked(
    StatsViewMode mode,
    size_t limit
) const {
    if (!dispelDb_ || dispelDb_->empty()) {
        return {};
    }

    auto [startTime, endTime] = getTimeRange(mode);

    return dispelDb_->getRankedByActor(
        startTime,
        endTime,
        limit,
        false,
        DispelDatabase::FilterType::DispelsOnly
    );
}

void LiveCombatStats::launchAsyncSelect(std::function<void()> work) {
    // The parse and refresh run on a detached worker. selectMutex_ keeps two
    // parses from overlapping, and selectInFlight_ stops rapid clicks from
    // queueing a thread each.
    if (selectInFlight_.exchange(true)) {
        // A worker is already queued; the next click picks up the latest
        // selectedPullIndex_
        return;
    }
    std::thread([this, w = std::move(work)]() mutable {
        std::lock_guard<std::mutex> guard(selectMutex_);
        // Keep parsingInProgress_ raised through refresh() as well as the
        // parse, so the render thread never reads a half-rebuilt database
        // such as petToOwnerMap_.
        if (session_) session_->beginExternalParse();
        w();
        if (session_) session_->endExternalParse();
        selectInFlight_.store(false);
    }).detach();
}

void LiveCombatStats::selectHistoricalPull(size_t pullIndex) {
    // An index, since a pointer would dangle when the vector grows. Set
    // before the parse so readers see the new selection straight away.
    selectedPullIndex_ = pullIndex;
    if (!session_) return;

    launchAsyncSelect([this, pullIndex]() {
        if (!session_) return;
        auto snapshot = session_->getSnapshot();
        if (pullIndex >= snapshot.pullHistory.size()) return;
        const auto& segment = snapshot.pullHistory[pullIndex];
        if (session_->parseSegment(segment)) {
            refresh();
        }
    });
}

void LiveCombatStats::selectOverallView() {
    if (!session_) return;

    launchAsyncSelect([this]() {
        if (!session_) return;
        auto snapshot = session_->getSnapshot();
        uint32_t currentRunId = session_->getCurrentDungeonRunId();

        std::vector<PullSegment> segments;
        for (const auto& pull : snapshot.pullHistory) {
            if (pull.dungeonRunId == currentRunId) {
                segments.push_back(pull);
            }
        }

        // Include current pull if it belongs to this dungeon
        if (snapshot.currentPull.dungeonRunId == currentRunId &&
            snapshot.currentPull.startByteOffset > 0) {
            // An in-progress pull ends at the current file position
            PullSegment current = snapshot.currentPull;
            if (current.endByteOffset == 0) {
                current.endByteOffset = session_->getCurrentFileSize();
            }
            segments.push_back(current);
        }

        if (!segments.empty()) {
            if (session_->parseSegments(segments)) {
                refresh();
            }
        }
    });
}

void LiveCombatStats::clearHistoricalPullSelection() {
    selectedPullIndex_ = SIZE_MAX;

    if (session_) {
        session_->returnToLiveMode();
        refresh();
    }
}

std::pair<int32_t, int32_t> LiveCombatStats::getTimeRange(StatsViewMode mode) const {
    if (!session_) {
        return {0, INT32_MAX};
    }

    auto snapshot = session_->getSnapshot();

    switch (mode) {
        case StatsViewMode::CurrentPull: {
            // The top "live" option is a sliding view over the currently
            // active combat segment:
            //
            //  - Between bosses, currentPull_ is the trash segment for the
            //    whole gap, with window [trashStart, now].
            //
            //  - During a boss, currentPull_ is the boss segment. Boss
            //    records don't appear in the log until the kill flushes
            //    them, so there's a stretch after ENCOUNTER_START where
            //    the boss window holds no data yet. For that stretch we
            //    fall back to session-so-far (min..max over everything
            //    the combatDb currently holds - the preceding trash,
            //    which startNewPull left in place) so the meter does not
            //    go blank. Once the boss's records land the view moves
            //    to the boss window.
            const PullSegment* pull = &snapshot.currentPull;

            // Don't flip to a brand-new segment for the first few seconds.
            // Every boss pull and trash block opens a fresh segment, and
            // snapping the live meter to it the instant it starts makes the
            // numbers blink to near-empty. While the active segment is
            // younger than this, keep showing the previous one; the switch
            // happens once the new segment has some substance.
            constexpr int32_t kMinSegmentAgeMs = 3000;
            if (combatDb_ && !snapshot.pullHistory.empty()) {
                int32_t segAge = combatDb_->getMaxTimestamp() - pull->startTime_ms;
                if (segAge >= 0 && segAge < kMinSegmentAgeMs) {
                    return getPullTimeRange(&snapshot.pullHistory.back());
                }
            }

            if (pull->isEncounter && combatDb_) {
                // Have the boss records been flushed? Parsing is in time
                // order, so the combatDb's max timestamp only reaches the
                // boss window after the kill burst is parsed; until then it
                // sits on the trash before the boss.
                bool bossDataFlushed =
                    combatDb_->getMaxTimestamp() >= pull->startTime_ms;
                if (!bossDataFlushed) {
                    // Session-so-far: everything currently loaded. An
                    // empty combatDb reports min as UINT32_MAX (-1 as
                    // int32) and max as 0, so guard on empty() and hand
                    // back a wide-open window rather than that sentinel
                    // pair.
                    if (combatDb_->empty()) {
                        return {0, INT32_MAX};
                    }
                    return {combatDb_->getMinTimestamp(),
                            combatDb_->getMaxTimestamp()};
                }
                // Boss data present: use the boss window.
                // getPullTimeRange keeps the in-progress end at INT32_MAX
                // so late interrupts/deaths just past the last damage
                // event aren't truncated (each render clamps further).
            }

            return getPullTimeRange(pull);
        }

        case StatsViewMode::HistoricalPull: {
            // parseSegment rebases timestamps to the segment start, so the
            // window is [0, duration], not the log-relative times stored in
            // the PullSegment, which would match no records. CurrentPull
            // stays log-relative.
            if (selectedPullIndex_ != SIZE_MAX && selectedPullIndex_ < snapshot.pullHistory.size()) {
                const auto& pull = snapshot.pullHistory[selectedPullIndex_];
                int32_t duration = pull.getDurationMs();
                if (duration <= 0) {
                    return {0, 0};
                }
                return {0, duration};
            }
            // No historical pull selected: use the current one
            return getPullTimeRange(&snapshot.currentPull);
        }

        case StatsViewMode::CurrentDungeon: {
            // From dungeon start to now
            int32_t startTime = session_ ? session_->getDungeonStartTime() : 0;
            int32_t endTime = combatDb_ ? combatDb_->getMaxTimestamp() : INT32_MAX;
            return {startTime, endTime};
        }

        case StatsViewMode::TrashOverall: {
            auto ranges = getTrashPullTimeRanges();
            if (ranges.empty()) {
                return {0, 0};
            }
            int32_t minStart = ranges.front().first;
            int32_t maxEnd = ranges.front().second;
            for (const auto& [start, end] : ranges) {
                minStart = std::min(minStart, start);
                maxEnd = std::max(maxEnd, end);
            }
            return {minStart, maxEnd};
        }

        case StatsViewMode::SessionTotal: {
            // All data
            int32_t startTime = combatDb_ ? combatDb_->getMinTimestamp() : 0;
            int32_t endTime = combatDb_ ? combatDb_->getMaxTimestamp() : INT32_MAX;
            return {startTime, endTime};
        }
    }

    return {0, INT32_MAX};
}

std::pair<int32_t, int32_t> LiveCombatStats::getPullTimeRange(const PullSegment* pull) const {
    if (!pull) {
        return {0, INT32_MAX};
    }

    int32_t startTime = pull->startTime_ms;
    // An in-progress pull ends at INT32_MAX rather than the combatDb's
    // max timestamp. Interrupts, dispels, deaths and CC breaks are in other
    // databases and often land just after the last damage event, so
    // clamping here would drop them. calculateTimeWindow clamps each meter
    // to its own database later.
    int32_t endTime = pull->isInProgress()
        ? INT32_MAX
        : pull->endTime_ms;

    return {startTime, endTime};
}

float LiveCombatStats::getDurationSeconds(StatsViewMode mode) const {
    auto [startTime, endTime] = getTimeRange(mode);
    if (endTime <= startTime) return 0.0f;
    return static_cast<float>(endTime - startTime) / 1000.0f;
}

std::string LiveCombatStats::getCurrentSegmentName() const {
    if (!session_) return "";

    auto snapshot = session_->getSnapshot();

    if (selectedPullIndex_ != SIZE_MAX && selectedPullIndex_ < snapshot.pullHistory.size()) {
        return snapshot.pullHistory[selectedPullIndex_].label;
    }

    // The live label is currentPull_'s: the trash block between bosses, or
    // the boss name. While the meter shows the session fallback window
    // (boss started, records not flushed yet) it already reads the boss
    // name, since that is what is being fought.
    return snapshot.currentPull.label;
}

bool LiveCombatStats::isInEncounter() const {
    if (!session_) return false;
    auto snapshot = session_->getSnapshot();
    return snapshot.inEncounter;
}

bool LiveCombatStats::isInMythicPlus() const {
    if (!session_) return false;
    auto snapshot = session_->getSnapshot();
    return snapshot.inMythicPlus;
}

std::vector<std::pair<int32_t, int32_t>> LiveCombatStats::getTrashPullTimeRanges() const {
    std::vector<std::pair<int32_t, int32_t>> ranges;
    if (!session_) return ranges;

    auto snapshot = session_->getSnapshot();
    uint32_t currentRunId = session_->getCurrentDungeonRunId();

    for (const auto& pull : snapshot.pullHistory) {
        if (pull.dungeonRunId == currentRunId &&
            pull.segmentType == PullSegmentType::TrashPull) {
            ranges.emplace_back(pull.startTime_ms, pull.endTime_ms);
        }
    }

    // The current pull counts if it is trash in this dungeon
    if (snapshot.currentPull.segmentType == PullSegmentType::TrashPull &&
        snapshot.currentPull.dungeonRunId == currentRunId) {
        int32_t endTime = snapshot.currentPull.isInProgress()
            ? (combatDb_ ? combatDb_->getMaxTimestamp() : INT32_MAX)
            : snapshot.currentPull.endTime_ms;
        ranges.emplace_back(snapshot.currentPull.startTime_ms, endTime);
    }

    return ranges;
}
