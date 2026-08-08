#ifndef PC_CONSUMER_H
#define PC_CONSUMER_H

#include <string>
#include <atomic>
#include <thread>
#include <memory>
#include <chrono>
#include <list>
#include <unordered_set>
#include "common/types.h"
#include "common/message.h"
#include "common/socket.h"
#include "common/queue.h"
#include "consumer/thread_pool.h"
#include "consumer/work_unit_handler.h"
#include "consumer/result_sink.h"

namespace pc {

struct ConsumerConfig {
    std::string host = "127.0.0.1";
    uint16_t port = 9876;
    Transport transport = Transport::TCP;
    int threads = 0;
    std::string file_dir = "./";
    int max_messages = 0;
    bool local = false;
    std::string gateway = "192.168.1.1";
    std::string consumer_id = "";
    std::string handler_type = "";
};

class Consumer {
public:
    explicit Consumer(const ConsumerConfig& config);
    ~Consumer();

    void run();
    void shutdown();

private:
    void connect_to_producer();
    void receiver_loop();
    void download_source_file(const std::string& source_file, const std::string& source_hash);
    void send_work_request(int threads_available);
    void send_result(const ResultMessage& result);
    void print_statistics();
    std::string generate_consumer_id();
    std::string now_iso();
    std::string compute_sha256(const std::string& path);
    bool is_completed(const std::string& work_unit_id);
    void mark_completed(const std::string& work_unit_id);

    ConsumerConfig config_;
    std::string consumer_id_;
    Socket client_socket_;

    std::unique_ptr<ThreadPool> pool_;
    BoundedQueue<WorkUnitMessage> work_queue_;

    std::atomic<bool> running_{false};
    std::atomic<bool> file_ready_{false};

    std::thread receiver_thread_;

    int64_t total_received_ = 0;
    int64_t total_discarded_ = 0;
    int64_t first_seq_ = -1;
    int64_t last_seq_ = -1;

    std::string source_file_;
    std::string local_file_path_;
    std::mutex source_file_mutex_;
    std::shared_ptr<IWorkUnitHandler> handler_;
    std::shared_ptr<IResultSink> sink_;

    std::chrono::steady_clock::time_point last_request_time_;

    std::list<std::string> completed_ids_lru_;
    std::unordered_set<std::string> completed_ids_set_;
    std::mutex completed_ids_mutex_;
    static constexpr size_t kMaxCompletedIds = 3000;
};

} // namespace pc

#endif // PC_CONSUMER_H
