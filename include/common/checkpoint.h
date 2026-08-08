#ifndef PC_CHECKPOINT_H
#define PC_CHECKPOINT_H

#include <string>
#include <vector>
#include <optional>
#include <nlohmann/json.hpp>
#include "common/types.h"

namespace pc {

struct CheckpointState {
    std::string producer_id;
    std::string source_file;
    std::string permutation;
    int64_t permutation_seed;
    int64_t total_jobs;
    int64_t last_completed_seq;
    std::string last_completed_work_unit_id;
    int64_t completed_count;
    int64_t pending_count;
    int64_t failed_count;
    std::string checkpoint_timestamp;
    std::vector<ConsumerState> consumers_connected;
    std::optional<nlohmann::json> plugin_state;

    nlohmann::json to_json() const;
    static CheckpointState from_json(const nlohmann::json& j);
};

class CheckpointManager {
public:
    explicit CheckpointManager(const std::string& directory);

    void save(const CheckpointState& state);
    std::optional<CheckpointState> load() const;
    bool exists() const;

    const std::string& directory() const { return dir_; }
    const std::string& primary_path() const { return primary_path_; }
    const std::string& backup_path() const { return backup_path_; }

private:
    std::string dir_;
    std::string primary_path_;
    std::string backup_path_;
};

} // namespace pc

#endif // PC_CHECKPOINT_H
