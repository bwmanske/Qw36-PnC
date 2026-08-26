#include "producer/producer.h"
#include "common/signal_handler.h"
#include "common/util.h"
#include "common/version.h"
#include "producer/PWD_plugin.h"
#include "producer/BENCH_plugin.h"
#include "producer/ECHO_plugin.h"
#include <fstream>
#include <iostream>
#include <algorithm>
#include <random>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <filesystem>
#include <cstring>
#ifndef _WIN32
#include <arpa/inet.h>
#endif

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

    // Create and bind the listening sockets BEFORE starting the worker
    // threads that call accept() on them, to avoid a race where a thread
    // accepts on an uninitialized socket and busy-loops on the error.
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
        server_socket_.set_recv_timeout(5000);
        std::cout << "[producer] Listening on 0.0.0.0:" << config_.port << " (UDP)\n";
    }

    running_ = true;
    checkpoint_running_ = true;
    file_transfer_running_ = true;
    monitor_running_ = true;
    udp_running_ = (config_.transport == Transport::UDP);
    start_time_ = std::chrono::steady_clock::now();

    checkpoint_thread_ = std::thread(&Producer::checkpoint_loop, this);
    file_transfer_thread_ = std::thread(&Producer::file_transfer_loop, this);
    dispatcher_thread_ = std::thread(&Producer::dispatcher_loop, this);
    monitor_thread_ = std::thread(&Producer::monitor_connections, this);
    if (udp_running_) {
        udp_thread_ = std::thread(&Producer::udp_loop, this);
    }

    if (config_.transport == Transport::TCP) {
        while (running_ && !SignalHandler::is_stop_requested()) {
            try {
                Socket client = server_socket_.accept();
                client.set_recv_timeout(10000);
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
    } else {
        while (running_ && !SignalHandler::is_stop_requested()) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }

    shutdown();
}

void Producer::shutdown() {
    running_ = false;
    checkpoint_running_ = false;
    file_transfer_running_ = false;
    monitor_running_ = false;
    udp_running_ = false;

    dispatch_queue_.shutdown();
    result_queue_.shutdown();

    // Close listening sockets to unblock accept() in worker threads
    server_socket_.close();
    file_transfer_socket_.close();

    if (dispatcher_thread_.joinable()) dispatcher_thread_.join();
    if (checkpoint_thread_.joinable()) checkpoint_thread_.join();
    if (file_transfer_thread_.joinable()) file_transfer_thread_.join();
    if (monitor_thread_.joinable()) monitor_thread_.join();
    if (udp_thread_.joinable()) udp_thread_.join();

    write_final_checkpoint();
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

    if (config_.transfer_siblings) {
        scan_additional_files();
    }
}

void Producer::scan_additional_files() {
    fs::path config_dir = fs::path(config_.file_path).parent_path();
    std::string config_filename = fs::path(config_.file_path).filename().string();

    if (!fs::is_directory(config_dir)) {
        return;
    }

    for (const auto& entry : fs::directory_iterator(config_dir)) {
        if (!entry.is_regular_file()) continue;
        std::string fname = entry.path().filename().string();
        if (fname == config_filename) continue;

        AdditionalFile af;
        af.name = fname;
        af.size = entry.file_size();
        af.sha256 = sha256_file(entry.path().string());
        additional_files_.push_back(af);
    }

    std::cout << "[producer] Transfer siblings: " << additional_files_.size() << " file(s) available\n";
    for (const auto& f : additional_files_) {
        std::cout << "  " << f.name << " (" << f.size << " bytes, sha256=" << f.sha256 << ")\n";
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
    } else if (test_type_ == "ECHO") {
        plugin_ = create_echo_plugin();
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

        if (config_.max_time_sec > 0) {
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - start_time_).count();
            if (elapsed >= config_.max_time_sec) {
                std::cout << "[producer] Max time reached (" << config_.max_time_sec << "s), shutting down\n";
                running_ = false;
                break;
            }
        }

        if (max_units_ > 0 && tracker_.completed_count() >= static_cast<int64_t>(max_units_)) {
            std::cout << "[producer] Max units reached (" << max_units_ << "), shutting down\n";
            running_ = false;
            break;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    // Close the listening socket so a blocked accept() in the main thread returns,
    // regardless of which stop condition triggered the exit.
    server_socket_.close();
}

void Producer::handle_client(Socket client_socket) {
    std::string consumer_id;
    try {
        // Version handshake — first message must be version check
        std::string frame = recv_frame(client_socket);
        nlohmann::json j = nlohmann::json::parse(frame);
        std::string msg_type = j.value("msg_type", "");

        if (msg_type == "version") {
            VersionMessage ver = VersionMessage::from_json(j);
            nlohmann::json ver_resp;
            ver_resp["msg_type"] = "version";
            ver_resp["version"] = PC_VERSION;

            if (ver.version != PC_VERSION) {
                ver_resp["status"] = "mismatch";
                std::cerr << "[producer] Version mismatch: consumer v" << ver.version
                          << " vs producer v" << PC_VERSION << " (id: " << ver.consumer_id << ")\n";
                send_frame(client_socket, ver_resp.dump());
                client_socket.close();
                return;
            }

            ver_resp["status"] = "ok";
            send_frame(client_socket, ver_resp.dump());
            consumer_id = ver.consumer_id;
            register_consumer(consumer_id, client_socket);
            std::cout << "[producer] Consumer connected: " << consumer_id
                      << " (v" << PC_VERSION << ")\n";
        }

        while (running_ && client_socket.is_open()) {
            frame = recv_frame(client_socket);
            j = nlohmann::json::parse(frame);
            msg_type = j.value("msg_type", "");

            if (msg_type == "work_request") {
                WorkRequestMessage req = WorkRequestMessage::from_json(j);
                if (consumer_id.empty()) {
                    register_consumer(req.consumer_id, client_socket);
                    consumer_id = req.consumer_id;
                } else if (req.consumer_id != consumer_id) {
                    consumer_id = req.consumer_id;
                }
                update_consumer_activity(consumer_id);
                handle_work_request(req, client_socket);
            } else if (msg_type == "result") {
                ResultMessage result = ResultMessage::from_json(j);
                if (consumer_id.empty() && !result.consumer_id.empty()) {
                    register_consumer(result.consumer_id, client_socket);
                    consumer_id = result.consumer_id;
                }
                update_consumer_activity(consumer_id);
                handle_result(result);
            } else if (msg_type == "heartbeat") {
                HeartbeatMessage hb = HeartbeatMessage::from_json(j);
                if (consumer_id.empty() && !hb.consumer_id.empty()) {
                    register_consumer(hb.consumer_id, client_socket);
                    consumer_id = hb.consumer_id;
                }
                update_consumer_activity(consumer_id);
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "[producer] Client error: " << e.what() << "\n";
    }

    if (!consumer_id.empty()) {
        auto reclaimed = tracker_.get_failed_for_consumer(consumer_id);
        std::cout << "[producer] Consumer disconnected: " << consumer_id
                  << " (reclaimed " << reclaimed.size() << " work units)\n";
        unregister_consumer(consumer_id);
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
        if (result.found_password.has_value()) {
            pwd_set_found(result.found_password.value());
            std::cout << "[producer] PASSWORD FOUND: " << result.found_password.value() << "\n";
            running_ = false;
        }
    } else {
        tracker_.mark_failed(result.work_unit_id);
        if (result.file_error.has_value()) {
            pwd_set_file_error(result.file_error.value());
            std::cerr << "[producer] FILE ERROR: " << result.file_error.value() << "\n";
            running_ = false;
        }
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
    if (test_type_ == "PWD") {
        if (!pwd_get_found_password().empty()) {
            std::cout << "Password found:        " << pwd_get_found_password() << "\n";
        }
        if (!pwd_get_file_error().empty()) {
            std::cout << "File error:            " << pwd_get_file_error() << "\n";
        }
    }
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
            client.set_recv_timeout(10000);
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

        uint8_t request_code = request_buf[0];

        // ── 0x02: manifest request ─────────────────────────────
        if (request_code == 0x02) {
            nlohmann::json manifest = nlohmann::json::array();
            for (const auto& af : additional_files_) {
                manifest.push_back({
                    {"name", af.name},
                    {"size", af.size},
                    {"sha256", af.sha256}
                });
            }
            std::string manifest_str = manifest.dump();
            std::vector<uint8_t> payload(manifest_str.begin(), manifest_str.end());
            uint32_t mlen = static_cast<uint32_t>(payload.size());
            uint32_t net_mlen = htonl(mlen);
            client_socket.send_data(reinterpret_cast<const uint8_t*>(&net_mlen), 4);
            if (!payload.empty()) {
                size_t offset = 0;
                while (offset < payload.size()) {
                    ssize_t sent = client_socket.send_data(payload.data() + offset, payload.size() - offset);
                    if (sent <= 0) break;
                    offset += static_cast<size_t>(sent);
                }
            }
            std::cout << "[producer] Sent manifest with " << additional_files_.size() << " file(s)\n";
            return;
        }

        if (request_code != 0x01) {
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

void Producer::register_consumer(const std::string& consumer_id, Socket& socket) {
    std::lock_guard<std::mutex> lock(consumers_mutex_);
    auto it = connected_consumers_.find(consumer_id);
    if (it == connected_consumers_.end()) {
        ConsumerInfo info;
        info.socket = &socket;
        info.last_activity = std::chrono::steady_clock::now();
        info.registered_at = info.last_activity;
        connected_consumers_[consumer_id] = std::move(info);
        std::cout << "[producer] Consumer registered: " << consumer_id
                  << " (total: " << connected_consumers_.size() << ")\n";
    } else {
        it->second.socket = &socket;
        it->second.last_activity = std::chrono::steady_clock::now();
    }
}

void Producer::unregister_consumer(const std::string& consumer_id) {
    std::lock_guard<std::mutex> lock(consumers_mutex_);
    connected_consumers_.erase(consumer_id);
}

void Producer::update_consumer_activity(const std::string& consumer_id) {
    std::lock_guard<std::mutex> lock(consumers_mutex_);
    auto it = connected_consumers_.find(consumer_id);
    if (it != connected_consumers_.end()) {
        it->second.last_activity = std::chrono::steady_clock::now();
    }
}

void Producer::monitor_connections() {
    constexpr int kHeartbeatTimeoutSec = 30;
    constexpr int kCheckIntervalSec = 5;

    while (monitor_running_) {
        std::this_thread::sleep_for(std::chrono::seconds(kCheckIntervalSec));

        if (!monitor_running_) break;

        auto now = std::chrono::steady_clock::now();
        std::vector<std::string> stale;

        {
            std::lock_guard<std::mutex> lock(consumers_mutex_);
            for (auto& [id, info] : connected_consumers_) {
                auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                    now - info.last_activity).count();
                if (elapsed >= kHeartbeatTimeoutSec) {
                    stale.push_back(id);
                }
            }
        }

        for (const auto& id : stale) {
            std::cout << "[producer] Consumer stale (no activity for "
                      << kHeartbeatTimeoutSec << "s): " << id << "\n";

            std::lock_guard<std::mutex> lock(consumers_mutex_);
            auto it = connected_consumers_.find(id);
            if (it != connected_consumers_.end() && it->second.socket) {
                it->second.socket->close();
            }

            auto reclaimed = tracker_.get_failed_for_consumer(id);
            std::cout << "[producer] Reclaimed " << reclaimed.size()
                      << " work units from stale consumer " << id << "\n";
            connected_consumers_.erase(it);
        }
    }
}

void Producer::udp_loop() {
    while (udp_running_ && !SignalHandler::is_stop_requested()) {
        try {
            std::string from_addr;
            uint16_t from_port;
            std::string frame = recv_frame_udp(server_socket_, from_addr, from_port);
            if (frame.empty()) continue;

            std::string key = from_addr + ":" + std::to_string(from_port);
            handle_udp_message(key, from_addr, from_port, frame);
        } catch (const std::exception& e) {
            if (udp_running_) {
                std::cerr << "[producer] UDP recv error: " << e.what() << "\n";
            }
        }
    }
}

void Producer::handle_udp_message(const std::string& consumer_id, const std::string& address, uint16_t port, const std::string& frame) {
    nlohmann::json j = nlohmann::json::parse(frame);
    std::string msg_type = j.value("msg_type", "");

    if (msg_type == "version") {
        VersionMessage ver = VersionMessage::from_json(j);
        nlohmann::json ver_resp;
        ver_resp["msg_type"] = "version";
        ver_resp["version"] = PC_VERSION;

        if (ver.version != PC_VERSION) {
            ver_resp["status"] = "mismatch";
            std::cerr << "[producer] Version mismatch: consumer v" << ver.version
                      << " vs producer v" << PC_VERSION << " (id: " << ver.consumer_id << ")\n";
            send_frame_udp(server_socket_, address, port, ver_resp.dump());
            return;
        }

        ver_resp["status"] = "ok";
        send_frame_udp(server_socket_, address, port, ver_resp.dump());
        register_consumer_udp(ver.consumer_id, address, port);
        std::cout << "[producer] Consumer connected (UDP): " << ver.consumer_id
                  << " @" << address << ":" << port << " (v" << PC_VERSION << ")\n";
    } else if (msg_type == "work_request") {
        WorkRequestMessage req = WorkRequestMessage::from_json(j);
        update_consumer_activity(req.consumer_id);
        handle_udp_work_request(req, address, port);
    } else if (msg_type == "result") {
        ResultMessage result = ResultMessage::from_json(j);
        update_consumer_activity(result.consumer_id);
        handle_result(result);
    } else if (msg_type == "heartbeat") {
        HeartbeatMessage hb = HeartbeatMessage::from_json(j);
        update_consumer_activity(hb.consumer_id);
    }
}

void Producer::handle_udp_work_request(const WorkRequestMessage& req, const std::string& address, uint16_t port) {
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
            send_frame_udp(server_socket_, address, port, msg.to_string());
        } catch (const std::exception& e) {
            std::cerr << "[producer] UDP send error: " << e.what() << "\n";
            break;
        }
    }
}

void Producer::register_consumer_udp(const std::string& consumer_id, const std::string& address, uint16_t port) {
    std::lock_guard<std::mutex> lock(consumers_mutex_);
    auto it = connected_consumers_.find(consumer_id);
    if (it == connected_consumers_.end()) {
        ConsumerInfo info;
        info.address = address;
        info.port = port;
        info.last_activity = std::chrono::steady_clock::now();
        info.registered_at = info.last_activity;
        connected_consumers_[consumer_id] = std::move(info);
        std::cout << "[producer] Consumer registered (UDP): " << consumer_id
                  << " (total: " << connected_consumers_.size() << ")\n";
    } else {
        it->second.address = address;
        it->second.port = port;
        it->second.last_activity = std::chrono::steady_clock::now();
    }
}

} // namespace pc
