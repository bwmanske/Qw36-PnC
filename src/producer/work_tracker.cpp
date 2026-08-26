#include "producer/work_tracker.h"
#include <algorithm>
#include <chrono>
#include <iomanip>
#include <sstream>

namespace pc {

WorkTracker::WorkTracker() {}

void WorkTracker::add_pending(const WorkUnitEntry& entry) {
    std::lock_guard<std::mutex> lock(mtx_);
    entries_[entry.work_unit_id] = entry;
}

void WorkTracker::mark_sent(const std::string& work_unit_id, const std::string& consumer_id) {
    std::lock_guard<std::mutex> lock(mtx_);
    auto it = entries_.find(work_unit_id);
    if (it != entries_.end()) {
        it->second.status = WorkUnitStatus::Sent;
        it->second.consumer_id = consumer_id;
        it->second.sent_at = ""; // TODO: populate with current timestamp
    }
}

void WorkTracker::mark_completed(const std::string& work_unit_id) {
    std::lock_guard<std::mutex> lock(mtx_);
    auto it = entries_.find(work_unit_id);
    if (it != entries_.end()) {
        it->second.status = WorkUnitStatus::Completed;
        it->second.completed_at = ""; // TODO: populate with current timestamp
        last_completed_seq_ = it->second.seq;
        completed_count_++;
    }
}

void WorkTracker::mark_failed(const std::string& work_unit_id) {
    std::lock_guard<std::mutex> lock(mtx_);
    auto it = entries_.find(work_unit_id);
    if (it != entries_.end()) {
        it->second.status = WorkUnitStatus::Pending;
        it->second.consumer_id = "";
        it->second.sent_at = "";
        failed_count_++;
    }
}

std::vector<WorkUnitEntry> WorkTracker::get_pending(int count) {
    std::lock_guard<std::mutex> lock(mtx_);
    std::vector<WorkUnitEntry> pending;
    for (auto& [id, entry] : entries_) {
        if (entry.status == WorkUnitStatus::Pending) {
            pending.push_back(entry);
        }
    }
    // Dispatch in sequence order (FIFO) for deterministic, in-order work distribution.
    std::sort(pending.begin(), pending.end(),
              [](const WorkUnitEntry& a, const WorkUnitEntry& b) {
                  return a.seq < b.seq;
              });
    if (static_cast<int>(pending.size()) > count) {
        pending.resize(count);
    }
    return pending;
}

std::optional<WorkUnitEntry> WorkTracker::find(const std::string& work_unit_id) const {
    std::lock_guard<std::mutex> lock(mtx_);
    auto it = entries_.find(work_unit_id);
    if (it != entries_.end()) return it->second;
    return std::nullopt;
}

int64_t WorkTracker::last_completed_seq() const {
    std::lock_guard<std::mutex> lock(mtx_);
    return last_completed_seq_;
}

int64_t WorkTracker::completed_count() const {
    std::lock_guard<std::mutex> lock(mtx_);
    return completed_count_;
}

int64_t WorkTracker::pending_count() const {
    std::lock_guard<std::mutex> lock(mtx_);
    int64_t count = 0;
    for (const auto& [id, entry] : entries_) {
        if (entry.status == WorkUnitStatus::Pending ||
            entry.status == WorkUnitStatus::Sent) {
            count++;
        }
    }
    return count;
}

int64_t WorkTracker::failed_count() const {
    std::lock_guard<std::mutex> lock(mtx_);
    return failed_count_;
}

std::vector<std::string> WorkTracker::get_failed_for_consumer(const std::string& consumer_id) {
    std::lock_guard<std::mutex> lock(mtx_);
    std::vector<std::string> result;
    for (auto& [id, entry] : entries_) {
        if (entry.status == WorkUnitStatus::Sent &&
            entry.consumer_id == consumer_id) {
            entry.status = WorkUnitStatus::Pending;
            entry.consumer_id = "";
            result.push_back(id);
        }
    }
    return result;
}

CheckpointState WorkTracker::to_checkpoint(int64_t total_jobs) const {
    std::lock_guard<std::mutex> lock(mtx_);
    CheckpointState state;
    state.total_jobs = total_jobs;
    state.last_completed_seq = last_completed_seq_;
    state.completed_count = completed_count_;
    state.failed_count = failed_count_;
    state.pending_count = 0;
    for (const auto& [id, entry] : entries_) {
        if (entry.status == WorkUnitStatus::Pending ||
            entry.status == WorkUnitStatus::Sent) {
            state.pending_count++;
        }
    }
    return state;
}

} // namespace pc
