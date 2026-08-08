#include "producer/producer.h"
#include "common/signal_handler.h"
#include "common/util.h"
#include <iostream>
#include <fstream>
#include <filesystem>
#include <cstdlib>

namespace pc {
namespace fs = std::filesystem;

static void print_usage() {
    std::cerr << "Usage: producer --file PATH [OPTIONS]\n"
              << "\nOptions:\n"
              << "  --file PATH            Path to job file (required)\n"
              << "  --port PORT            Port to bind on (default: 9876)\n"
              << "  --transport tcp|udp    Transport protocol (default: tcp)\n"
              << "  --permutation MODE     Job permutation: sequential, random, round_robin, reverse (default: sequential)\n"
              << "  --seed N               PRNG seed for random permutation\n"
              << "  --duration SECS        Run duration in seconds (0 = run until done)\n"
              << "  --gateway IP           Default local gateway IPv4 (default: 192.168.1.1)\n"
              << "  --checkpoint-dir DIR   Directory for checkpoint files (default: ./)\n"
              << "  --resume               Resume from checkpoint if one exists\n"
              << "  --test-type TYPE       Test type identifier (e.g. PWD)\n";
}

int run_producer(int argc, char* argv[]) {
    ProducerConfig config;

    // Parse arguments
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--file" && i + 1 < argc) {
            config.file_path = argv[++i];
        } else if (arg == "--port" && i + 1 < argc) {
            config.port = static_cast<uint16_t>(std::stoi(argv[++i]));
        } else if (arg == "--transport" && i + 1 < argc) {
            std::string t = argv[++i];
            config.transport = (t == "udp") ? Transport::UDP : Transport::TCP;
        } else if (arg == "--permutation" && i + 1 < argc) {
            config.permutation = argv[++i];
        } else if (arg == "--seed" && i + 1 < argc) {
            config.seed = std::stoll(argv[++i]);
        } else if (arg == "--duration" && i + 1 < argc) {
            config.duration = std::stoi(argv[++i]);
        } else if (arg == "--gateway" && i + 1 < argc) {
            config.gateway = argv[++i];
        } else if (arg == "--checkpoint-dir" && i + 1 < argc) {
            config.checkpoint_dir = argv[++i];
        } else if (arg == "--resume") {
            config.resume = true;
        } else if (arg == "--test-type" && i + 1 < argc) {
            config.test_type = argv[++i];
        } else if (arg == "--help" || arg == "-h") {
            print_usage();
            return 0;
        } else {
            std::cerr << "Unknown option: " << arg << "\n";
            print_usage();
            return 1;
        }
    }

    // Validate required arguments
    if (config.file_path.empty()) {
        std::cerr << "Error: --file is required\n";
        print_usage();
        return 1;
    }

    // Default checkpoint directory to platform data directory
    if (config.checkpoint_dir.empty()) {
        config.checkpoint_dir = get_data_directory();
    }

    // Validate file exists
    if (!fs::exists(config.file_path)) {
        std::cerr << "Error: Job file does not exist: " << config.file_path << "\n";
        return 1;
    }

    // Validate permutation mode
    if (config.permutation != "sequential" && config.permutation != "random" &&
        config.permutation != "round_robin" && config.permutation != "reverse") {
        std::cerr << "Error: Invalid permutation mode: " << config.permutation << "\n";
        return 1;
    }

    SignalHandler::install();

    try {
        Producer producer(config);
        producer.run();
    } catch (const std::exception& e) {
        std::cerr << "[producer] Fatal error: " << e.what() << "\n";
        return 2;
    }

    return 0;
}

} // namespace pc

int main(int argc, char* argv[]) {
    return pc::run_producer(argc, argv);
}
