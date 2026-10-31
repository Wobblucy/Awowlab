#include "UI/UIActorSelector.h"
#include "Color/ActorColorGenerator.h"
#include "../../Database/OutputWriter.h"  // For ActorType enum

void UIActorSelector::updateKnownPlayers(const std::shared_ptr<ActorColorGenerator>& colorGen,
                                          const std::unordered_map<std::string, std::string>& allPlayers) {
    m_allKnownPlayers.clear();

    for (const auto& [guid, name] : allPlayers) {
        const ActorType* typePtr = colorGen->getActorType(guid);
        if (typePtr && *typePtr == ActorType::Player) {
            PlayerInfo info;
            info.guid = guid;
            info.name = name;
            info.color = colorGen->getCachedColor(guid);
            info.is_active = false;  // Will be updated during render

            // Use name as key for alphabetical sorting
            m_allKnownPlayers[name] = info;
        }
    }
}

std::optional<std::string> UIActorSelector::getSelectedActor() const {
    return m_selectedActorGuid;
}

bool UIActorSelector::isActorSelected(const std::string& guid) const {
    return m_selectedActorGuid.has_value() && m_selectedActorGuid.value() == guid;
}

void UIActorSelector::clearSelection() {
    m_selectedActorGuid.reset();
}

void UIActorSelector::setSelectedActor(const std::string& guid) {
    m_selectedActorGuid = guid;
}
