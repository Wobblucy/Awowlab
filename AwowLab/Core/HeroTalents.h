#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace awow {

// Hero talent names, generated from the client data by
// tools/db2_extractor/build_hero_talents.py and shipped in data/.
//
// WarcraftLogs reports every talent a character took as trait node entry ids
// and never says which are hero talents, so this is what turns an imported
// fight into "Arms, Colossus" rather than just "Arms". Hero choice changes
// rotation, buffs and cooldown usage, so a baseline that ignores it compares
// two different playstyles.
class HeroTalents {
public:
    static HeroTalents& instance();

    HeroTalents(const HeroTalents&) = delete;
    HeroTalents& operator=(const HeroTalents&) = delete;

    /**
     * Read the generated file. Safe to call more than once; later calls are
     * ignored once a load has succeeded.
     *
     * @param dataDir Directory holding hero_talents_{version}.json.
     * @return True when trees were loaded.
     */
    bool load(const std::string& dataDir);

    /**
     * Name the hero tree a build took, from the talent ids WCL reported.
     *
     * A character has exactly one, so the first id that resolves answers for
     * the whole build. Returns empty when none of the ids is a hero talent,
     * which is always the case for a local log: it does not report talents
     * this way.
     */
    std::string treeForTalents(const std::vector<uint32_t>& talentEntryIds) const;

    /**
     * The trees a spec has been seen using, for building a picker.
     *
     * This comes from observed characters rather than from the client tables,
     * which group hero trees by class rather than by spec. A spec with no
     * entry here means no cached character had played it when the file was
     * generated, not that it has no hero trees.
     */
    const std::vector<std::string>& treesForSpec(uint16_t specId) const;

    /** Every tree name in the file, sorted. */
    const std::vector<std::string>& allTrees() const { return allTrees_; }

    bool isLoaded() const { return loaded_; }

private:
    HeroTalents() = default;

    std::unordered_map<uint32_t, std::string> entryToTree_;
    std::unordered_map<uint16_t, std::vector<std::string>> specToTrees_;
    std::vector<std::string> allTrees_;
    bool loaded_ = false;
};

} // namespace awow
