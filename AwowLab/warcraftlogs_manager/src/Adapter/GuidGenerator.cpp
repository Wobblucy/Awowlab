#include "Adapter/GuidGenerator.h"

#include <charconv>
#include <sstream>

namespace warcraftlogs {

WclActorType parseActorType(const std::string& typeStr) noexcept {
    if (typeStr == "Player") return WclActorType::Player;
    if (typeStr == "NPC") return WclActorType::NPC;
    if (typeStr == "Pet") return WclActorType::Pet;
    if (typeStr == "Guardian") return WclActorType::Guardian;
    if (typeStr == "Boss") return WclActorType::Boss;
    return WclActorType::Unknown;
}

const char* getGuidPrefix(WclActorType type) noexcept {
    switch (type) {
        case WclActorType::Player:   return "Player";
        case WclActorType::NPC:      return "Creature";
        case WclActorType::Pet:      return "Pet";
        case WclActorType::Guardian: return "Pet";  // Guardians use Pet prefix in WoW
        case WclActorType::Boss:     return "Creature";
        case WclActorType::Unknown:
        default:                     return "Unknown";
    }
}

std::string GuidGenerator::generate(int32_t actorId, WclActorType actorType) {
    // {Type}-0-0-0-0-{ActorID}. WarcraftLogs gives no realm or server ids, so
    // those fields are zero.
    std::ostringstream oss;
    oss << getGuidPrefix(actorType) << "-0-0-0-0-" << actorId;
    return oss.str();
}

std::string GuidGenerator::generate(int32_t actorId, const std::string& actorTypeStr) {
    return generate(actorId, parseActorType(actorTypeStr));
}

bool GuidGenerator::parse(
    const std::string& guid,
    int32_t& outActorId,
    WclActorType& outActorType
) noexcept {
    // Synthetic GUIDs look like {Type}-0-0-0-0-{ID}.

    size_t firstDash = guid.find('-');
    if (firstDash == std::string::npos) {
        return false;
    }

    std::string typePrefix = guid.substr(0, firstDash);

    if (typePrefix == "Player") {
        outActorType = WclActorType::Player;
    } else if (typePrefix == "Creature") {
        outActorType = WclActorType::NPC;
    } else if (typePrefix == "Pet") {
        outActorType = WclActorType::Pet;
    } else if (typePrefix == "Unknown") {
        outActorType = WclActorType::Unknown;
    } else {
        return false;
    }

    size_t lastDash = guid.rfind('-');
    if (lastDash == std::string::npos || lastDash == firstDash) {
        return false;
    }

    std::string idStr = guid.substr(lastDash + 1);

    auto [ptr, ec] = std::from_chars(
        idStr.data(),
        idStr.data() + idStr.size(),
        outActorId
    );

    return ec == std::errc{} && ptr == idStr.data() + idStr.size();
}

bool GuidGenerator::isSyntheticGuid(const std::string& guid) noexcept {
    // Synthetic GUIDs contain -0-0-0-0-; the shortest is "X-0-0-0-0-1".

    if (guid.length() < 11) {
        return false;
    }

    return guid.find("-0-0-0-0-") != std::string::npos;
}

int32_t GuidGenerator::extractActorId(const std::string& guid) noexcept {
    int32_t actorId = -1;
    WclActorType actorType;

    if (parse(guid, actorId, actorType)) {
        return actorId;
    }

    return -1;
}

std::string ActorIdMapper::registerActor(
    int32_t actorId,
    WclActorType actorType,
    const std::string& actorName
) {
    auto it = m_idToInfo.find(actorId);
    if (it != m_idToInfo.end()) {
        return it->second.guid;
    }

    std::string guid = GuidGenerator::generate(actorId, actorType);

    ActorInfo info;
    info.guid = guid;
    info.name = actorName;
    info.type = actorType;

    m_idToInfo[actorId] = info;
    m_guidToId[guid] = actorId;

    return guid;
}

std::string ActorIdMapper::getGuid(int32_t actorId) const noexcept {
    auto it = m_idToInfo.find(actorId);
    if (it != m_idToInfo.end()) {
        return it->second.guid;
    }
    return "";
}

int32_t ActorIdMapper::getActorId(const std::string& guid) const noexcept {
    auto it = m_guidToId.find(guid);
    if (it != m_guidToId.end()) {
        return it->second;
    }
    return -1;
}

std::string ActorIdMapper::getActorName(int32_t actorId) const noexcept {
    auto it = m_idToInfo.find(actorId);
    if (it != m_idToInfo.end()) {
        return it->second.name;
    }
    return "";
}

WclActorType ActorIdMapper::getActorType(int32_t actorId) const noexcept {
    auto it = m_idToInfo.find(actorId);
    if (it != m_idToInfo.end()) {
        return it->second.type;
    }
    return WclActorType::Unknown;
}

void ActorIdMapper::clear() noexcept {
    m_idToInfo.clear();
    m_guidToId.clear();
}

size_t ActorIdMapper::size() const noexcept {
    return m_idToInfo.size();
}

} // namespace warcraftlogs
