#include "consumer/consumer.h"
#include "common/signal_handler.h"
#include "common/version.h"
#include "consumer/PWD_Handler.h"
#include <iostream>
#include <memory>

int main(int argc, char* argv[]) {
    std::cout << "[consumer] v" << pc::PC_VERSION << "\n";

    pc::ConsumerConfig config;

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--host" && i + 1 < argc) {
            config.host = argv[++i];
        } else if (arg == "--port" && i + 1 < argc) {
            config.port = static_cast<uint16_t>(std::stoi(argv[++i]));
        } else if (arg == "--transport" && i + 1 < argc) {
            std::string t = argv[++i];
            config.transport = (t == "udp") ? pc::Transport::UDP : pc::Transport::TCP;
        } else if (arg == "--threads" && i + 1 < argc) {
            config.threads = std::stoi(argv[++i]);
        } else if (arg == "--file-dir" && i + 1 < argc) {
            config.file_dir = argv[++i];
        } else if (arg == "--max-messages" && i + 1 < argc) {
            config.max_messages = std::stoi(argv[++i]);
        } else if (arg == "--local") {
            config.local = true;
        } else if (arg == "--gateway" && i + 1 < argc) {
            config.gateway = argv[++i];
        } else if (arg == "--consumer-id" && i + 1 < argc) {
            config.consumer_id = argv[++i];
        } else if (arg == "--handler" && i + 1 < argc) {
            config.handler_type = argv[++i];
        } else if (arg == "--handler-config" && i + 1 < argc) {
            config.handler_config = argv[++i];
        } else if (arg == "--result-file" && i + 1 < argc) {
            config.result_file = argv[++i];
        } else if (arg == "--max-failures" && i + 1 < argc) {
            config.max_failures = std::stoi(argv[++i]);
        } else if (arg == "--max-duration" && i + 1 < argc) {
            config.max_duration_sec = std::stoi(argv[++i]);
        } else if (arg == "--timeout" && i + 1 < argc) {
            config.idle_timeout_sec = std::stoi(argv[++i]);
        } else if (arg == "--no-yield") {
            config.yield_cpu = false;
        } else if (arg == "--help" || arg == "-h") {
            std::cerr << "Usage: consumer [OPTIONS]\n"
                      << "\nOptions:\n"
                      << "  --host HOST          Producer host (default: 127.0.0.1)\n"
                      << "  --port PORT          Producer port (default: 9876)\n"
                      << "  --transport tcp|udp  Transport protocol (default: tcp)\n"
                      << "  --threads N          Thread pool size (default: 1 per core)\n"
                      << "  --file-dir DIR       Directory for source files (default: ./)\n"
                      << "  --max-messages N     Stop after N work units (0 = no limit)\n"
                      << "  --local              Force localhost connection\n"
                      << "  --gateway IP         Default gateway (default: 192.168.1.1)\n"
                       << "  --consumer-id ID     Consumer identifier\n"
                        << "  --handler TYPE       Work unit handler type (e.g. PWD)\n"
                        << "  --handler-config F   Config file for handler options\n"
                        << "  --result-file FILE   Write results to JSON lines file\n"
                        << "  --max-failures N     Stop after N failures (0 = no limit)\n"
                        << "  --max-duration SEC   Stop after N seconds (0 = no limit)\n"
                        << "  --timeout SEC        Close after N seconds without producer communication (0 = no limit)\n"
                        << "  --no-yield           Do not lower process priority when connecting to a localhost producer\n";
            return 0;
        } else {
            std::cerr << "Unknown option: " << arg << "\n";
            return 1;
        }
    }

    pc::SignalHandler::install();

    try {
        pc::Consumer consumer(config);
        consumer.run();
    } catch (const std::exception& e) {
        std::cerr << "[consumer] Fatal error: " << e.what() << "\n";
        return 2;
    }

    return 0;
}
