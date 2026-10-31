#include "../OutputWriter.h"
#include "Core/ErrorLogger.h"
#include <fstream>

static const char* actorTypeToString(ActorType type) {
    switch (type) {
        case ActorType::Player: return "Player";
        case ActorType::Creature: return "Creature";
        case ActorType::Vehicle: return "Vehicle";
        case ActorType::Pet: return "Pet";
        default: return "Unknown";
    }
}

static const char* reactionToString(UnitReaction reaction) {
    switch (reaction) {
        case UnitReaction::Friendly: return "Friendly";
        case UnitReaction::Neutral: return "Neutral";
        case UnitReaction::Hostile: return "Hostile";
        default: return "Unknown";
    }
}

bool OutputWriter::writeToCSV(
    const std::vector<ActorEvent>& events,
    const std::string& filename
) {
    std::ofstream outfile(filename);
    if (!outfile.is_open()) {
        AWOW_LOG_INFO("APP", "Could not open " + filename + " for writing");
        return false;
    }

    outfile << "SourceGUID,SourceActor,ActorType,Reaction,Flags,Timestamp_ms,X_Location,Y_Location\n";

    for (const auto& event : events) {
        outfile << event.source_guid << ","
                << event.source_actor << ","
                << actorTypeToString(event.actor_type) << ","
                << reactionToString(event.flags.reaction) << ","
                << "0x" << std::hex << event.flags.raw << std::dec << ","
                << event.timestamp_ms << ","
                << event.x_loc << ","
                << event.y_loc << "\n";
    }

    return true;
}
