#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif

#include "consumer/consumer.h"
#include "common/signal_handler.h"
#include "common/util.h"
#include "common/version.h"
#include "consumer/PWD_Handler.h"
#include "consumer/BENCH_Handler.h"
#include "consumer/ECHO_Handler.h"
#include "consumer/file_result_sink.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <chrono>
#include <iomanip>
#include <cstdlib>
#include <filesystem>
#include <thread>
#include <algorithm>
#include <cstring>

namespace pc {
namespace fs = std::filesystem;

Consumer::Consumer(const ConsumerConfig& config)
    : config_(config),
      work_queue_(4096),
      last_comm_time_(std::chrono::steady_clock::now()) {
    consumer_id_ = config.consumer_id.empty() ? generate_consumer_id() : config.consumer_id;

    if (config.handler_type == "PWD") {
        handler_ = std::make_shared<PWD_Handler>();
    } else if (config.handler_type == "BENCH") {
        handler_ = std::make_shared<BENCH_Handler>();
    } else if (config.handler_type == "ECHO") {
        handler_ = std::make_shared<ECHO_Handler>();
    }

    if (handler_ && !config.handler_config.empty()) {
        handler_->configure(config.handler_config);
    }

    if (!config.result_file.empty()) {
        sink_ = std::make_shared<FileResultSink>(config.result_file, config.max_failures, config.max_duration_sec);
        std::cout << "[consumer] Result sink: file=" << config.result_file << "\n";
    } else {
        std::string default_path = get_data_directory() + "results_" + consumer_id_ + ".jsonl";
        sink_ = std::make_shared<FileResultSink>(default_path, config.max_failures, config.max_duration_sec);
        std::cout << "[consumer] Result sink: file=" << default_path << " (default)\n";
    }
}

Consumer::~Consumer() {
    shutdown();
}

void Consumer::run() {
    running_ = true;

    std::cout << "[consumer] " << consumer_id_ << " connecting to "
              << config_.host << ":" << config_.port << "\n";

    connect_to_producer();

    // Determine pool size
    int pool_size = config_.threads > 0 ? config_.threads :
                    static_cast<int>(std::thread::hardware_concurrency());
    if (pool_size <= 0) pool_size = 4;

    // Start receiver thread
    receiver_thread_ = std::thread(&Consumer::receiver_loop, this);

    // Start heartbeat thread
    heartbeat_thread_ = std::thread(&Consumer::heartbeat_loop, this);

    // Wait for stop signal or max messages
    while (running_) {
        if (SignalHandler::is_stop_requested()) break;
        if (config_.max_messages > 0 && total_received_ >= config_.max_messages) break;
        if (sink_ && sink_->should_stop()) break;
        if (config_.idle_timeout_sec > 0) {
            std::lock_guard<std::mutex> lock(comm_time_mutex_);
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - last_comm_time_).count();
            if (elapsed >= config_.idle_timeout_sec) {
                std::cout << "[consumer] Idle timeout (" << config_.idle_timeout_sec
                          << "s without producer communication), shutting down\n";
                break;
            }
        }
        // Safety net: if the pool is fully idle with nothing queued, request
        // more work. The idle callback's request may have been dropped by the
        // 50ms throttle, which would otherwise stall the consumer.
        if (pool_ && pool_->active_count() == 0 && pool_->queue_empty()) {
            send_work_request(static_cast<int>(pool_->idle_count()));
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    shutdown();
}

void Consumer::shutdown() {
    running_ = false;

    if (receiver_thread_.joinable()) receiver_thread_.join();
    if (heartbeat_thread_.joinable()) heartbeat_thread_.join();

    // Drain pending work units and report as failures
    if (pool_) {
        std::vector<WorkUnitMessage> pending = pool_->drain_pending();
        if (!pending.empty()) {
            std::cout << "[consumer] Returning " << pending.size()
                      << " in-progress units as failure (shutdown)\n";
            for (auto& work : pending) {
                ResultMessage fail;
                fail.work_unit_id = work.work_unit_id;
                fail.seq = work.seq;
                fail.consumer_id = consumer_id_;
                fail.status = "failure";
                fail.result = nlohmann::json::object();
                fail.result["error"] = "consumer shutdown";
                fail.timestamp = now_iso();
                send_result(fail);
            }
        }

        pool_->shutdown();
        pool_.reset();
    }

    work_queue_.shutdown();
    client_socket_.close();
    print_statistics();
}

void Consumer::connect_to_producer() {
    int retries = 0;
    const int max_retries = 30;
    std::string host = config_.local ? "127.0.0.1" : config_.host;

    while (retries < max_retries && running_) {
        try {
            client_socket_ = Socket(config_.transport);

            if (config_.transport == Transport::TCP) {
                client_socket_.connect(host, config_.port);
                client_socket_.set_recv_timeout(10000);

                // Version handshake (TCP)
                VersionMessage ver_req;
                ver_req.version = PC_VERSION;
                ver_req.consumer_id = consumer_id_;
                send_frame(client_socket_, ver_req.to_string());

                std::string ver_frame = recv_frame(client_socket_);
                nlohmann::json ver_j = nlohmann::json::parse(ver_frame);
                std::string producer_version = ver_j.value("version", "");
                std::string ver_status = ver_j.value("status", "");

                if (ver_status != "ok") {
                    std::cerr << "[consumer] Version mismatch: consumer v" << PC_VERSION
                              << " vs producer v" << producer_version << "\n";
                    exit(4);
                }

                std::cout << "[consumer] Connected to " << host << ":" << config_.port
                          << " (v" << producer_version << ", TCP)\n";
            } else {
                // UDP mode: bind local socket, no connect needed
                client_socket_.bind("0.0.0.0", 0);
                client_socket_.set_recv_timeout(10000);

                // Version handshake (UDP)
                VersionMessage ver_req;
                ver_req.version = PC_VERSION;
                ver_req.consumer_id = consumer_id_;
                send_frame_udp(client_socket_, host, config_.port, ver_req.to_string());

                std::string ver_frame = recv_frame_udp(client_socket_, producer_address_, producer_port_);
                nlohmann::json ver_j = nlohmann::json::parse(ver_frame);
                std::string producer_version = ver_j.value("version", "");
                std::string ver_status = ver_j.value("status", "");

                if (ver_status != "ok") {
                    std::cerr << "[consumer] Version mismatch: consumer v" << PC_VERSION
                              << " vs producer v" << producer_version << "\n";
                    exit(4);
                }

                std::cout << "[consumer] Connected to " << host << ":" << config_.port
                          << " (v" << producer_version << ", UDP)\n";
            }

            // Send initial work request
            int hc = static_cast<int>(std::thread::hardware_concurrency());
            if (hc < 1) hc = 1;
            send_work_request(config_.threads > 0 ? config_.threads : hc);
            return;
        } catch (const std::exception& e) {
            retries++;
            std::cerr << "[consumer] Connect attempt " << retries << "/"
                      << max_retries << " failed: " << e.what() << "\n";
            std::this_thread::sleep_for(std::chrono::seconds(5));
        }
    }

    std::cerr << "[consumer] Cannot connect to Producer after " << max_retries << " attempts\n";
    exit(2);
}

void Consumer::receiver_loop() {
    while (running_ && client_socket_.is_open()) {
        try {
            std::string frame;
            if (config_.transport == Transport::TCP) {
                frame = recv_frame(client_socket_);
            } else {
                std::string from_addr;
                uint16_t from_port;
                frame = recv_frame_udp(client_socket_, from_addr, from_port);
                if (frame.empty()) continue;
            }

            {
                std::lock_guard<std::mutex> lock(comm_time_mutex_);
                last_comm_time_ = std::chrono::steady_clock::now();
            }

            nlohmann::json j = nlohmann::json::parse(frame);
            std::string msg_type = j.value("msg_type", "");

            if (msg_type == "work_unit") {
                WorkUnitMessage work = WorkUnitMessage::from_json(j);

                // Validate required fields
                if (work.work_unit_id.empty() || work.job.empty()) {
                    std::cerr << "[consumer] Invalid work unit, discarding\n";
                    total_discarded_++;
                    continue;
                }

                if (first_seq_ < 0) first_seq_ = work.seq;
                last_seq_ = work.seq;
                total_received_++;

                // Handle source file on first message
                if (!file_ready_) {
                    std::lock_guard<std::mutex> lock(source_file_mutex_);
                    if (!file_ready_) {
                        source_file_ = work.source_file;
                        std::string hash = work.source_hash.value_or("");
                        download_source_file(source_file_, hash);
                        download_additional_files();
                        file_ready_ = true;

                        // Start thread pool
                        int hc2 = static_cast<int>(std::thread::hardware_concurrency());
                        if (hc2 < 1) hc2 = 1;
                        int pool_size = config_.threads > 0 ? config_.threads : hc2;
                        pool_ = std::make_unique<ThreadPool>(static_cast<size_t>(pool_size));
                        if (handler_) pool_->set_handler(handler_);

                        pool_->set_result_callback(
                            [this](const ResultMessage& result) {
                                if (result.status == "success") {
                                    mark_completed(result.work_unit_id);
                                }
                                if (sink_) sink_->on_result(result);
                                send_result(result);
                            }
                        );

                        pool_->set_idle_callback(
                            [this](size_t idle) {
                                send_work_request(static_cast<int>(idle));
                            }
                        );

                        pool_->start();
                        std::cout << "[consumer] Thread pool started with "
                                  << pool_size << " threads, handler="
                                  << (handler_ ? handler_->type() : "none") << "\n";

                        // Put the first work unit into the pool
                        pool_->submit(std::move(work));
                        continue;
                    }
                }

                // Submit to pool
                if (pool_) {
                    if (is_completed(work.work_unit_id)) {
                        ResultMessage dup;
                        dup.work_unit_id = work.work_unit_id;
                        dup.seq = work.seq;
                        dup.consumer_id = consumer_id_;
                        dup.status = "success";
                        dup.result = nlohmann::json::object();
                        dup.result["note"] = "duplicate, already processed";
                        dup.timestamp = now_iso();
                        send_result(dup);
                        continue;
                    }
                    pool_->submit(std::move(work));
                }
            }
        } catch (const std::exception& e) {
            if (running_) {
                std::cerr << "[consumer] Receive error: " << e.what() << "\n";
            }
            break;
        }
    }
}

void Consumer::download_source_file(const std::string& source_file, const std::string& source_hash) {
    auto filename = fs::path(source_file).filename().string();
    local_file_path_ = config_.file_dir + "/" + filename;

    if (fs::exists(local_file_path_)) {
        // Verify hash if provided
        if (!source_hash.empty()) {
            std::string local_hash = sha256_file(local_file_path_);
            if (local_hash != source_hash) {
                std::cout << "[consumer] Local file hash mismatch, re-downloading...\n";
            } else {
                std::cout << "[consumer] Source file exists locally (hash verified): " << local_file_path_ << "\n";
                return;
            }
        } else {
            std::cout << "[consumer] Source file exists locally: " << local_file_path_ << "\n";
            return;
        }
    }

    std::cout << "[consumer] Downloading source file: " << source_file << "\n";

    uint16_t file_port = config_.port + 1;
    std::string host = config_.local ? "127.0.0.1" : config_.host;

    try {
        Socket file_socket(Transport::TCP);
        file_socket.connect(host, file_port);
        file_socket.set_recv_timeout(30000);

        // Send request: 0x01 + null-terminated filename
        std::vector<uint8_t> request;
        request.push_back(0x01);
        for (char c : filename) request.push_back(static_cast<uint8_t>(c));
        request.push_back(0x00);

        size_t sent = 0;
        while (sent < request.size()) {
            ssize_t n = file_socket.send_data(request.data() + sent, request.size() - sent);
            if (n <= 0) throw std::runtime_error("Failed to send file request");
            sent += static_cast<size_t>(n);
        }

        // Read 4-byte big-endian file size
        uint8_t header[4];
        size_t hdr_off = 0;
        while (hdr_off < 4) {
            ssize_t n = file_socket.recv_data(header + hdr_off, 4 - hdr_off);
            if (n <= 0) throw std::runtime_error("Connection closed while reading file header");
            hdr_off += static_cast<size_t>(n);
        }
        uint32_t net_len = *reinterpret_cast<uint32_t*>(header);
        uint32_t file_size = ntohl(net_len);

        if (file_size == 0) {
            std::cerr << "[consumer] Producer does not have file: " << filename << "\n";
            exit(3);
        }

        std::cout << "[consumer] Receiving " << file_size << " bytes...\n";

        // Read file contents
        std::vector<uint8_t> file_data(file_size);
        size_t off = 0;
        while (off < file_size) {
            ssize_t n = file_socket.recv_data(file_data.data() + off, file_size - off);
            if (n <= 0) throw std::runtime_error("Connection closed while receiving file");
            off += static_cast<size_t>(n);
        }
        file_socket.close();

        // Write to local file
        std::ofstream out(local_file_path_, std::ios::binary | std::ios::trunc);
        if (!out.is_open()) {
            std::cerr << "[consumer] Cannot write local file: " << local_file_path_ << "\n";
            exit(3);
        }
        out.write(reinterpret_cast<const char*>(file_data.data()), file_size);
        out.close();

        std::cout << "[consumer] File saved to " << local_file_path_ << "\n";

    } catch (const std::exception& e) {
        std::cerr << "[consumer] File transfer failed: " << e.what() << "\n";
        std::cerr << "[consumer] Place '" << filename << "' in '" << config_.file_dir << "' and retry.\n";
        exit(3);
    }
}

void Consumer::download_additional_files() {
    // Local consumers skip — they already have the files
    if (config_.local || config_.host == "127.0.0.1") {
        return;
    }

    uint16_t file_port = config_.port + 1;
    std::string host = config_.local ? "127.0.0.1" : config_.host;

    // Request manifest (0x02)
    Socket file_socket(Transport::TCP);
    for (int retry = 0; retry < 3; retry++) {
        try {
            file_socket.connect(host, file_port);
            file_socket.set_recv_timeout(30000);
            break;
        } catch (...) {
            if (retry < 2) {
                std::this_thread::sleep_for(std::chrono::seconds(2));
            } else {
                std::cout << "[consumer] Cannot reach file transfer server, skipping additional files\n";
                return;
            }
        }
    }

    try {
        uint8_t req = 0x02;
        file_socket.send_data(&req, 1);

        // Read 4-byte big-endian JSON length
        uint8_t header[4];
        size_t hdr_off = 0;
        while (hdr_off < 4) {
            ssize_t n = file_socket.recv_data(header + hdr_off, 4 - hdr_off);
            if (n <= 0) throw std::runtime_error("Connection closed reading manifest header");
            hdr_off += static_cast<size_t>(n);
        }
        uint32_t net_len = *reinterpret_cast<uint32_t*>(header);
        uint32_t json_len = ntohl(net_len);

        if (json_len == 0) {
            std::cout << "[consumer] No additional files to transfer\n";
            return;
        }

        // Read JSON manifest
        std::vector<uint8_t> json_buf(json_len);
        size_t off = 0;
        while (off < json_len) {
            ssize_t n = file_socket.recv_data(json_buf.data() + off, json_len - off);
            if (n <= 0) throw std::runtime_error("Connection closed reading manifest");
            off += static_cast<size_t>(n);
        }
        file_socket.close();

        std::string json_str(reinterpret_cast<char*>(json_buf.data()), json_len);
        nlohmann::json manifest = nlohmann::json::parse(json_str);

        if (manifest.empty()) {
            std::cout << "[consumer] No additional files to transfer\n";
            return;
        }

        std::cout << "[consumer] Manifest: " << manifest.size() << " additional file(s)\n";

        // Download each file
        for (const auto& entry : manifest) {
            std::string fname = entry.value("name", "");
            std::string expected_hash = entry.value("sha256", "");
            uint64_t expected_size = entry.value("size", 0);

            std::string local_path = config_.file_dir + "/" + fname;

            // Check if file already exists with correct hash
            if (fs::exists(local_path)) {
                std::string local_hash = sha256_file(local_path);
                if (local_hash == expected_hash) {
                    std::cout << "[consumer] Additional file exists (verified): " << fname << "\n";
                    continue;
                } else {
                    std::cout << "[consumer] Hash mismatch, re-downloading: " << fname << "\n";
                }
            }

            // Download file via 0x01 protocol
            Socket dl_socket(Transport::TCP);
            dl_socket.connect(host, file_port);
            dl_socket.set_recv_timeout(30000);

            std::vector<uint8_t> request;
            request.push_back(0x01);
            for (char c : fname) request.push_back(static_cast<uint8_t>(c));
            request.push_back(0x00);

            size_t sent = 0;
            while (sent < request.size()) {
                ssize_t n = dl_socket.send_data(request.data() + sent, request.size() - sent);
                if (n <= 0) throw std::runtime_error("Failed to send file request for " + fname);
                sent += static_cast<size_t>(n);
            }

            // Read 4-byte file size
            uint8_t fheader[4];
            hdr_off = 0;
            while (hdr_off < 4) {
                ssize_t n = dl_socket.recv_data(fheader + hdr_off, 4 - hdr_off);
                if (n <= 0) throw std::runtime_error("Connection closed reading file header for " + fname);
                hdr_off += static_cast<size_t>(n);
            }
            uint32_t fnet_len = *reinterpret_cast<uint32_t*>(fheader);
            uint32_t file_size = ntohl(fnet_len);

            if (file_size == 0) {
                std::cerr << "[consumer] Producer does not have file: " << fname << "\n";
                exit(3);
            }

            std::cout << "[consumer] Downloading: " << fname << " (" << file_size << " bytes)\n";

            std::vector<uint8_t> file_data(file_size);
            off = 0;
            while (off < file_size) {
                ssize_t n = dl_socket.recv_data(file_data.data() + off, file_size - off);
                if (n <= 0) throw std::runtime_error("Connection closed receiving " + fname);
                off += static_cast<size_t>(n);
            }
            dl_socket.close();

            // Write to local file
            std::ofstream out(local_path, std::ios::binary | std::ios::trunc);
            if (!out.is_open()) {
                std::cerr << "[consumer] Cannot write: " << local_path << "\n";
                exit(3);
            }
            out.write(reinterpret_cast<const char*>(file_data.data()), file_size);
            out.close();

            // Verify hash
            std::string actual_hash = sha256_file(local_path);
            if (actual_hash != expected_hash) {
                std::cerr << "[consumer] Hash verification failed for " << fname
                          << " (expected " << expected_hash << ", got " << actual_hash << ")\n";
                exit(3);
            }

            std::cout << "[consumer] Verified: " << fname << "\n";
        }

    } catch (const std::exception& e) {
        std::cerr << "[consumer] Additional file transfer failed: " << e.what() << "\n";
        exit(3);
    }
}

void Consumer::send_work_request(int threads_available) {
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - last_request_time_).count();

    if (elapsed < 50) {
        return;
    }

    last_request_time_ = now;

    WorkRequestMessage req;
    req.consumer_id = consumer_id_;
    req.threads_available = threads_available;
    req.timestamp = now_iso();

    try {
        if (config_.transport == Transport::TCP) {
            send_frame(client_socket_, req.to_string());
        } else {
            std::string host = config_.local ? "127.0.0.1" : config_.host;
            send_frame_udp(client_socket_, host, config_.port, req.to_string());
        }
    } catch (const std::exception& e) {
        std::cerr << "[consumer] Failed to send work request: " << e.what() << "\n";
    }
}

void Consumer::send_result(const ResultMessage& result) {
    try {
        if (config_.transport == Transport::TCP) {
            send_frame(client_socket_, result.to_string());
        } else {
            std::string host = config_.local ? "127.0.0.1" : config_.host;
            send_frame_udp(client_socket_, host, config_.port, result.to_string());
        }
    } catch (const std::exception& e) {
        std::cerr << "[consumer] Failed to send result: " << e.what() << "\n";
    }
}

void Consumer::print_statistics() {
    std::cout << "\n=== Consumer Statistics ===\n";
    std::cout << "Work units received:     " << total_received_ << "\n";
    if (pool_) {
        std::cout << "Work units completed:    " << pool_->total_completed() << "\n";
        std::cout << "Work units failed:       " << pool_->total_failed() << "\n";
    }
    std::cout << "Work units discarded:    " << total_discarded_ << "\n";
    std::cout << "Consumer ID:             " << consumer_id_ << "\n";
    if (first_seq_ >= 0)
        std::cout << "Sequence range:          " << first_seq_ << " - " << last_seq_ << "\n";
    if (sink_) {
        auto s = sink_->summary();
        std::cout << "Result sink:             " << s.value("type", "unknown")
                  << " file=" << s.value("file", "none")
                  << " total=" << s.value("total", 0)
                  << " ok=" << s.value("successes", 0)
                  << " fail=" << s.value("failures", 0) << "\n";
    }
    std::cout << "===========================\n";
}

std::string Consumer::generate_consumer_id() {
    std::string hostname = "unknown";
#ifdef _WIN32
    char name[MAX_COMPUTERNAME_LENGTH + 1];
    DWORD len = MAX_COMPUTERNAME_LENGTH + 1;
    if (GetComputerNameA(name, &len)) hostname = name;
#else
    char name[256];
    if (gethostname(name, sizeof(name)) == 0) hostname = name;
#endif
    return "cons-" + hostname + "-" + std::to_string(static_cast<unsigned>(
        std::hash<std::string>{}(std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count())) % 10000));
}

std::string Consumer::now_iso() {
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

std::string Consumer::compute_sha256(const std::string& path) {
    return sha256_file(path);
}

bool Consumer::is_completed(const std::string& work_unit_id) {
    std::lock_guard<std::mutex> lock(completed_ids_mutex_);
    return completed_ids_set_.count(work_unit_id) > 0;
}

void Consumer::mark_completed(const std::string& work_unit_id) {
    std::lock_guard<std::mutex> lock(completed_ids_mutex_);
    if (completed_ids_set_.count(work_unit_id)) {
        completed_ids_lru_.remove(work_unit_id);
    }
    completed_ids_lru_.push_front(work_unit_id);
    completed_ids_set_.insert(work_unit_id);
    while (completed_ids_lru_.size() > kMaxCompletedIds) {
        std::string oldest = completed_ids_lru_.back();
        completed_ids_lru_.pop_back();
        completed_ids_set_.erase(oldest);
    }
}

void Consumer::heartbeat_loop() {
    constexpr int kIntervalSec = 5;
    while (running_ && client_socket_.is_open()) {
        std::this_thread::sleep_for(std::chrono::seconds(kIntervalSec));
        if (!running_ || !client_socket_.is_open()) break;

        HeartbeatMessage hb;
        hb.consumer_id = consumer_id_;
        hb.timestamp = now_iso();
        try {
            if (config_.transport == Transport::TCP) {
                send_frame(client_socket_, hb.to_string());
            } else {
                std::string host = config_.local ? "127.0.0.1" : config_.host;
                send_frame_udp(client_socket_, host, config_.port, hb.to_string());
            }
        } catch (const std::exception& e) {
            std::cerr << "[consumer] Heartbeat send failed: " << e.what() << "\n";
            break;
        }
    }
}

} // namespace pc
