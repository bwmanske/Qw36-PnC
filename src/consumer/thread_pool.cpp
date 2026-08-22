#include "consumer/thread_pool.h"
#include "common/signal_handler.h"
#include <chrono>
#include <iostream>

namespace pc {

ThreadPool::ThreadPool(size_t num_threads)
    : num_threads_(num_threads),
      queue_(4096) {
    if (num_threads_ == 0) {
        num_threads_ = std::thread::hardware_concurrency();
        if (num_threads_ == 0) num_threads_ = 4;
    }
}

ThreadPool::~ThreadPool() {
    shutdown();
}

void ThreadPool::start() {
    idle_count_ = num_threads_;
    for (size_t i = 0; i < num_threads_; i++) {
        workers_.emplace_back(&ThreadPool::worker_loop, this);
    }
}

void ThreadPool::shutdown() {
    queue_.shutdown();
    for (auto& w : workers_) {
        if (w.joinable()) w.join();
    }
    workers_.clear();
}

void ThreadPool::submit(WorkUnitMessage work) {
    queue_.push(std::move(work));
}

size_t ThreadPool::idle_count() const {
    return idle_count_.load();
}

size_t ThreadPool::active_count() const {
    return active_count_.load();
}

bool ThreadPool::queue_empty() const {
    return queue_.empty();
}

size_t ThreadPool::total_completed() const {
    return total_completed_.load();
}

size_t ThreadPool::total_failed() const {
    return total_failed_.load();
}

void ThreadPool::set_result_callback(ResultCallback cb) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    result_callback_ = std::move(cb);
}

void ThreadPool::set_idle_callback(IdleCallback cb) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    idle_callback_ = std::move(cb);
}

void ThreadPool::set_handler(std::shared_ptr<IWorkUnitHandler> handler) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    handler_ = handler;
}

std::vector<WorkUnitMessage> ThreadPool::drain_pending() {
    std::vector<WorkUnitMessage> pending;

    // Drain queued items
    WorkUnitMessage work;
    while (true) {
        auto opt = queue_.try_pop(std::chrono::milliseconds(1));
        if (!opt) break;
        pending.push_back(std::move(*opt));
    }

    // Collect active work units
    {
        std::lock_guard<std::mutex> lock(active_work_mutex_);
        for (auto& [id, w] : active_work_) {
            pending.push_back(std::move(w));
        }
        active_work_.clear();
    }

    return pending;
}

void ThreadPool::worker_loop() {
    while (!SignalHandler::is_stop_requested()) {
        WorkUnitMessage work;
        try {
            work = queue_.pop();
        } catch (const std::exception&) {
            break;
        }

        active_count_++;
        idle_count_--;

        {
            std::lock_guard<std::mutex> lock(active_work_mutex_);
            active_work_[work.work_unit_id] = work;
        }

        ResultMessage result;
        {
            std::lock_guard<std::mutex> lock(callback_mutex_);
            if (handler_) {
                result = handler_->handle(work);
            } else {
                result.work_unit_id = work.work_unit_id;
                result.seq = work.seq;
                result.status = "failure";
                result.result = nlohmann::json::object();
                result.result["error"] = "no handler registered";
            }
        }

        {
            std::lock_guard<std::mutex> lock(active_work_mutex_);
            active_work_.erase(work.work_unit_id);
        }

        active_count_--;

        {
            std::lock_guard<std::mutex> lock(callback_mutex_);
            if (result_callback_) {
                result_callback_(result);
            }
        }

        if (result.status == "success") {
            total_completed_++;
        } else {
            total_failed_++;
        }

        idle_count_++;

        // Check if we should request more work
        if (queue_.empty()) {
            std::lock_guard<std::mutex> lock(callback_mutex_);
            if (idle_callback_) {
                idle_callback_(idle_count_.load());
            }
        }
    }
}

} // namespace pc
