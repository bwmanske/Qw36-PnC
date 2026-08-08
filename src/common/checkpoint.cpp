#include "common/checkpoint.h"
#include <fstream>
#include <filesystem>
#include <stdexcept>
#include <iostream>
#include <chrono>
#include <iomanip>
#include <sstream>

namespace pc {
namespace fs = std::filesystem;

// ── CheckpointState ──────────────────────────────────────────────

nlohmann::json CheckpointState::to_json() const {
    nlohmann::json j;
    j["producer_id"] = producer_id;
    j["source_file"] = source_file;
    j["permutation"] = permutation;
    j["permutation_seed"] = permutation_seed;
    j["total_jobs"] = total_jobs;
    j["last_completed_seq"] = last_completed_seq;
    j["last_completed_work_unit_id"] = last_completed_work_unit_id;
    j["completed_count"] = completed_count;
    j["pending_count"] = pending_count;
    j["failed_count"] = failed_count;
    j["checkpoint_timestamp"] = checkpoint_timestamp;
    nlohmann::json consumers = nlohmann::json::array();
    for (const auto& c : consumers_connected) {
        nlohmann::json cj;
        cj["consumer_id"] = c.consumer_id;
        cj["pending_units"] = c.pending_units;
        consumers.push_back(cj);
    }
    j["consumers_connected"] = consumers;
    if (plugin_state.has_value()) {
        j["plugin_state"] = *plugin_state;
    }
    return j;
}

CheckpointState CheckpointState::from_json(const nlohmann::json& j) {
    CheckpointState state;
    state.producer_id = j.value("producer_id", "");
    state.source_file = j.value("source_file", "");
    state.permutation = j.value("permutation", "");
    state.permutation_seed = j.value("permutation_seed", 0);
    state.total_jobs = j.value("total_jobs", 0);
    state.last_completed_seq = j.value("last_completed_seq", -1);
    state.last_completed_work_unit_id = j.value("last_completed_work_unit_id", "");
    state.completed_count = j.value("completed_count", 0);
    state.pending_count = j.value("pending_count", 0);
    state.failed_count = j.value("failed_count", 0);
    state.checkpoint_timestamp = j.value("checkpoint_timestamp", "");
    if (j.contains("consumers_connected")) {
        for (const auto& cj : j["consumers_connected"]) {
            ConsumerState cs;
            cs.consumer_id = cj.value("consumer_id", "");
            cs.pending_units = cj.value("pending_units", 0);
            state.consumers_connected.push_back(cs);
        }
    }
    if (j.contains("plugin_state")) {
        state.plugin_state = j["plugin_state"].get<nlohmann::json>();
    }
    return state;
}

// ── CheckpointManager ────────────────────────────────────────────

CheckpointManager::CheckpointManager(const std::string& directory)
    : dir_(directory),
      primary_path_(directory + "/state.json"),
      backup_path_(directory + "/state.backup.json") {
    fs::create_directories(directory);
}

void CheckpointManager::save(const CheckpointState& state) {
    std::string json_str = state.to_json().dump(2);

    // Write backup first (copy of current primary if it exists)
    if (fs::exists(primary_path_)) {
        std::ifstream in(primary_path_);
        if (in.is_open()) {
            std::ofstream out(backup_path_, std::ios::trunc);
            if (out.is_open()) {
                out << in.rdbuf();
                out.close();
            }
            in.close();
        }
    }

    // Write primary
    std::ofstream out(primary_path_, std::ios::trunc);
    if (!out.is_open()) {
        std::cerr << "[checkpoint] Failed to open " << primary_path_ << " for writing\n";
        return;
    }
    out << json_str;
    out.close();
}

std::optional<CheckpointState> CheckpointManager::load() const {
    // Try primary first
    if (fs::exists(primary_path_)) {
        std::ifstream in(primary_path_);
        if (in.is_open()) {
            try {
                nlohmann::json j = nlohmann::json::parse(in);
                in.close();
                return CheckpointState::from_json(j);
            } catch (const std::exception& e) {
                std::cerr << "[checkpoint] Primary state.json is corrupt: " << e.what() << "\n";
                in.close();
            }
        }
    }

    // Fall back to backup
    if (fs::exists(backup_path_)) {
        std::ifstream in(backup_path_);
        if (in.is_open()) {
            try {
                nlohmann::json j = nlohmann::json::parse(in);
                in.close();
                std::cerr << "[checkpoint] Using backup state.backup.json\n";
                return CheckpointState::from_json(j);
            } catch (const std::exception& e) {
                std::cerr << "[checkpoint] Backup state.backup.json is corrupt: " << e.what() << "\n";
                in.close();
            }
        }
    }

    return std::nullopt;
}

bool CheckpointManager::exists() const {
    return fs::exists(primary_path_) || fs::exists(backup_path_);
}

} // namespace pc
