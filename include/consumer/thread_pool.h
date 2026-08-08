#ifndef PC_THREAD_POOL_H
#define PC_THREAD_POOL_H

#include <vector>
#include <thread>
#include <atomic>
#include <functional>
#include <mutex>
#include <memory>
#include "common/types.h"
#include "common/message.h"
#include "common/queue.h"
#include "consumer/work_unit_handler.h"

namespace pc {

class ThreadPool {
public:
    explicit ThreadPool(size_t num_threads);
    ~ThreadPool();

    void start();
    void shutdown();

    void submit(WorkUnitMessage work);
    size_t idle_count() const;
    size_t active_count() const;
    size_t total_completed() const;
    size_t total_failed() const;

    void set_handler(std::shared_ptr<IWorkUnitHandler> handler);

    using ResultCallback = std::function<void(const ResultMessage&)>;
    void set_result_callback(ResultCallback cb);

    using IdleCallback = std::function<void(size_t idle_threads)>;
    void set_idle_callback(IdleCallback cb);

private:
    void worker_loop();

    size_t num_threads_;
    std::vector<std::thread> workers_;
    BoundedQueue<WorkUnitMessage> queue_;

    std::atomic<size_t> idle_count_{0};
    std::atomic<size_t> active_count_{0};
    std::atomic<size_t> total_completed_{0};
    std::atomic<size_t> total_failed_{0};

    std::shared_ptr<IWorkUnitHandler> handler_;
    ResultCallback result_callback_;
    IdleCallback idle_callback_;
    std::mutex callback_mutex_;
};

} // namespace pc

#endif // PC_THREAD_POOL_H
