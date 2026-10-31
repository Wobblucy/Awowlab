#include "PlayAlong/PlayAlongKeybindSettings.h"
#include <imgui.h>
#include <rapidjson/document.h>
#include <rapidjson/writer.h>
#include <rapidjson/prettywriter.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/error/en.h>
#include <charconv>
#include <cstring>
#include <fstream>
#include <sstream>
#include <filesystem>

namespace PlayAlong {

const Keybind PlayAlongKeybindSettings::emptyKeybind_{};

std::string Keybind::toString() const {
    if (isUnbound()) {
        return "";
    }

    std::string result;
    if (ctrl) result += "Ctrl+";
    if (shift) result += "Shift+";
    if (alt) result += "Alt+";
    result += PlayAlongKeybindSettings::imguiKeyToName(imguiKey);
    return result;
}

std::string PlayAlongKeybindSettings::specIdToName(uint16_t specId) {
    static const std::unordered_map<uint16_t, std::string> specNames = {
        // Death Knight
        {250, "blood-dk"}, {251, "frost-dk"}, {252, "unholy-dk"},
        {1455, "dk"},  // Initial
        // Demon Hunter
        {577, "havoc-dh"}, {581, "vengeance-dh"},
        {1480, "devourer-dh"},
        {1456, "dh"},  // Initial
        // Druid
        {102, "balance"}, {103, "feral"}, {104, "guardian"}, {105, "resto-druid"},
        {1447, "druid"},  // Initial
        // Evoker
        {1467, "devastation"}, {1468, "preservation"}, {1473, "augmentation"},
        {1465, "evoker"},  // Initial
        // Hunter
        {253, "beast-mastery"}, {254, "marksmanship"}, {255, "survival"},
        {1448, "hunter"},  // Initial
        // Mage
        {62, "arcane-mage"}, {63, "fire-mage"}, {64, "frost-mage"},
        {1449, "mage"},  // Initial
        // Monk
        {268, "brewmaster"}, {270, "mistweaver"}, {269, "windwalker"},
        {1450, "monk"},  // Initial
        // Paladin
        {65, "holy-paladin"}, {66, "prot-paladin"}, {70, "retribution"},
        {1451, "paladin"},  // Initial
        // Priest
        {256, "discipline"}, {257, "holy-priest"}, {258, "shadow"},
        {1452, "priest"},  // Initial
        // Rogue
        {259, "assassination"}, {260, "outlaw"}, {261, "subtlety"},
        {1453, "rogue"},  // Initial
        // Shaman
        {262, "elemental"}, {263, "enhancement"}, {264, "resto-shaman"},
        {1444, "shaman"},  // Initial
        // Warlock
        {265, "affliction"}, {266, "demonology"}, {267, "destruction"},
        {1454, "warlock"},  // Initial
        // Warrior
        {71, "arms"}, {72, "fury"}, {73, "prot-warrior"},
        {1446, "warrior"}  // Initial
    };

    auto it = specNames.find(specId);
    return it != specNames.end() ? it->second : "unknown-" + std::to_string(specId);
}

std::string PlayAlongKeybindSettings::specDisplayName(uint16_t specId) {
    static const std::unordered_map<uint16_t, std::string> specDisplayNames = {
        // Death Knight
        {250, "Blood Death Knight"}, {251, "Frost Death Knight"}, {252, "Unholy Death Knight"},
        // Demon Hunter
        {577, "Havoc Demon Hunter"}, {581, "Vengeance Demon Hunter"}, {1480, "Devourer Demon Hunter"},
        // Druid
        {102, "Balance Druid"}, {103, "Feral Druid"}, {104, "Guardian Druid"}, {105, "Restoration Druid"},
        // Evoker
        {1467, "Devastation Evoker"}, {1468, "Preservation Evoker"}, {1473, "Augmentation Evoker"},
        // Hunter
        {253, "Beast Mastery Hunter"}, {254, "Marksmanship Hunter"}, {255, "Survival Hunter"},
        // Mage
        {62, "Arcane Mage"}, {63, "Fire Mage"}, {64, "Frost Mage"},
        // Monk
        {268, "Brewmaster Monk"}, {270, "Mistweaver Monk"}, {269, "Windwalker Monk"},
        // Paladin
        {65, "Holy Paladin"}, {66, "Protection Paladin"}, {70, "Retribution Paladin"},
        // Priest
        {256, "Discipline Priest"}, {257, "Holy Priest"}, {258, "Shadow Priest"},
        // Rogue
        {259, "Assassination Rogue"}, {260, "Outlaw Rogue"}, {261, "Subtlety Rogue"},
        // Shaman
        {262, "Elemental Shaman"}, {263, "Enhancement Shaman"}, {264, "Restoration Shaman"},
        // Warlock
        {265, "Affliction Warlock"}, {266, "Demonology Warlock"}, {267, "Destruction Warlock"},
        // Warrior
        {71, "Arms Warrior"}, {72, "Fury Warrior"}, {73, "Protection Warrior"}
    };

    auto it = specDisplayNames.find(specId);
    return it != specDisplayNames.end() ? it->second : std::string();
}

std::string PlayAlongKeybindSettings::getSettingsDirectory() {
    std::filesystem::path exePath = std::filesystem::current_path();
    std::filesystem::path settingsPath = exePath / "User Settings" / "Playalong";

    if (!std::filesystem::exists(settingsPath)) {
        std::filesystem::create_directories(settingsPath);
    }

    return settingsPath.string();
}

std::string PlayAlongKeybindSettings::getFilePath() const {
    return getSettingsDirectory() + "/" + specName_ + "-keybinds.json";
}

bool PlayAlongKeybindSettings::load(uint16_t specId) {
    specId_ = specId;
    specName_ = specIdToName(specId);
    keybinds_.clear();
    ignoredSpellIds_.clear();

    std::string filePath = getFilePath();

    if (!std::filesystem::exists(filePath)) {
        return true;
    }

    std::ifstream file(filePath);
    if (!file.is_open()) {
        return false;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string jsonContent = buffer.str();
    file.close();

    rapidjson::Document doc;
    if (doc.Parse(jsonContent.c_str()).HasParseError()) {
        return false;
    }

    if (!doc.IsObject() || !doc.HasMember("keybinds") || !doc["keybinds"].IsObject()) {
        return false;
    }

    const auto& keybindsObj = doc["keybinds"];
    for (auto it = keybindsObj.MemberBegin(); it != keybindsObj.MemberEnd(); ++it) {
        // The file can be edited by hand, so a bad entry is skipped rather
        // than throwing out of the whole load
        const char* idText = it->name.GetString();
        const char* idEnd = idText + std::strlen(idText);
        uint32_t spellId = 0;
        auto [parseEnd, parseError] = std::from_chars(idText, idEnd, spellId);
        if (parseError != std::errc() || parseEnd != idEnd || parseEnd == idText) {
            continue;
        }
        const auto& kb = it->value;

        if (!kb.IsObject()) continue;

        Keybind keybind;

        if (kb.HasMember("key") && kb["key"].IsInt()) {
            keybind.imguiKey = kb["key"].GetInt();
        } else if (kb.HasMember("key_name") && kb["key_name"].IsString()) {
            // Fallback to key_name for human-edited files
            keybind.imguiKey = nameToImguiKey(kb["key_name"].GetString());
        }

        if (kb.HasMember("ctrl") && kb["ctrl"].IsBool()) {
            keybind.ctrl = kb["ctrl"].GetBool();
        }
        if (kb.HasMember("shift") && kb["shift"].IsBool()) {
            keybind.shift = kb["shift"].GetBool();
        }
        if (kb.HasMember("alt") && kb["alt"].IsBool()) {
            keybind.alt = kb["alt"].GetBool();
        }

        // The key goes straight to ImGui::IsKeyPressed, which asserts on
        // anything outside the named key range
        if (!keybind.isUnbound() && isValidImguiKey(keybind.imguiKey)) {
            keybinds_[spellId] = keybind;
        }
    }

    if (doc.HasMember("ignored_spells") && doc["ignored_spells"].IsArray()) {
        for (const auto& id : doc["ignored_spells"].GetArray()) {
            if (id.IsUint()) {
                ignoredSpellIds_.insert(id.GetUint());
            }
        }
    }

    return true;
}

bool PlayAlongKeybindSettings::save() const {
    rapidjson::Document doc;
    doc.SetObject();
    auto& allocator = doc.GetAllocator();

    doc.AddMember("version", 1, allocator);

    doc.AddMember("spec_id", specId_, allocator);
    rapidjson::Value specNameVal;
    specNameVal.SetString(specName_.c_str(), static_cast<rapidjson::SizeType>(specName_.size()), allocator);
    doc.AddMember("spec_name", specNameVal, allocator);

    rapidjson::Value keybindsObj(rapidjson::kObjectType);

    for (const auto& [spellId, keybind] : keybinds_) {
        if (keybind.isUnbound()) continue;

        rapidjson::Value kbObj(rapidjson::kObjectType);

        kbObj.AddMember("key", keybind.imguiKey, allocator);

        std::string keyName = imguiKeyToName(keybind.imguiKey);
        rapidjson::Value keyNameVal;
        keyNameVal.SetString(keyName.c_str(), static_cast<rapidjson::SizeType>(keyName.size()), allocator);
        kbObj.AddMember("key_name", keyNameVal, allocator);

        kbObj.AddMember("ctrl", keybind.ctrl, allocator);
        kbObj.AddMember("shift", keybind.shift, allocator);
        kbObj.AddMember("alt", keybind.alt, allocator);

        std::string spellIdStr = std::to_string(spellId);
        rapidjson::Value spellIdKey;
        spellIdKey.SetString(spellIdStr.c_str(), static_cast<rapidjson::SizeType>(spellIdStr.size()), allocator);
        keybindsObj.AddMember(spellIdKey, kbObj, allocator);
    }

    doc.AddMember("keybinds", keybindsObj, allocator);

    rapidjson::Value ignoredArray(rapidjson::kArrayType);
    for (uint32_t spellId : ignoredSpellIds_) {
        ignoredArray.PushBack(spellId, allocator);
    }
    doc.AddMember("ignored_spells", ignoredArray, allocator);

    rapidjson::StringBuffer buffer;
    rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(buffer);
    doc.Accept(writer);

    std::string filePath = getFilePath();
    std::ofstream file(filePath);
    if (!file.is_open()) {
        return false;
    }

    file << buffer.GetString();
    file.close();

    return true;
}

const Keybind& PlayAlongKeybindSettings::getKeybind(uint32_t spellId) const {
    auto it = keybinds_.find(spellId);
    if (it != keybinds_.end()) {
        return it->second;
    }
    return emptyKeybind_;
}

void PlayAlongKeybindSettings::setKeybind(uint32_t spellId, const Keybind& keybind) {
    if (keybind.isUnbound()) {
        keybinds_.erase(spellId);
    } else {
        keybinds_[spellId] = keybind;
    }
}

void PlayAlongKeybindSettings::clearKeybind(uint32_t spellId) {
    keybinds_.erase(spellId);
}

bool PlayAlongKeybindSettings::hasKeybind(uint32_t spellId) const {
    auto it = keybinds_.find(spellId);
    return it != keybinds_.end() && !it->second.isUnbound();
}

void PlayAlongKeybindSettings::ignoreSpell(uint32_t spellId) {
    ignoredSpellIds_.insert(spellId);
    keybinds_.erase(spellId);
}

void PlayAlongKeybindSettings::unignoreSpell(uint32_t spellId) {
    ignoredSpellIds_.erase(spellId);
}

bool PlayAlongKeybindSettings::isSpellIgnored(uint32_t spellId) const {
    return ignoredSpellIds_.count(spellId) > 0;
}

std::string PlayAlongKeybindSettings::imguiKeyToName(int imguiKey) {
    switch (imguiKey) {
        // Letters
        case ImGuiKey_A: return "A";
        case ImGuiKey_B: return "B";
        case ImGuiKey_C: return "C";
        case ImGuiKey_D: return "D";
        case ImGuiKey_E: return "E";
        case ImGuiKey_F: return "F";
        case ImGuiKey_G: return "G";
        case ImGuiKey_H: return "H";
        case ImGuiKey_I: return "I";
        case ImGuiKey_J: return "J";
        case ImGuiKey_K: return "K";
        case ImGuiKey_L: return "L";
        case ImGuiKey_M: return "M";
        case ImGuiKey_N: return "N";
        case ImGuiKey_O: return "O";
        case ImGuiKey_P: return "P";
        case ImGuiKey_Q: return "Q";
        case ImGuiKey_R: return "R";
        case ImGuiKey_S: return "S";
        case ImGuiKey_T: return "T";
        case ImGuiKey_U: return "U";
        case ImGuiKey_V: return "V";
        case ImGuiKey_W: return "W";
        case ImGuiKey_X: return "X";
        case ImGuiKey_Y: return "Y";
        case ImGuiKey_Z: return "Z";

        // Numbers
        case ImGuiKey_0: return "0";
        case ImGuiKey_1: return "1";
        case ImGuiKey_2: return "2";
        case ImGuiKey_3: return "3";
        case ImGuiKey_4: return "4";
        case ImGuiKey_5: return "5";
        case ImGuiKey_6: return "6";
        case ImGuiKey_7: return "7";
        case ImGuiKey_8: return "8";
        case ImGuiKey_9: return "9";

        // Function keys
        case ImGuiKey_F1: return "F1";
        case ImGuiKey_F2: return "F2";
        case ImGuiKey_F3: return "F3";
        case ImGuiKey_F4: return "F4";
        case ImGuiKey_F5: return "F5";
        case ImGuiKey_F6: return "F6";
        case ImGuiKey_F7: return "F7";
        case ImGuiKey_F8: return "F8";
        case ImGuiKey_F9: return "F9";
        case ImGuiKey_F10: return "F10";
        case ImGuiKey_F11: return "F11";
        case ImGuiKey_F12: return "F12";

        // Special keys
        case ImGuiKey_Space: return "Space";
        case ImGuiKey_Tab: return "Tab";
        case ImGuiKey_Enter: return "Enter";
        case ImGuiKey_Backspace: return "Backspace";
        case ImGuiKey_Delete: return "Delete";
        case ImGuiKey_Insert: return "Insert";
        case ImGuiKey_Home: return "Home";
        case ImGuiKey_End: return "End";
        case ImGuiKey_PageUp: return "PageUp";
        case ImGuiKey_PageDown: return "PageDown";

        // Arrow keys
        case ImGuiKey_UpArrow: return "Up";
        case ImGuiKey_DownArrow: return "Down";
        case ImGuiKey_LeftArrow: return "Left";
        case ImGuiKey_RightArrow: return "Right";

        // Punctuation
        case ImGuiKey_Minus: return "-";
        case ImGuiKey_Equal: return "=";
        case ImGuiKey_LeftBracket: return "[";
        case ImGuiKey_RightBracket: return "]";
        case ImGuiKey_Backslash: return "\\";
        case ImGuiKey_Semicolon: return ";";
        case ImGuiKey_Apostrophe: return "'";
        case ImGuiKey_Comma: return ",";
        case ImGuiKey_Period: return ".";
        case ImGuiKey_Slash: return "/";
        case ImGuiKey_GraveAccent: return "`";

        // Numpad
        case ImGuiKey_Keypad0: return "Num0";
        case ImGuiKey_Keypad1: return "Num1";
        case ImGuiKey_Keypad2: return "Num2";
        case ImGuiKey_Keypad3: return "Num3";
        case ImGuiKey_Keypad4: return "Num4";
        case ImGuiKey_Keypad5: return "Num5";
        case ImGuiKey_Keypad6: return "Num6";
        case ImGuiKey_Keypad7: return "Num7";
        case ImGuiKey_Keypad8: return "Num8";
        case ImGuiKey_Keypad9: return "Num9";
        case ImGuiKey_KeypadDecimal: return "Num.";
        case ImGuiKey_KeypadDivide: return "Num/";
        case ImGuiKey_KeypadMultiply: return "Num*";
        case ImGuiKey_KeypadSubtract: return "Num-";
        case ImGuiKey_KeypadAdd: return "Num+";
        case ImGuiKey_KeypadEnter: return "NumEnter";

        default: return "?";
    }
}

bool PlayAlongKeybindSettings::isValidImguiKey(int imguiKey) {
    return imguiKey >= ImGuiKey_NamedKey_BEGIN && imguiKey < ImGuiKey_NamedKey_END;
}

int PlayAlongKeybindSettings::nameToImguiKey(const std::string& name) {
    static const std::unordered_map<std::string, int> nameToKey = {
        // Letters
        {"A", ImGuiKey_A}, {"B", ImGuiKey_B}, {"C", ImGuiKey_C}, {"D", ImGuiKey_D},
        {"E", ImGuiKey_E}, {"F", ImGuiKey_F}, {"G", ImGuiKey_G}, {"H", ImGuiKey_H},
        {"I", ImGuiKey_I}, {"J", ImGuiKey_J}, {"K", ImGuiKey_K}, {"L", ImGuiKey_L},
        {"M", ImGuiKey_M}, {"N", ImGuiKey_N}, {"O", ImGuiKey_O}, {"P", ImGuiKey_P},
        {"Q", ImGuiKey_Q}, {"R", ImGuiKey_R}, {"S", ImGuiKey_S}, {"T", ImGuiKey_T},
        {"U", ImGuiKey_U}, {"V", ImGuiKey_V}, {"W", ImGuiKey_W}, {"X", ImGuiKey_X},
        {"Y", ImGuiKey_Y}, {"Z", ImGuiKey_Z},

        // Numbers
        {"0", ImGuiKey_0}, {"1", ImGuiKey_1}, {"2", ImGuiKey_2}, {"3", ImGuiKey_3},
        {"4", ImGuiKey_4}, {"5", ImGuiKey_5}, {"6", ImGuiKey_6}, {"7", ImGuiKey_7},
        {"8", ImGuiKey_8}, {"9", ImGuiKey_9},

        // Function keys
        {"F1", ImGuiKey_F1}, {"F2", ImGuiKey_F2}, {"F3", ImGuiKey_F3}, {"F4", ImGuiKey_F4},
        {"F5", ImGuiKey_F5}, {"F6", ImGuiKey_F6}, {"F7", ImGuiKey_F7}, {"F8", ImGuiKey_F8},
        {"F9", ImGuiKey_F9}, {"F10", ImGuiKey_F10}, {"F11", ImGuiKey_F11}, {"F12", ImGuiKey_F12},

        // Special keys
        {"Space", ImGuiKey_Space}, {"Tab", ImGuiKey_Tab}, {"Enter", ImGuiKey_Enter},
        {"Backspace", ImGuiKey_Backspace}, {"Delete", ImGuiKey_Delete}, {"Insert", ImGuiKey_Insert},
        {"Home", ImGuiKey_Home}, {"End", ImGuiKey_End}, {"PageUp", ImGuiKey_PageUp}, {"PageDown", ImGuiKey_PageDown},

        // Arrow keys
        {"Up", ImGuiKey_UpArrow}, {"Down", ImGuiKey_DownArrow},
        {"Left", ImGuiKey_LeftArrow}, {"Right", ImGuiKey_RightArrow},

        // Punctuation
        {"-", ImGuiKey_Minus}, {"=", ImGuiKey_Equal}, {"[", ImGuiKey_LeftBracket},
        {"]", ImGuiKey_RightBracket}, {"\\", ImGuiKey_Backslash}, {";", ImGuiKey_Semicolon},
        {"'", ImGuiKey_Apostrophe}, {",", ImGuiKey_Comma}, {".", ImGuiKey_Period},
        {"/", ImGuiKey_Slash}, {"`", ImGuiKey_GraveAccent},

        // Numpad
        {"Num0", ImGuiKey_Keypad0}, {"Num1", ImGuiKey_Keypad1}, {"Num2", ImGuiKey_Keypad2},
        {"Num3", ImGuiKey_Keypad3}, {"Num4", ImGuiKey_Keypad4}, {"Num5", ImGuiKey_Keypad5},
        {"Num6", ImGuiKey_Keypad6}, {"Num7", ImGuiKey_Keypad7}, {"Num8", ImGuiKey_Keypad8},
        {"Num9", ImGuiKey_Keypad9}, {"Num.", ImGuiKey_KeypadDecimal}, {"Num/", ImGuiKey_KeypadDivide},
        {"Num*", ImGuiKey_KeypadMultiply}, {"Num-", ImGuiKey_KeypadSubtract},
        {"Num+", ImGuiKey_KeypadAdd}, {"NumEnter", ImGuiKey_KeypadEnter}
    };

    auto it = nameToKey.find(name);
    return it != nameToKey.end() ? it->second : 0;
}

void PlayAlongKeybindSettings::setUsedSpells(const std::unordered_set<uint32_t>& usedSpellIds) {
    usedSpellIds_ = usedSpellIds;
}

bool PlayAlongKeybindSettings::isKeybindActive(uint32_t spellId) const {
    // With no used-spell list set, every keybind counts as active
    if (usedSpellIds_.empty()) {
        return true;
    }
    return usedSpellIds_.count(spellId) > 0;
}

} // namespace PlayAlong
