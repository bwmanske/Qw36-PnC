#include "producer/producer.h"
#include "common/signal_handler.h"
#include "common/util.h"
#include "producer/PWD_plugin.h"
#include "producer/BENCH_plugin.h"
#include <fstream>
#include <iostream>
#include <algorithm>
#include <random>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <filesystem>
#include <cstring>

namespace pc {
namespace fs = std::filesystem;

Producer::Producer(const ProducerConfig& config)
    : config_(config),
      checkpoint_mgr_(config.checkpoint_dir),
      dispatch_queue_(1024),
      result_queue_(256) {
    producer_id_ = generate_producer_id();
}

Producer::~Producer() {
    shutdown();
}

void Producer::run() {
    load_job_config();

    if (config_.resume && checkpoint_mgr_.exists()) {
        load_checkpoint();
    }

    init_plugin();

    running_ = true;
    checkpoint_running_ = true;
    file_transfer_running_ = true;

    checkpoint_thread_ = std::thread(&Producer::checkpoint_loop, this);
    file_transfer_thread_ = std::thread(&Producer::file_transfer_loop, this);
    dispatcher_thread_ = std::thread(&Producer::dispatcher_loop, this);

    server_socket_ = Socket(config_.transport);
    server_socket_.bind("0.0.0.0", config_.port);

    try {
        file_transfer_socket_ = Socket(Transport::TCP);
        file_transfer_socket_.bind("0.0.0.0", config_.port + 1);
        file_transfer_socket_.listen(5);
        std::cout << "[producer] File transfer server on 0.0.0.0:" << (config_.port + 1) << "\n";
    } catch (const std::exception& e) {
        std::cerr << "[producer] File transfer server failed: " << e.what() << "\n";
    }

    if (config_.transport == Transport::TCP) {
        server_socket_.listen(5);
        std::cout << "[producer] Listening on 0.0.0.0:" << config_.port << " (TCP)\n";
    } else {
        std::cout << "[producer] Sending on port " << config_.port << " (UDP)\n";
    }

    if (config_.transport == Transport::TCP) {
        while (running_ && !SignalHandler::is_stop_requested()) {
            try {
                Socket client = server_socket_.accept();
                std::thread t([this, c = std::move(client)]() mutable {
                    handle_client(std::move(c));
                });
                t.detach();
            } catch (const std::exception& e) {
                if (running_) {
                    std::cerr << "[producer] Accept error: " << e.what() << "\n";
                }
            }
        }
    }

    shutdown();
}

void Producer::shutdown() {
    running_ = false;
    checkpoint_running_ = false;
    file_transfer_running_ = false;

    dispatch_queue_.shutdown();
    result_queue_.shutdown();

    if (dispatcher_thread_.joinable()) dispatcher_thread_.join();
    if (checkpoint_thread_.joinable()) checkpoint_thread_.join();
    if (file_transfer_thread_.joinable()) file_transfer_thread_.join();

    write_final_checkpoint();
    server_socket_.close();
    print_statistics();
}

void Producer::load_job_config() {
    std::ifstream file(config_.file_path);
    if (!file.is_open()) {
        std::cerr << "[producer] Cannot open config file: " << config_.file_path << "\n";
        exit(1);
    }

    nlohmann::json cfg;
    try {
        cfg = nlohmann::json::parse(file);
    } catch (const std::exception& e) {
        std::cerr << "[producer] Invalid JSON in config file: " << e.what() << "\n";
        exit(1);
    }
    file.close();

    test_type_ = cfg.value("test_type", "");
    config_file_ = cfg.value("config_file", "");
    source_file_ = cfg.value("source_file", "");
    max_units_ = cfg.value("max_units", 0);
    max_idle_seconds_ = cfg.value("max_idle_seconds", 300);

    if (config_.duration == 0) {
        config_.duration = cfg.value("duration", 0);
    }

    std::cout << "[producer] Config: type=" << test_type_
              << " config_file=" << config_file_
              << " source_file=" << source_file_
              << " duration=" << config_.duration
              << " max_units=" << max_units_ << "\n";

    if (test_type_.empty()) {
        std::cerr << "[producer] Error: test_type is required in config file\n";
        exit(1);
    }
}

void Producer::load_checkpoint() {
    auto state = checkpoint_mgr_.load();
    if (state.has_value()) {
        std::cout << "[producer] Resuming from checkpoint: last_completed_seq="
                  << state->last_completed_seq << "\n";
        next_seq_ = state->last_completed_seq + 1;

        if (state->plugin_state.has_value()) {
            plugin_resume_state_ = *state->plugin_state;
        }
    }
}

void Producer::init_plugin() {
    if (test_type_ == "PWD") {
        plugin_ = create_pwd_plugin();
    } else if (test_type_ == "BENCH") {
        plugin_ = create_bench_plugin();
    } else {
        std::cerr << "[producer] Unknown test_type: " << test_type_ << "\n";
        exit(1);
    }

    if (!plugin_.is_valid()) {
        std::cerr << "[producer] Plugin initialization failed for " << test_type_ << "\n";
        exit(1);
    }

    std::string config_path = config_file_.empty() ? "" : config_file_;
    plugin_.startup(config_path, plugin_resume_state_);

    if (test_type_ == "BENCH" && !source_file_.empty()) {
        if (fs::exists(source_file_)) {
            auto size = fs::file_size(source_file_);
            std::cout << "[producer] BENCH source file: " << source_file_
                      << " (" << size << " bytes)\n";
            set_bench_source_file(source_file_);
        } else {
            std::cerr << "[producer] BENCH source file not found: " << source_file_ << "\n";
            exit(1);
        }
    }
}

void Producer::dispatcher_loop() {
    while (running_ && !SignalHandler::is_stop_requested()) {
        if (plugin_.exit_conditions && plugin_.exit_conditions()) {
            std::cout << "[producer] Plugin exit conditions met, shutting down\n";
            running_ = false;
            break;
        }

        if (config_.duration > 0 && total_dispatched_ > 0) {
            // TODO: Track start time and check duration
        }

        if (max_units_ > 0 && tracker_.completed_count() >= static_cast<int64_t>(max_units_)) {
            std::cout << "[producer] Max units reached (" << max_units_ << "), shutting down\n";
            running_ = false;
            break;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

void Producer::handle_client(Socket client_socket) {
    std::string consumer_id;
    try {
        while (running_ && client_socket.is_open()) {
            std::string frame = recv_frame(client_socket);
            nlohmann::json j = nlohmann::json::parse(frame);
            std::string msg_type = j.value("msg_type", "");

            if (msg_type == "work_request") {
                WorkRequestMessage req = WorkRequestMessage::from_json(j);
                consumer_id = req.consumer_id;
                handle_work_request(req, client_socket);
            } else if (msg_type == "result") {
                ResultMessage result = ResultMessage::from_json(j);
                handle_result(result);
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "[producer] Client error: " << e.what() << "\n";
    }

    if (!consumer_id.empty()) {
        tracker_.get_failed_for_consumer(consumer_id);
    }
    client_socket.close();
}

void Producer::handle_work_request(const WorkRequestMessage& req, Socket& client) {
    int to_dispatch = req.threads_available;

    for (int i = 0; i < to_dispatch; i++) {
        WorkUnitMessage msg;
        msg.test_type = test_type_;
        msg.source_file = source_file_;
        msg.work_unit_id = producer_id_ + "-" + std::to_string(next_seq_);
        msg.seq = next_seq_;
        msg.timestamp = now_iso();
        msg.producer_id = producer_id_;

        if (!plugin_.next_unit(msg)) {
            std::cout << "[producer] Plugin exhausted, no more work units\n";
            break;
        }

        total_generated_++;

        WorkUnitEntry entry;
        entry.work_unit_id = msg.work_unit_id;
        entry.seq = msg.seq;
        entry.job = msg.job;
        entry.status = WorkUnitStatus::Pending;
        tracker_.add_pending(entry);

        tracker_.mark_sent(entry.work_unit_id, req.consumer_id);
        total_dispatched_++;
        next_seq_++;

        try {
            send_frame(client, msg.to_string());
        } catch (const std::exception& e) {
            std::cerr << "[producer] Send error: " << e.what() << "\n";
            break;
        }
    }
}

void Producer::handle_result(const ResultMessage& result) {
    if (result.status == "success") {
        tracker_.mark_completed(result.work_unit_id);
    } else {
        tracker_.mark_failed(result.work_unit_id);
    }
}

void Producer::checkpoint_loop() {
    auto last_write = std::chrono::steady_clock::now();
    while (checkpoint_running_) {
        std::this_thread::sleep_for(std::chrono::seconds(10));
        auto now = std::chrono::steady_clock::now();
        if (now - last_write >= std::chrono::seconds(60)) {
            auto state = tracker_.to_checkpoint(total_generated_);
            state.producer_id = producer_id_;
            state.source_file = source_file_;
            state.permutation = config_.permutation;
            state.permutation_seed = config_.seed;
            state.checkpoint_timestamp = now_iso();
            state.plugin_state = plugin_.checkpoint();
            checkpoint_mgr_.save(state);
            last_write = now;
        }
    }
}

void Producer::write_final_checkpoint() {
    auto state = tracker_.to_checkpoint(total_generated_);
    state.producer_id = producer_id_;
    state.source_file = source_file_;
    state.permutation = config_.permutation;
    state.permutation_seed = config_.seed;
    state.checkpoint_timestamp = now_iso();
    state.plugin_state = plugin_.checkpoint();
    checkpoint_mgr_.save(state);
    std::cout << "[producer] Checkpoint written to " << checkpoint_mgr_.primary_path() << "\n";
}

void Producer::print_statistics() {
    std::cout << "\n=== Producer Statistics ===\n";
    std::cout << "Test type:             " << test_type_ << "\n";
    std::cout << "Work units generated:  " << total_generated_ << "\n";
    std::cout << "Work units dispatched: " << total_dispatched_ << "\n";
    std::cout << "Work units completed:  " << tracker_.completed_count() << "\n";
    std::cout << "Work units failed:     " << tracker_.failed_count() << "\n";
    std::cout << "Work units pending:    " << tracker_.pending_count() << "\n";
    std::cout << "=========================\n";
}

std::string Producer::generate_producer_id() {
    return "prod-" + std::to_string(static_cast<unsigned>(std::hash<std::string>{}(
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())) % 10000));
}

std::string Producer::now_iso() {
    auto now = std::chrono::system_clock::now();
    auto t = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;
    std::tm tm_buf;
#ifdef _WIN32
    localtime_s(&tm_buf, &t);
#else
    localtime_r(&t, &tm_buf);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm_buf, "%Y-%m-%dT%H:%M:%S")
        << '.' << std::setfill('0') << std::setw(3) << ms.count() << "Z";
    return oss.str();
}

void Producer::file_transfer_loop() {
    while (file_transfer_running_ && !SignalHandler::is_stop_requested()) {
        try {
            Socket client = file_transfer_socket_.accept();
            std::thread t([this, c = std::move(client)]() mutable {
                handle_file_transfer(std::move(c));
            });
            t.detach();
        } catch (const std::exception& e) {
            if (file_transfer_running_) {
                std::cerr << "[producer] File transfer accept error: " << e.what() << "\n";
            }
        }
    }
}

void Producer::handle_file_transfer(Socket client_socket) {
    try {
        std::vector<uint8_t> request_buf(4096);
        ssize_t n = client_socket.recv_data(request_buf.data(), request_buf.size());
        if (n <= 0) {
            std::cerr << "[producer] File transfer: empty request\n";
            return;
        }

        if (request_buf[0] != 0x01) {
            std::cerr << "[producer] File transfer: invalid request code\n";
            return;
        }

        std::string filename(reinterpret_cast<char*>(request_buf.data() + 1));
        size_t null_pos = filename.find('\0');
        if (null_pos != std::string::npos) filename.erase(null_pos);

        std::cout << "[producer] File transfer request: " << filename << "\n";

        std::string file_path;
        fs::path config_dir = fs::path(config_.file_path).parent_path();

        if (fs::exists(filename)) {
            file_path = fs::canonical(filename).string();
        } else if (fs::exists(config_dir / filename)) {
            file_path = fs::canonical(config_dir / filename).string();
        } else if (!source_file_.empty() && fs::exists(source_file_) &&
                   fs::path(source_file_).filename().string() == filename) {
            file_path = fs::canonical(source_file_).string();
        } else if (fs::exists(config_.file_path) &&
                   fs::path(config_.file_path).filename().string() == filename) {
            file_path = fs::canonical(config_.file_path).string();
        } else {
            std::cerr << "[producer] File not found: " << filename << "\n";
            uint32_t zero = 0;
            uint32_t net_zero = htonl(zero);
            client_socket.send_data(reinterpret_cast<const uint8_t*>(&net_zero), 4);
            return;
        }

        std::ifstream file(file_path, std::ios::binary | std::ios::ate);
        if (!file.is_open()) {
            std::cerr << "[producer] Cannot open file: " << file_path << "\n";
            uint32_t zero = 0;
            uint32_t net_zero = htonl(zero);
            client_socket.send_data(reinterpret_cast<const uint8_t*>(&net_zero), 4);
            return;
        }

        std::streamsize fsize = file.tellg();
        file.seekg(0, std::ios::beg);
        std::vector<uint8_t> file_data(static_cast<size_t>(fsize));
        file.read(reinterpret_cast<char*>(file_data.data()), fsize);
        file.close();

        uint32_t file_size = static_cast<uint32_t>(file_data.size());
        uint32_t net_size = htonl(file_size);

        client_socket.send_data(reinterpret_cast<const uint8_t*>(&net_size), 4);

        if (!file_data.empty()) {
            size_t offset = 0;
            while (offset < file_data.size()) {
                ssize_t sent = client_socket.send_data(
                    file_data.data() + offset, file_data.size() - offset);
                if (sent <= 0) break;
                offset += static_cast<size_t>(sent);
            }
        }

        std::cout << "[producer] Sent " << file_size << " bytes for " << filename << "\n";

    } catch (const std::exception& e) {
        std::cerr << "[producer] File transfer error: " << e.what() << "\n";
    }
    client_socket.close();
}

} // namespace pc
