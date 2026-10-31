#include "PlayAlong/PlayAlongFeedbackSystem.h"
#include "../../Database/SpellCastDatabase.h"
#include <algorithm>

namespace PlayAlong {

void PlayAlongFeedbackSystem::initialize(const PlayAlongKeybindSettings& keybinds,
                                          const SpellCastDatabase* spellCastDb,
                                          const std::string& targetPlayerGuid) {
    keybinds_ = keybinds;
    spellCastDb_ = spellCastDb;
    targetPlayerGuid_ = targetPlayerGuid;

    reset();

    loadExpectedCasts();
}

void PlayAlongFeedbackSystem::reset() {
    expectedCasts_.clear();
    nextExpectedIndex_ = 0;
    pendingInputs_.clear();
    recentFeedback_.clear();
    allFeedback_.clear();
    totalGood_ = 0;
    totalBad_ = 0;
    totalMissed_ = 0;
    minTimeMs_ = 0;
    maxTimeMs_ = 0;
}

void PlayAlongFeedbackSystem::resetToTime(int32_t timeMs) {
    pendingInputs_.clear();
    recentFeedback_.clear();
    allFeedback_.clear();
    totalGood_ = 0;
    totalBad_ = 0;
    totalMissed_ = 0;

    for (auto& expected : expectedCasts_) {
        expected.matched = false;
        expected.matchedAtMs = 0;
    }

    // Find the correct nextExpectedIndex_ for this time
    // We want the first expected cast that hasn't passed yet
    nextExpectedIndex_ = 0;
    for (size_t i = 0; i < expectedCasts_.size(); ++i) {
        int32_t validEnd = expectedCasts_[i].timestampMs + LATE_WINDOW_MS;
        if (timeMs <= validEnd) {
            nextExpectedIndex_ = i;
            break;
        }
        nextExpectedIndex_ = i + 1;
    }
}

void PlayAlongFeedbackSystem::loadExpectedCasts() {
    if (!spellCastDb_) return;

    const auto& allCasts = spellCastDb_->getAllActorCasts();
    auto it = allCasts.find(targetPlayerGuid_);
    if (it == allCasts.end()) return;

    const auto& playerCasts = it->second;

    const auto& keybindMap = keybinds_.getAllKeybinds();

    for (const auto& cast : playerCasts) {
        // Skip melee attacks (spell ID 0 and 1)
        if (cast.spell_id == 0 || cast.spell_id == 1) continue;

        if (keybinds_.isSpellIgnored(cast.spell_id)) continue;

        if (keybindMap.find(cast.spell_id) == keybindMap.end()) continue;

        // Use start timestamp if available, otherwise end timestamp
        int32_t castTime = cast.start_timestamp_ms > 0 ? cast.start_timestamp_ms : cast.end_timestamp_ms;

        ExpectedCast expected;
        expected.spellId = cast.spell_id;
        expected.timestampMs = castTime;
        expected.matched = false;
        expected.matchedAtMs = 0;

        expectedCasts_.push_back(expected);
    }

    std::sort(expectedCasts_.begin(), expectedCasts_.end(),
              [](const ExpectedCast& a, const ExpectedCast& b) {
                  return a.timestampMs < b.timestampMs;
              });

    if (!expectedCasts_.empty()) {
        minTimeMs_ = expectedCasts_.front().timestampMs;
        maxTimeMs_ = expectedCasts_.back().timestampMs;
    }
}

void PlayAlongFeedbackSystem::recordPlayerInput(const Keybind& keybind, int32_t timestampMs) {
    PlayerInput input;
    input.keybind = keybind;
    input.timestampMs = timestampMs;
    input.processed = false;

    pendingInputs_.push_back(input);
}

void PlayAlongFeedbackSystem::update(int32_t currentTimeMs) {
    for (auto& input : pendingInputs_) {
        if (!input.processed) {
            processInput(input, currentTimeMs);
        }
    }

    while (!pendingInputs_.empty() && pendingInputs_.front().processed) {
        pendingInputs_.pop_front();
    }

    checkForMissedCasts(currentTimeMs);

    while (recentFeedback_.size() > MAX_RECENT_FEEDBACK) {
        recentFeedback_.pop_front();
    }
}

void PlayAlongFeedbackSystem::processInput(PlayerInput& input, int32_t currentTimeMs) {
    input.processed = true;

    for (size_t i = nextExpectedIndex_; i < expectedCasts_.size(); ++i) {
        ExpectedCast& expected = expectedCasts_[i];

        int32_t validStart = expected.timestampMs - spellQueueWindowMs_;
        int32_t validEnd = expected.timestampMs + LATE_WINDOW_MS;

        if (input.timestampMs >= validStart && input.timestampMs <= validEnd) {
            const Keybind& expectedKeybind = keybinds_.getKeybind(expected.spellId);
            if (input.keybind == expectedKeybind) {
                if (!expected.matched) {
                    expected.matched = true;
                    expected.matchedAtMs = input.timestampMs;

                    FeedbackEvent event;
                    event.spellId = expected.spellId;
                    event.playerPressMs = input.timestampMs;
                    event.expectedCastMs = expected.timestampMs;
                    event.deltaMs = input.timestampMs - expected.timestampMs;
                    event.result = FeedbackResult::Good;

                    recentFeedback_.push_back(event);
                    allFeedback_.push_back(event);
                    totalGood_++;
                    return;
                } else {
                    // Duplicate press for a cast that already matched; not counted as bad
                    return;
                }
            }
        }

        // Before this cast's window: it may be an early queue for a later cast on the same key
        if (input.timestampMs < validStart) {
            for (size_t j = i; j < expectedCasts_.size(); ++j) {
                ExpectedCast& futureExpected = expectedCasts_[j];
                int32_t futureValidStart = futureExpected.timestampMs - spellQueueWindowMs_;
                int32_t futureValidEnd = futureExpected.timestampMs + LATE_WINDOW_MS;

                if (input.timestampMs >= futureValidStart && input.timestampMs <= futureValidEnd) {
                    const Keybind& futureKeybind = keybinds_.getKeybind(futureExpected.spellId);
                    if (input.keybind == futureKeybind && !futureExpected.matched) {
                        futureExpected.matched = true;
                        futureExpected.matchedAtMs = input.timestampMs;

                        FeedbackEvent event;
                        event.spellId = futureExpected.spellId;
                        event.playerPressMs = input.timestampMs;
                        event.expectedCastMs = futureExpected.timestampMs;
                        event.deltaMs = input.timestampMs - futureExpected.timestampMs;
                        event.result = FeedbackResult::Good;

                        recentFeedback_.push_back(event);
                        allFeedback_.push_back(event);
                        totalGood_++;
                        return;
                    }
                }
            }
        }

        // If we've passed this expected cast's late window, continue to next
        if (currentTimeMs > validEnd && !expected.matched) {
            continue;
        }
    }

    // No match. Pick any spell on this key so the feedback has an icon.
    uint32_t displaySpellId = 0;
    for (const auto& [spellId, kb] : keybinds_.getAllKeybinds()) {
        if (kb == input.keybind) {
            displaySpellId = spellId;
            break;
        }
    }

    FeedbackEvent event;
    event.spellId = displaySpellId;  // Use spell ID for icon display
    event.playerPressMs = input.timestampMs;
    event.expectedCastMs = 0;
    event.deltaMs = 0;
    event.result = FeedbackResult::Bad;

    recentFeedback_.push_back(event);
    allFeedback_.push_back(event);
    totalBad_++;
}

void PlayAlongFeedbackSystem::checkForMissedCasts(int32_t currentTimeMs) {
    while (nextExpectedIndex_ < expectedCasts_.size()) {
        ExpectedCast& expected = expectedCasts_[nextExpectedIndex_];

        int32_t validEnd = expected.timestampMs + LATE_WINDOW_MS;

        // If we've passed the late window, this cast is finalized
        if (currentTimeMs > validEnd) {
            if (!expected.matched) {
                FeedbackEvent event;
                event.spellId = expected.spellId;
                event.playerPressMs = 0;  // No player press
                event.expectedCastMs = expected.timestampMs;
                event.deltaMs = 0;
                event.result = FeedbackResult::Bad;  // Missed counts as bad

                recentFeedback_.push_back(event);
                allFeedback_.push_back(event);
                totalMissed_++;
            }
            nextExpectedIndex_++;
        } else {
            // Haven't passed this cast's window yet, stop checking
            break;
        }
    }
}

std::vector<SectionScore> PlayAlongFeedbackSystem::getSectionScores() const {
    std::vector<SectionScore> sections;

    if (expectedCasts_.empty()) {
        return sections;
    }

    static constexpr int32_t SECTION_DURATION_MS = 15000;

    int32_t sectionStart = 0;
    while (sectionStart < maxTimeMs_) {
        SectionScore section;
        section.startMs = sectionStart;
        section.endMs = sectionStart + SECTION_DURATION_MS;

        for (const auto& event : allFeedback_) {
            int32_t eventTime = (event.expectedCastMs > 0) ? event.expectedCastMs : event.playerPressMs;
            if (eventTime >= section.startMs && eventTime < section.endMs) {
                switch (event.result) {
                    case FeedbackResult::Good:
                        section.goodCount++;
                        break;
                    case FeedbackResult::Bad:
                        if (event.playerPressMs == 0) {
                            section.missedCount++;  // Missed expected cast
                        } else {
                            section.badCount++;     // Wrong spell
                        }
                        break;
                    case FeedbackResult::Neutral:
                    case FeedbackResult::Pending:
                        break;
                }
            }
        }

        sections.push_back(section);
        sectionStart += SECTION_DURATION_MS;
    }

    return sections;
}

float PlayAlongFeedbackSystem::getOverallAccuracy() const {
    uint32_t total = totalGood_ + totalBad_ + totalMissed_;
    if (total == 0) return 1.0f;
    return static_cast<float>(totalGood_) / static_cast<float>(total);
}

} // namespace PlayAlong
