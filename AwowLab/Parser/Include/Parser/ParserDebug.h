#pragma once
#include <iostream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <set>
#include <vector>
#include <algorithm>

namespace parser {

// PARSER_DEBUG calls are left in the parsers for tracing a single event type;
// define the macro to print its message when needed.
#define PARSER_DEBUG(eventName, msg) ((void)0)

#ifndef NDEBUG
namespace debug {

    // Token counts seen per event type
    inline std::unordered_map<std::string, std::set<size_t>> observedTokenCounts;

    inline void recordTokenCount(std::string_view eventType, size_t count) {
        observedTokenCounts[std::string(eventType)].insert(count);
    }

    struct ParseFailureSample {
        std::string eventType;
        std::string actorType;   // Player, Creature, Vehicle, Pet, Nil, Unknown
        size_t tokenCount;
        std::string rawTokens;   // First 15 tokens, comma-joined
    };

    // eventType -> actorType -> first failing line seen, so each kind of
    // short line is printed once rather than on every occurrence
    inline std::unordered_map<std::string,
        std::unordered_map<std::string, ParseFailureSample>> parseFailures;

    inline std::string getActorTypeFromGUID(std::string_view guid) {
        if (guid.size() >= 7 && guid.substr(0, 7) == "Player-") return "Player";
        if (guid.size() >= 9 && guid.substr(0, 9) == "Creature-") return "Creature";
        if (guid.size() >= 8 && guid.substr(0, 8) == "Vehicle-") return "Vehicle";
        if (guid.size() >= 4 && guid.substr(0, 4) == "Pet-") return "Pet";
        if (guid == "0000000000000000" || guid == "nil") return "Nil";
        return "Unknown";
    }

    inline void recordParseFailure(
        std::string_view eventType,
        std::string_view sourceGUID,
        size_t tokenCount,
        const std::vector<std::string_view>& tokens
    ) {
        std::string eventStr(eventType);
        std::string actorType = getActorTypeFromGUID(sourceGUID);

        auto& eventFailures = parseFailures[eventStr];
        if (eventFailures.find(actorType) != eventFailures.end()) {
            return;
        }

        ParseFailureSample sample;
        sample.eventType = eventStr;
        sample.actorType = actorType;
        sample.tokenCount = tokenCount;

        std::string rawTokens;
        size_t limit = std::min(tokens.size(), size_t(15));
        for (size_t i = 0; i < limit; ++i) {
            if (i > 0) rawTokens += ",";
            rawTokens += tokens[i];
        }
        if (tokens.size() > 15) {
            rawTokens += "...";
        }
        sample.rawTokens = rawTokens.substr(0, 300);

        std::cout << "[PARSE_FAIL] " << eventStr
                  << " [" << actorType << "] tokens=" << tokenCount << "\n"
                  << "  " << sample.rawTokens << "\n";

        eventFailures[actorType] = std::move(sample);
    }

    inline std::unordered_map<std::string, size_t> unhandledEventCounts;

    inline void recordUnhandledEvent(std::string_view eventType) {
        unhandledEventCounts[std::string(eventType)]++;
    }

} // namespace debug
#endif // NDEBUG

} // namespace parser
