#include <catch2/catch_test_macros.hpp>

#include <cctype>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

// Format-specifier checks for the localized strings.
//
// LF() feeds a translated string to snprintf at runtime, so the compiler
// cannot check that its conversions match the arguments the call site passes.
// A string whose %s and %f are in a different order than the code expects
// makes snprintf read a pointer as a double - or worse, a double as a pointer,
// which it then dereferences. That is a crash in a release build, triggered
// only when that particular finding happens to be shown.
//
// These tests pin the shape of every string the benchmark formats, so a
// reordered translation fails here rather than in front of a user.

namespace {

// Conversions in order, with %% (a literal percent) skipped since it consumes
// no argument.
std::vector<char> conversions(const std::string& format) {
    std::vector<char> out;
    for (size_t i = 0; i < format.size(); ++i) {
        if (format[i] != '%') continue;
        if (i + 1 < format.size() && format[i + 1] == '%') {
            ++i;            // Literal percent
            continue;
        }
        // A space right after '%' means prose: "200% crit multiplier" would
        // otherwise parse as the space flag followed by %c. The space flag is
        // legal printf but never appears in these strings, and reading a
        // sentence as a conversion is the more damaging mistake.
        if (i + 1 < format.size() && format[i + 1] == ' ') continue;

        // Skip flags, width and precision to reach the conversion letter.
        size_t j = i + 1;
        while (j < format.size() &&
               (std::isdigit(static_cast<unsigned char>(format[j])) ||
                format[j] == '.' || format[j] == '-' || format[j] == '+' ||
                format[j] == '#' || format[j] == '0')) {
            ++j;
        }
        // Then the length modifier: %llu and %zu are one conversion each, and
        // stopping at the 'l' or 'z' would read them as two.
        while (j < format.size() &&
               (format[j] == 'l' || format[j] == 'h' || format[j] == 'z' ||
                format[j] == 'j' || format[j] == 't' || format[j] == 'L')) {
            ++j;
        }
        if (j < format.size()) {
            const char conversion = format[j];
            // A '%' followed by something that is not a conversion letter is
            // prose, not a specifier - "200% crit multiplier" is a sentence,
            // and treating its '%' as a conversion invents an argument that
            // was never there.
            if (std::isalpha(static_cast<unsigned char>(conversion))) {
                out.push_back(conversion);
                i = j;
            }
        }
    }
    return out;
}

// One conversion letter -> the argument kind it consumes.
char kindOf(char conversion) {
    switch (conversion) {
        case 'd': case 'i': case 'u': case 'x': case 'X': case 'c':
            return 'i';     // Integer
        case 'f': case 'F': case 'e': case 'E': case 'g': case 'G':
            return 'f';     // Double
        case 's':
            return 's';     // const char*
        default:
            return '?';
    }
}

std::string kindsOf(const std::string& format) {
    std::string out;
    for (char c : conversions(format)) out.push_back(kindOf(c));
    return out;
}

const std::unordered_map<std::string, std::string>& strings() {
    static const auto table = [] {
        std::unordered_map<std::string, std::string> map;
        std::ifstream file("data/lang/en_US.csv");
        std::string line;
        while (std::getline(file, line)) {
            if (line.empty() || line[0] == '#') continue;
            const size_t comma = line.find(',');
            if (comma == std::string::npos) continue;
            if (!line.empty() && line.back() == '\r') line.pop_back();
            map[line.substr(0, comma)] = line.substr(comma + 1);
        }
        return map;
    }();
    return table;
}

// Assert a key exists and takes exactly these argument kinds, in this order.
void expectSignature(const std::string& key, const std::string& expected) {
    const auto& table = strings();
    const auto it = table.find(key);
    INFO("key: " << key);
    REQUIRE(it != table.end());

    INFO("format: " << it->second);
    CHECK(kindsOf(it->second) == expected);
}

} // namespace

TEST_CASE("The specifier parser handles literal percents",
          "[localization][format]") {
    // The trap this whole file exists for: %% consumes no argument, so a
    // parser that counts it shifts every later conversion.
    CHECK(kindsOf("%.0f%% of the time") == "f");
    CHECK(kindsOf("%s took %.0f%% - reference %.0f%%") == "sff");
    CHECK(kindsOf("no conversions here") == "");
    CHECK(kindsOf("%d and %s and %.1f") == "isf");
    // Length modifiers are part of one conversion, not extra ones.
    CHECK(kindsOf("%llu / %llu") == "ii");
    CHECK(kindsOf("(%zu fights)") == "i");
    // A percent in prose is not a conversion.
    CHECK(kindsOf("assuming a 200% crit multiplier") == "");
}

TEST_CASE("Benchmark finding strings match what the report card passes",
          "[localization][format][benchmark]") {
    // Each of these mirrors a LF() call in UIBenchmarkReportCard.cpp. Read the
    // call site before changing one; the order matters as much as the count.
    expectSignature("benchmark.finding.cd_delayed", "sfs");
    expectSignature("benchmark.finding.cd_weaker", "sss");
    expectSignature("benchmark.finding.cd_unused", "si");
    expectSignature("benchmark.finding.cd_fewer", "sii");
    expectSignature("benchmark.finding.filler_dps", "ss");
    expectSignature("benchmark.finding.filler_idle", "ff");
    expectSignature("benchmark.finding.cast_rate", "sff");
    expectSignature("benchmark.finding.damage_taken", "sss");

    // The one that crashed: the npc name is passed first, so %s leads.
    expectSignature("benchmark.finding.target", "sff");

    // Live mode's status line, formatted in Application::runLiveCompareMode.
    expectSignature("benchmark.live_waiting", "s");
    expectSignature("benchmark.live_reading", "i");
    expectSignature("benchmark.live_showing", "i");

    expectSignature("benchmark.finding.proc_wasted", "si");
    expectSignature("benchmark.finding.uptime", "sff");
    expectSignature("benchmark.finding.resource_capped", "f");
}

TEST_CASE("Benchmark summary strings match their call sites",
          "[localization][format][benchmark]") {
    expectSignature("benchmark.headline", "ss");
    // Both now name the player AND the window they were measured over - the
    // truncation is the thing most likely to be misread, so it is stated
    // rather than left implied.
    expectSignature("benchmark.window_died", "ss");
    expectSignature("benchmark.window_survived", "s");
    expectSignature("benchmark.window_mentor_end", "s");
    // The verdict headline: magnitude first, then the reference's name.
    expectSignature("benchmark.verdict_behind", "fs");
    expectSignature("benchmark.verdict_ahead", "fs");
    expectSignature("benchmark.shortfall_burst", "s");
    expectSignature("benchmark.shortfall_filler", "s");
    expectSignature("benchmark.vs", "s");
    expectSignature("benchmark.phase_index", "i");
    expectSignature("benchmark.cd_window_detail", "si");
    expectSignature("benchmark.cd_window", "ss");
    expectSignature("benchmark.cd_output", "ssi");
    expectSignature("benchmark.cd_buff", "sf");
    expectSignature("benchmark.timeline_worst", "ss");
    expectSignature("classify.evidence.faster_than_gcd", "if");
    expectSignature("classify.evidence.auto_attack", "i");
    expectSignature("classify.spell_id", "i");
    expectSignature("benchmark.movement_yards", "f");
    expectSignature("benchmark.movie.speed", "f");
    expectSignature("benchmark.composition.shares", "ff");
    expectSignature("benchmark.composition.header", "ss");
    expectSignature("benchmark.downtime_count", "si");
    expectSignature("benchmark.downtime_worst", "s");
    expectSignature("benchmark.downtime_gap", "sf");
    expectSignature("benchmark.downtime_cancelled", "s");
    expectSignature("benchmark.weight_suffix", "f");
    expectSignature("benchmark.downtime_value", "sf");
    expectSignature("benchmark.window_target", "ss");
    expectSignature("preview.per_minute", "f");
    expectSignature("preview.unresolved_phases", "s");
    expectSignature("preview.cd_detail", "ssssi");
    expectSignature("preview.spell_row", "if");
    expectSignature("benchmark.blocked.pending_count", "i");
    expectSignature("benchmark.cd_row", "si");
    expectSignature("benchmark.unknown_spell", "i");
    expectSignature("benchmark.unknown_npc", "i");
    expectSignature("benchmark.prepare_use", "s");
}

TEST_CASE("Classification strings match their call sites",
          "[localization][format][benchmark]") {
    expectSignature("classify.subtitle", "s");
    expectSignature("classify.pending", "i");
    expectSignature("classify.evidence.cadence", "iff");
    expectSignature("classify.evidence.irregular", "i");
    expectSignature("classify.evidence.plain", "i");
    expectSignature("classify.evidence.aura", "iff");
}

TEST_CASE("Picker and overlay strings match their call sites",
          "[localization][format][benchmark]") {
    expectSignature("mentorpicker.step_mentor", "s");
    expectSignature("mentorpicker.step_fight", "s");
    expectSignature("mentorpicker.mentor_detail", "ss");
    expectSignature("mentorpicker.unknown_encounter", "i");
    expectSignature("overlay.benchmark_result", "sf");
    expectSignature("overlay.benchmark_findings", "i");
    expectSignature("prepare.subtitle", "ss");
}

TEST_CASE("Translated benchmark strings agree with en_US on argument order",
          "[localization][format][benchmark]") {
    // A translator reordering a sentence is normal and fine - but the
    // conversions have to stay in the same ORDER, because snprintf matches
    // them positionally. Getting this wrong crashes only for users running
    // that language, which is the worst kind of bug to ship.
    //
    // Scoped to the keys this feature owns. A sweep of every string finds ~26
    // pre-existing breakages elsewhere in the app - almost all of them a
    // translation tool having replaced specifiers with "[[PH0]]" tokens and
    // never put them back. Those are real, and worth fixing, but they are not
    // this feature's to fix and would leave a permanently red test.
    static const char* kPrefixes[] = {
        "benchmark.", "classify.", "mentorpicker.", "prepare.",
        "overlay.benchmark",
    };

    static const char* kLocales[] = {
        "de_DE", "es_MX", "fr_FR", "ko_KR", "pt_BR", "ru_RU", "zh_CN", "zh_TW",
    };

    const auto& base = strings();

    for (const char* locale : kLocales) {
        const std::string path = std::string("data/lang/") + locale + ".csv";
        std::ifstream file(path);
        if (!file) continue;    // Locale not shipped; nothing to check

        std::string line;
        while (std::getline(file, line)) {
            if (line.empty() || line[0] == '#') continue;
            if (!line.empty() && line.back() == '\r') line.pop_back();
            const size_t comma = line.find(',');
            if (comma == std::string::npos) continue;

            const std::string key = line.substr(0, comma);

            bool owned = false;
            for (const char* prefix : kPrefixes) {
                if (key.rfind(prefix, 0) == 0) { owned = true; break; }
            }
            if (!owned) continue;

            const auto baseIt = base.find(key);
            if (baseIt == base.end()) continue;   // Key only in this locale

            const std::string translated = line.substr(comma + 1);
            if (translated.empty()) continue;

            INFO("locale: " << locale << "  key: " << key);
            INFO("en_US:      " << baseIt->second);
            INFO("translated: " << translated);
            CHECK(kindsOf(translated) == kindsOf(baseIt->second));
        }
    }
}
