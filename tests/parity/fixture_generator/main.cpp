#include "fixture_generator.h"
#include <iostream>
#include <string>

void printUsage(const char* program) {
    std::cout << "Usage: " << program << " [options]\n"
              << "\n"
              << "Options:\n"
              << "  --local <path>    Path to local combat log file\n"
              << "  --wcl <path>      Path to directory containing WCL page*.json files\n"
              << "  --output <path>   Output directory for fixtures (default: ./fixtures/parity)\n"
              << "  --samples <n>     Number of samples per event type (default: 3)\n"
              << "  --tolerance <ms>  Timestamp matching tolerance in ms (default: 100)\n"
              << "  --help            Show this help message\n"
              << "\n"
              << "Example:\n"
              << "  " << program << " --local \"C:/logs/local.txt\" --wcl \"C:/logs/fight8\" --output \"./fixtures/parity\"\n";
}

int main(int argc, char* argv[]) {
    std::string local_path;
    std::string wcl_path;
    std::string output_path = "./fixtures/parity";
    size_t samples_per_type = 3;
    int32_t tolerance_ms = 100;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "--help" || arg == "-h") {
            printUsage(argv[0]);
            return 0;
        }
        else if (arg == "--local" && i + 1 < argc) {
            local_path = argv[++i];
        }
        else if (arg == "--wcl" && i + 1 < argc) {
            wcl_path = argv[++i];
        }
        else if (arg == "--output" && i + 1 < argc) {
            output_path = argv[++i];
        }
        else if (arg == "--samples" && i + 1 < argc) {
            samples_per_type = std::stoul(argv[++i]);
        }
        else if (arg == "--tolerance" && i + 1 < argc) {
            tolerance_ms = std::stoi(argv[++i]);
        }
        else {
            std::cerr << "Unknown argument: " << arg << std::endl;
            printUsage(argv[0]);
            return 1;
        }
    }

    if (local_path.empty() || wcl_path.empty()) {
        std::cerr << "Error: --local and --wcl arguments are required\n" << std::endl;
        printUsage(argv[0]);
        return 1;
    }

    std::cout << "========================================" << std::endl;
    std::cout << "Parser Parity Fixture Generator" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Local log: " << local_path << std::endl;
    std::cout << "WCL pages: " << wcl_path << std::endl;
    std::cout << "Output:    " << output_path << std::endl;
    std::cout << "Samples:   " << samples_per_type << " per type" << std::endl;
    std::cout << "Tolerance: " << tolerance_ms << " ms" << std::endl;
    std::cout << "========================================" << std::endl;

    parity::FixtureGenerator generator;

    std::cout << "\n[1/5] Loading local combat log..." << std::endl;
    if (!generator.loadLocalLog(local_path)) {
        std::cerr << "Failed to load local log" << std::endl;
        return 1;
    }

    std::cout << "\n[2/5] Loading WarcraftLogs JSON pages..." << std::endl;
    if (!generator.loadWclPages(wcl_path)) {
        std::cerr << "Failed to load WCL pages" << std::endl;
        return 1;
    }

    std::cout << "\n[3/5] Building actor mapping..." << std::endl;
    generator.buildActorMapping();

    std::cout << "\n[4/5] Matching events (tolerance: " << tolerance_ms << "ms)..." << std::endl;
    auto matches = generator.matchEvents(tolerance_ms);

    if (matches.empty()) {
        std::cerr << "No matching events found!" << std::endl;
        std::cerr << "This could mean:" << std::endl;
        std::cerr << "  - The logs are from different encounters" << std::endl;
        std::cerr << "  - Timestamp offset is too large" << std::endl;
        std::cerr << "  - No overlapping spell IDs" << std::endl;
        return 1;
    }

    auto samples = generator.sampleByType(matches, samples_per_type);

    std::cout << "\n[5/5] Exporting fixtures..." << std::endl;
    if (!generator.exportFixtures(output_path, samples)) {
        std::cerr << "Failed to export fixtures" << std::endl;
        return 1;
    }

    std::cout << "\n========================================" << std::endl;
    std::cout << "Done! Generated " << samples.size() << " fixture samples" << std::endl;
    std::cout << "Fixtures saved to: " << output_path << std::endl;
    std::cout << "========================================" << std::endl;

    return 0;
}
