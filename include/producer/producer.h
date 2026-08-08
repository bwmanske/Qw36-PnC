#ifndef PC_PRODUCER_H
#define PC_PRODUCER_H

#include <string>
#include <vector>
#include <atomic>
#include <thread>
#include <memory>
#include <unordered_map>
#include "common/types.h"
#include "common/message.h"
#include "common/socket.h"
#include "common/queue.h"
#include "common/checkpoint.h"
#include "producer/work_tracker.h"
#include "producer/test_plugin.h"
#include <unordered_set>

namespace pc {

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
    void write_final_checkpoint();
    void print_statistics();
    std::string generate_producer_id();
    std::string now_iso();
    void file_transfer_loop();
    void handle_file_transfer(Socket client_socket);

    ProducerConfig config_;
    std::string producer_id_;
    std::string source_file_;
    std::string config_file_;
    std::string test_type_;
    int max_units_ = 0;
    int max_idle_seconds_ = 300;

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

    Socket server_socket_;
    std::vector<std::thread> client_threads_;
    std::mutex client_mutex_;
    std::thread dispatcher_thread_;
    std::thread checkpoint_thread_;

    std::unordered_map<std::string, Socket*> connected_consumers_;

    Socket file_transfer_socket_;
    std::thread file_transfer_thread_;
    std::atomic<bool> file_transfer_running_{false};
};

} // namespace pc

#endif // PC_PRODUCER_H
