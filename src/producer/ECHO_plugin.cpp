#include "producer/ECHO_plugin.h"
#include "common/util.h"
#include <fstream>
#include <iostream>
#include <sstream>
#include <random>
#include <chrono>

namespace pc {

struct ECHOState {
    int payload_size = 64;
    int64_t total_units = 0;
    int64_t generated = 0;
    int64_t seq = 0;
    std::mt19937 rng;
};

static ECHOState* g_echo_state = nullptr;

pc::TestPlugin create_echo_plugin() {
    pc::TestPlugin plugin;

    plugin.startup = [](const std::string& config_path, const nlohmann::json& resume_state) {
        if (g_echo_state) delete g_echo_state;
        g_echo_state = new ECHOState();

        if (!config_path.empty()) {
            std::ifstream file(config_path);
            if (file.is_open()) {
                try {
                    nlohmann::json cfg = nlohmann::json::parse(file);
                    g_echo_state->payload_size = cfg.value("payload_size", 64);
                    g_echo_state->total_units = cfg.value("total_units", 0);
                    file.close();
                } catch (const std::exception& e) {
                    std::cerr << "[ECHO_StartUp] Error parsing config: " << e.what() << "\n";
                }
            } else {
                std::cerr << "[ECHO_StartUp] Cannot open config: " << config_path << "\n";
            }
        }

        if (!resume_state.empty()) {
            if (resume_state.contains("generated")) {
                g_echo_state->generated = resume_state["generated"].get<int64_t>();
            }
            if (resume_state.contains("seq")) {
                g_echo_state->seq = resume_state["seq"].get<int64_t>();
            }
        }

        g_echo_state->rng.seed(42);

        std::cout << "[ECHO_StartUp] payload_size=" << g_echo_state->payload_size
                  << " total_units=" << g_echo_state->total_units
                  << " generated=" << g_echo_state->generated << "\n";
    };

    plugin.next_unit = [](WorkUnitMessage& out) {
        if (!g_echo_state) return false;

        if (g_echo_state->total_units > 0 && g_echo_state->generated >= g_echo_state->total_units) {
            return false;
        }

        g_echo_state->seq++;
        g_echo_state->generated++;

        std::ostringstream oss;
        for (int i = 0; i < g_echo_state->payload_size; i++) {
            char c = 'A' + (g_echo_state->seq + i) % 26;
            oss << c;
        }
        std::string payload = oss.str();
        std::string hash = sha256_bytes(
            reinterpret_cast<const uint8_t*>(payload.data()), payload.size());

        out.job = nlohmann::json::object();
        out.job["task"] = "ECHO";
        out.job["payload"] = payload;
        out.job["hash"] = hash;
        out.job["seq"] = g_echo_state->seq;

        return true;
    };

    plugin.checkpoint = []() {
        nlohmann::json j;
        if (!g_echo_state) return j;

        j["generated"] = g_echo_state->generated;
        j["seq"] = g_echo_state->seq;
        j["payload_size"] = g_echo_state->payload_size;
        j["total_units"] = g_echo_state->total_units;

        return j;
    };

    plugin.exit_conditions = []() {
        if (!g_echo_state) return false;
        if (g_echo_state->total_units > 0) {
            return g_echo_state->generated >= g_echo_state->total_units;
        }
        return false;
    };

    return plugin;
}

} // namespace pc
