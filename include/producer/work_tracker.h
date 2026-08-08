#ifndef PC_WORK_TRACKER_H
#define PC_WORK_TRACKER_H

#include <string>
#include <vector>
#include <mutex>
#include <unordered_map>
#include <nlohmann/json.hpp>
#include "common/types.h"
#include "common/checkpoint.h"

namespace pc {

class WorkTracker {
public:
    WorkTracker();

    void add_pending(const WorkUnitEntry& entry);
    void mark_sent(const std::string& work_unit_id, const std::string& consumer_id);
    void mark_completed(const std::string& work_unit_id);
    void mark_failed(const std::string& work_unit_id);

    std::vector<WorkUnitEntry> get_pending(int count);
    std::optional<WorkUnitEntry> find(const std::string& work_unit_id) const;

    int64_t last_completed_seq() const;
    int64_t completed_count() const;
    int64_t pending_count() const;
    int64_t failed_count() const;

    std::vector<std::string> get_failed_for_consumer(const std::string& consumer_id);
    CheckpointState to_checkpoint(int64_t total_jobs) const;

private:
    mutable std::mutex mtx_;
    std::unordered_map<std::string, WorkUnitEntry> entries_;
    int64_t last_completed_seq_ = -1;
    int64_t completed_count_ = 0;
    int64_t failed_count_ = 0;
};

} // namespace pc

#endif // PC_WORK_TRACKER_H
