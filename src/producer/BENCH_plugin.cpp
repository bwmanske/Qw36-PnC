#include "producer/BENCH_plugin.h"
#include "common/util.h"
#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <atomic>
#include <iomanip>

namespace pc {
namespace fs = std::filesystem;

struct BENCHState {
    std::string source_file;
    int chunk_size = 128;
    int64_t file_size = 0;
    std::atomic<int64_t> offset{0};
    int64_t total_chunks = 0;
    std::atomic<int64_t> processed{0};
    std::atomic<int64_t> seq{0};
    std::chrono::steady_clock::time_point start_time;
    double transactions_per_second = 0.0;
};

static BENCHState* g_bench_state = nullptr;

void set_bench_source_file(const std::string& path) {
    if (g_bench_state) {
        g_bench_state->source_file = path;
        if (!path.empty() && fs::exists(path)) {
            g_bench_state->file_size = fs::file_size(path);
            g_bench_state->total_chunks = (g_bench_state->file_size + g_bench_state->chunk_size - 1) / g_bench_state->chunk_size;
        }
    }
}

static std::string base64_encode(const uint8_t* data, size_t len) {
    static const char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((len + 2) / 3) * 4);
    for (size_t i = 0; i < len; i += 3) {
        uint32_t o = (static_cast<uint32_t>(data[i]) << 16) |
                     (i + 1 < len ? static_cast<uint32_t>(data[i + 1]) << 8 : 0) |
                     (i + 2 < len ? static_cast<uint32_t>(data[i + 2]) : 0);
        out.push_back(table[(o >> 18) & 0x3F]);
        out.push_back(table[(o >> 12) & 0x3F]);
        out.push_back(i + 1 < len ? table[(o >> 6) & 0x3F] : '=');
        out.push_back(i + 2 < len ? table[o & 0x3F] : '=');
    }
    return out;
}

pc::TestPlugin create_bench_plugin() {
    pc::TestPlugin plugin;

    plugin.startup = [](const std::string& config_path, const nlohmann::json& resume_state) {
        if (g_bench_state) delete g_bench_state;
        g_bench_state = new BENCHState();

        if (!config_path.empty()) {
            std::ifstream file(config_path);
            if (file.is_open()) {
                try {
                    nlohmann::json cfg = nlohmann::json::parse(file);
                    g_bench_state->chunk_size = cfg.value("chunk_size", 128);
                    file.close();
                } catch (const std::exception& e) {
                    std::cerr << "[BENCH_StartUp] Error parsing config: " << e.what() << "\n";
                }
            } else {
                std::cerr << "[BENCH_StartUp] Cannot open config: " << config_path << "\n";
            }
        }

        if (!resume_state.empty()) {
            if (resume_state.contains("offset")) {
                g_bench_state->offset = resume_state["offset"].get<int64_t>();
            }
            if (resume_state.contains("processed")) {
                g_bench_state->processed = resume_state["processed"].get<int64_t>();
            }
            if (resume_state.contains("seq")) {
                g_bench_state->seq = resume_state["seq"].get<int64_t>();
            }
        }

        std::cout << "[BENCH_StartUp] chunk_size=" << g_bench_state->chunk_size
                  << " offset=" << g_bench_state->offset.load() << "\n";
    };

    plugin.next_unit = [](WorkUnitMessage& out) {
        if (!g_bench_state) return false;

        if (g_bench_state->source_file.empty()) return false;

        if (g_bench_state->offset.load() >= g_bench_state->file_size) return false;

        std::ifstream file(g_bench_state->source_file, std::ios::binary);
        if (!file.is_open()) {
            std::cerr << "[BENCH_NextUnit] Cannot open source file: " << g_bench_state->source_file << "\n";
            return false;
        }

        file.seekg(g_bench_state->offset.load(), std::ios::beg);
        int64_t remaining = g_bench_state->file_size - g_bench_state->offset.load();
        int64_t to_read = std::min(static_cast<int64_t>(g_bench_state->chunk_size), remaining);

        std::vector<uint8_t> chunk(static_cast<size_t>(to_read));
        file.read(reinterpret_cast<char*>(chunk.data()), to_read);
        file.close();

        std::string b64 = base64_encode(chunk.data(), static_cast<size_t>(to_read));
        std::string hash = sha256_bytes(chunk.data(), static_cast<size_t>(to_read));

        g_bench_state->seq++;

        out.job = nlohmann::json::object();
        out.job["task"] = "BENCH";
        out.job["offset"] = g_bench_state->offset.load();
        out.job["chunk_size"] = to_read;
        out.job["data"] = b64;
        out.job["hash"] = hash;
        out.job["seq"] = g_bench_state->seq.load();

        g_bench_state->offset += to_read;

        return true;
    };

    plugin.checkpoint = []() {
        nlohmann::json j;
        if (!g_bench_state) return j;

        j["offset"] = g_bench_state->offset.load();
        j["seq"] = g_bench_state->seq.load();

        return j;
    };

    plugin.status = []() {
        if (!g_bench_state) return std::string();
        std::ostringstream oss;
        int64_t off = g_bench_state->offset.load();
        int64_t fsz = g_bench_state->file_size;
        double pct = (fsz > 0) ? (100.0 * static_cast<double>(off) / static_cast<double>(fsz)) : 0.0;
        oss << "Offset:      " << off << " / " << fsz << " (" << std::fixed << std::setprecision(1) << pct << "%)\n"
            << "Chunk size:  " << g_bench_state->chunk_size;
        return oss.str();
    };

    plugin.exit_conditions = []() {
        if (!g_bench_state) return false;
        return g_bench_state->offset.load() >= g_bench_state->file_size;
    };

    return plugin;
}

} // namespace pc
