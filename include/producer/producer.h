#ifndef PC_PRODUCER_H
#define PC_PRODUCER_H

#include <string>
#include <vector>
#include <atomic>
#include <thread>
#include <memory>
#include <unordered_map>
#include <chrono>
#include "common/types.h"
#include "common/message.h"
#include "common/socket.h"
#include "common/queue.h"
#include "common/checkpoint.h"
#include "producer/work_tracker.h"
#include "producer/test_plugin.h"
#include <unordered_set>

namespace pc {

struct AdditionalFile {
    std::string name;
    uint64_t size = 0;
    std::string sha256;
};

struct ProducerConfig {
    std::string file_path;
    uint16_t port = 9876;
    Transport transport = Transport::TCP;
    std::string permutation = "sequential";
    int64_t seed = 0;
    int duration = 0;
    std::string gateway = "192.168.1.1";
    std::string checkpoint_dir;
    bool resume = false;
    std::string test_type = "";
    bool transfer_siblings = false;
    int max_time_sec = 0;
    bool status_enabled = true;
};

class Producer {
public:
    explicit Producer(const ProducerConfig& config);
    ~Producer();

    void run();
    void shutdown();

private:
    void load_job_config();
    void load_checkpoint();
    void init_plugin();
    void dispatcher_loop();
    void handle_client(Socket client_socket);
    void handle_work_request(const WorkRequestMessage& req, Socket& client);
    void handle_result(const ResultMessage& result);
    void checkpoint_loop();
    void status_loop();
    void render_status(const std::vector<std::string>& lines);
    void log(const std::string& msg);
    void write_final_checkpoint();
    void print_statistics();
    std::string generate_producer_id();
    std::string now_iso();
    void file_transfer_loop();
    void handle_file_transfer(Socket client_socket);
    void monitor_connections();
    void register_consumer(const std::string& consumer_id, Socket& socket);
    void register_consumer_udp(const std::string& consumer_id, const std::string& address, uint16_t port);
    void unregister_consumer(const std::string& consumer_id);
    void update_consumer_activity(const std::string& consumer_id);
    void udp_loop();
    void handle_udp_message(const std::string& consumer_id, const std::string& address, uint16_t port, const std::string& frame);
    void handle_udp_work_request(const WorkRequestMessage& req, const std::string& address, uint16_t port);

    ProducerConfig config_;
    std::string producer_id_;
    std::string source_file_;
    std::string config_file_;
    std::string test_type_;
    int max_units_ = 0;
    int max_idle_seconds_ = 300;

    std::chrono::steady_clock::time_point start_time_;
    int64_t next_seq_ = 0;
    int64_t total_dispatched_ = 0;
    int64_t total_generated_ = 0;
    nlohmann::json plugin_resume_state_;

    WorkTracker tracker_;
    CheckpointManager checkpoint_mgr_;
    TestPlugin plugin_;

    BoundedQueue<WorkUnitMessage> dispatch_queue_;
    BoundedQueue<std::pair<Socket, std::string>> result_queue_;

    std::atomic<bool> running_{false};
    std::atomic<bool> checkpoint_running_{false};
    std::atomic<bool> shutdown_done_{false};

    Socket server_socket_;
    std::vector<std::thread> client_threads_;
    std::mutex client_mutex_;
    std::thread dispatcher_thread_;
    std::thread checkpoint_thread_;

    // In-place console status display (1 Hz). status_mutex_ serializes the
    // status render against scrolling event logs (see log()).
    bool status_enabled_ = true;
    std::atomic<bool> status_running_{false};
    std::thread status_thread_;
    std::mutex status_mutex_;
    size_t status_lines_prev_ = 0;

    struct ConsumerInfo {
        Socket* socket = nullptr;
        std::string address;
        uint16_t port = 0;
        std::chrono::steady_clock::time_point last_activity;
        std::chrono::steady_clock::time_point registered_at;
    };
    std::unordered_map<std::string, ConsumerInfo> connected_consumers_;
    std::mutex consumers_mutex_;
    std::atomic<bool> monitor_running_{false};
    std::thread monitor_thread_;

    Socket file_transfer_socket_;
    std::thread file_transfer_thread_;
    std::atomic<bool> file_transfer_running_{false};

    std::thread udp_thread_;
    std::atomic<bool> udp_running_{false};

    std::vector<AdditionalFile> additional_files_;
    void scan_additional_files();
};

} // namespace pc

#endif // PC_PRODUCER_H
