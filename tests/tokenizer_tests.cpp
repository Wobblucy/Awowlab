#define CATCH_CONFIG_MAIN
#include <catch2/catch_all.hpp>
#include "../include/Txttokenizer/Txttokenizer.h"
#include "../AwowLab/MMapManager/src/MMapManager.cpp"
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

TEST_CASE("Tokenize single English log line", "[tokenize][single]") {
    std::string log_line = R"(11/23/2024 18:34:39.988-7  SWING_DAMAGE_LANDED,Creature-0-3886-2660-16035-216336-00104282A4,"Ravenous Crawler",0x10a48,0x0,Player-11-0E90EFD2,"Woßß-Tichondrius-US",0x511,0x0,Player-11-0E90EFD2,0000000000000000,8586364,9070440,89583,18718,133133,191,0,0,0,2500000,2500000,0,-331.88,318.29,2357,1.5539,631,484076,1114267,-1,1,0,0,0,nil,nil,nil)";

    tokenized_segment tokenizer;
    auto all_tokens = tokenizer.tokenize(log_line.data(), log_line.size());

    REQUIRE(all_tokens.size() == 1);

    std::vector<std::string> expected_tokens = {
        "11/23/2024", "18:34:39.988-7", "SWING_DAMAGE_LANDED",
        "Creature-0-3886-2660-16035-216336-00104282A4", "Ravenous Crawler",
        "0x10a48", "0x0", "Player-11-0E90EFD2", "Woßß-Tichondrius-US",
        "0x511", "0x0", "Player-11-0E90EFD2", "0000000000000000",
        "8586364", "9070440", "89583", "18718", "133133", "191",
        "0", "0", "0", "2500000", "2500000", "0", "-331.88", "318.29",
        "2357", "1.5539", "631", "484076", "1114267", "-1", "1", "0", "0", "0",
        "nil", "nil", "nil"
    };

    const auto& tokens = all_tokens[0];
    REQUIRE(tokens.size() == expected_tokens.size());
    for (size_t i = 0; i < expected_tokens.size(); ++i) {
        INFO("Token " << i << ": expected [" << expected_tokens[i] << "] got [" << tokens[i] << "]");
        REQUIRE(tokens[i] == expected_tokens[i]);
    }
}

TEST_CASE("Tokenize multiple English lines", "[tokenize][multi]") {
    std::string multi_lines = R"(11/23/2024 18:34:39.988-7  SWING_DAMAGE_LANDED,Creature-0-3886-2660-16035-216336-00104282A4,"Ravenous Crawler"
11/23/2024 18:35:01.123-7  SPELL_CAST_START,Player-11-0E90EFD2,"Woßß-Tichondrius-US")";

    tokenized_segment tokenizer;
    auto all_tokens = tokenizer.tokenize(multi_lines.data(), multi_lines.size());

    REQUIRE(all_tokens.size() == 2);

    std::vector<std::vector<std::string>> expected_lines = {
        {"11/23/2024", "18:34:39.988-7", "SWING_DAMAGE_LANDED", "Creature-0-3886-2660-16035-216336-00104282A4", "Ravenous Crawler"},
        {"11/23/2024", "18:35:01.123-7", "SPELL_CAST_START", "Player-11-0E90EFD2", "Woßß-Tichondrius-US"}
    };

    for (size_t line_idx = 0; line_idx < expected_lines.size(); ++line_idx) {
        const auto& tokens = all_tokens[line_idx];
        const auto& expected_tokens = expected_lines[line_idx];
        REQUIRE(tokens.size() == expected_tokens.size());
        for (size_t i = 0; i < expected_tokens.size(); ++i) {
            INFO("Line " << line_idx << " Token " << i << ": expected [" << expected_tokens[i] << "] got [" << tokens[i] << "]");
            REQUIRE(tokens[i] == expected_tokens[i]);
        }
    }
}

TEST_CASE("Tokenize single Spanish/UTF-8 log line", "[tokenize][utf8]") {
    std::string log_line = R"(23/11/2024 18:34:39.988-7  DAÑO_GOLPEADO,Jugador-11-0E90EFD2,"Niñø-Tichondrius-US",0x511,0x0)";

    tokenized_segment tokenizer;
    auto all_tokens = tokenizer.tokenize(log_line.data(), log_line.size());

    REQUIRE(all_tokens.size() == 1);
    std::vector<std::string> expected_tokens = {
        "23/11/2024", "18:34:39.988-7", "DAÑO_GOLPEADO", "Jugador-11-0E90EFD2", "Niñø-Tichondrius-US", "0x511", "0x0"
    };

    const auto& tokens = all_tokens[0];
    REQUIRE(tokens.size() == expected_tokens.size());
    for (size_t i = 0; i < expected_tokens.size(); ++i) {
        INFO("Token " << i << ": expected [" << expected_tokens[i] << "] got [" << tokens[i] << "]");
        REQUIRE(tokens[i] == expected_tokens[i]);
    }
}

TEST_CASE("Tokenize empty line and spaces", "[tokenize][edge]") {
    std::string input = "   \n\n11/23/2024 18:34:39.988-7  EVENT_START\n";
    tokenized_segment tokenizer;
    auto all_tokens = tokenizer.tokenize(input.data(), input.size());

    // Should skip empty lines
    REQUIRE(all_tokens.size() == 1);
    const auto& tokens = all_tokens[0];
    REQUIRE(tokens.size() == 3);
    REQUIRE(tokens[0] == "11/23/2024");
    REQUIRE(tokens[1] == "18:34:39.988-7");
    REQUIRE(tokens[2] == "EVENT_START");
}

TEST_CASE("Tokenize quoted token with spaces", "[tokenize][quotes]") {
    std::string input = R"(11/23/2024 18:34:39.988-7  SPELL_CAST,"Player One","Spell Name")";
    tokenized_segment tokenizer;
    auto all_tokens = tokenizer.tokenize(input.data(), input.size());

    REQUIRE(all_tokens.size() == 1);
    const auto& tokens = all_tokens[0];
    REQUIRE(tokens.size() == 5);
    REQUIRE(tokens[0] == "11/23/2024");
    REQUIRE(tokens[1] == "18:34:39.988-7");
    REQUIRE(tokens[2] == "SPELL_CAST");
    REQUIRE(tokens[3] == "Player One");
    REQUIRE(tokens[4] == "Spell Name");
}

// An empty log (WoW creates the file before it writes anything) is nothing
// to load, not an error, and leaves no handle behind to be closed twice.
TEST_CASE("Mapping an empty log gives no data and no error", "[mmap]") {
    const auto path = std::filesystem::temp_directory_path() / "awowlab_empty_log.txt";
    std::ofstream(path, std::ios::binary | std::ios::trunc).close();

    {
        MMapFile file;
        REQUIRE_NOTHROW(file.open(path.string()));
        CHECK(file.get_data() == nullptr);
        CHECK(file.get_size() == 0);
    }

    // The handle was released: the file can be removed straight away.
    std::error_code ec;
    CHECK(std::filesystem::remove(path, ec));
}
