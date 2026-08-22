#include "consumer/ECHO_Handler.h"
#include "common/util.h"
#include <iostream>
#include <fstream>
#include <chrono>
#include <thread>
#include <cmath>

namespace pc {

std::string ECHO_Handler::type() const {
    return "ECHO";
}

void ECHO_Handler::configure(const std::string& config_path) {
    if (config_path.empty()) return;

    std::ifstream file(config_path);
    if (!file.is_open()) {
        std::cerr << "[ECHO_Handler] Cannot open config: " << config_path << "\n";
        return;
    }

    try {
        nlohmann::json cfg = nlohmann::json::parse(file);
        max_delay_sec_ = cfg.value("max_delay_sec", 0.0);
        file.close();

        rng_.seed(std::chrono::steady_clock::now().time_since_epoch().count());
        dist_ = std::uniform_real_distribution<double>(0.0, 1.0);

        std::cout << "[ECHO_Handler] Delay enabled: max=" << max_delay_sec_ << "s\n";
    } catch (const std::exception& e) {
        std::cerr << "[ECHO_Handler] Error parsing config: " << e.what() << "\n";
    }
}

ResultMessage ECHO_Handler::handle(const WorkUnitMessage& work) {
    ResultMessage result;
    result.work_unit_id = work.work_unit_id;
    result.seq = work.seq;
    result.timestamp = "";

    auto start = std::chrono::steady_clock::now();

    try {
        std::string payload = work.job.value("payload", "");
        std::string expected_hash = work.job.value("hash", "");

        std::string actual_hash = sha256_bytes(
            reinterpret_cast<const uint8_t*>(payload.data()), payload.size());

        bool match = (actual_hash == expected_hash);

        // Apply power-law distributed delay if configured
        double delay_ms = 0.0;
        if (max_delay_sec_ > 0) {
            double r;
            {
                std::lock_guard<std::mutex> lock(rng_mutex_);
                r = dist_(rng_);
            }
            // Power-law: delay = max_delay * (1 - r^1.64)
            // Small delays are common, large delays are rare
            delay_ms = max_delay_sec_ * 1000.0 * (1.0 - std::pow(r, 1.64));
            std::this_thread::sleep_for(std::chrono::microseconds(static_cast<int64_t>(delay_ms * 1000.0)));
        }

        auto end = std::chrono::steady_clock::now();
        auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

        result.status = "success";
        result.result = nlohmann::json::object();
        result.result["match"] = match;
        result.result["payload_size"] = static_cast<int>(payload.size());
        result.result["expected_hash"] = expected_hash;
        result.result["actual_hash"] = actual_hash;
        result.result["delay_ms"] = static_cast<int>(delay_ms);
        result.result["duration_ms"] = duration_ms;

        if (!match) {
            std::cerr << "[ECHO_Handler] Hash mismatch for " << work.work_unit_id << "\n";
        }

    } catch (const std::exception& e) {
        std::cerr << "[ECHO_Handler] Processing failed for " << work.work_unit_id
                  << ": " << e.what() << "\n";
        result.status = "failure";
        result.result = nlohmann::json::object();
        result.result["error"] = e.what();
    }

    return result;
}

} // namespace pc
