#include "HeroTalents.h"

#include "ErrorLogger.h"

#include <rapidjson/document.h>

#include <algorithm>
#include <filesystem>
#include <fstream>

namespace awow {
namespace {

const std::vector<std::string>& emptyTrees() {
    static const std::vector<std::string> empty;
    return empty;
}

} // namespace

HeroTalents& HeroTalents::instance() {
    static HeroTalents instance;
    return instance;
}

bool HeroTalents::load(const std::string& dataDir) {
    if (loaded_) return true;

    // The filename carries the patch it came from. Taking the highest one
    // present means a build shipped alongside two patches' data uses the newer,
    // and nothing here has to know which patch the app itself targets.
    std::vector<std::filesystem::path> candidates;

    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(dataDir, ec)) {
        const auto name = entry.path().filename().string();
        if (name.rfind("hero_talents_", 0) == 0 && entry.path().extension() == ".json") {
            candidates.push_back(entry.path());
        }
    }
    std::sort(candidates.begin(), candidates.end(),
              [](const auto& a, const auto& b) { return a.filename() > b.filename(); });

    for (const auto& path : candidates) {
        std::ifstream in(path, std::ios::binary);
        if (!in.is_open()) continue;

        std::string text((std::istreambuf_iterator<char>(in)),
                         std::istreambuf_iterator<char>());

        rapidjson::Document doc;
        doc.Parse(text.c_str());
        if (doc.HasParseError() || !doc.IsObject()) {
            AWOW_LOG_WARNING("HERO", "Could not parse " + path.string());
            continue;
        }

        if (doc.HasMember("entries") && doc["entries"].IsObject()) {
            for (auto it = doc["entries"].MemberBegin();
                 it != doc["entries"].MemberEnd(); ++it) {
                if (!it->value.IsString()) continue;
                try {
                    entryToTree_.emplace(
                        static_cast<uint32_t>(std::stoul(it->name.GetString())),
                        it->value.GetString());
                } catch (const std::exception&) {
                    // A key that is not a number is a malformed row, not a
                    // reason to abandon the rest of the file
                }
            }
        }

        if (doc.HasMember("specs") && doc["specs"].IsObject()) {
            for (auto it = doc["specs"].MemberBegin();
                 it != doc["specs"].MemberEnd(); ++it) {
                if (!it->value.IsArray()) continue;
                std::vector<std::string> trees;
                for (const auto& tree : it->value.GetArray()) {
                    if (tree.IsString()) trees.emplace_back(tree.GetString());
                }
                try {
                    specToTrees_.emplace(
                        static_cast<uint16_t>(std::stoul(it->name.GetString())),
                        std::move(trees));
                } catch (const std::exception&) {
                }
            }
        }

        if (doc.HasMember("trees") && doc["trees"].IsArray()) {
            for (const auto& tree : doc["trees"].GetArray()) {
                if (tree.IsString()) allTrees_.emplace_back(tree.GetString());
            }
        }

        if (!entryToTree_.empty()) {
            loaded_ = true;
            AWOW_LOG_INFO("HERO", "Loaded " + std::to_string(entryToTree_.size())
                                      + " hero talent entries from "
                                      + path.filename().string());
            return true;
        }
    }

    AWOW_LOG_WARNING("HERO", "No hero talent data in " + dataDir);
    return false;
}

std::string HeroTalents::treeForTalents(
    const std::vector<uint32_t>& talentEntryIds) const {
    // One hero tree per character, so the first hit settles it. Scanning the
    // rest would only find the same answer again.
    for (uint32_t id : talentEntryIds) {
        auto it = entryToTree_.find(id);
        if (it != entryToTree_.end()) return it->second;
    }
    return {};
}

const std::vector<std::string>& HeroTalents::treesForSpec(uint16_t specId) const {
    auto it = specToTrees_.find(specId);
    return it == specToTrees_.end() ? emptyTrees() : it->second;
}

} // namespace awow
