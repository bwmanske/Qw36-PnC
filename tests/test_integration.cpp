#include <gtest/gtest.h>
#include "common/message.h"
#include "common/queue.h"
#include "common/socket.h"
#include "producer/work_tracker.h"
#include "common/checkpoint.h"
#include <thread>
#include <chrono>
#include <filesystem>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;
using namespace pc;

TEST(Integration, MessageAndQueue) {
    BoundedQueue<WorkUnitMessage> q(100);

    std::thread producer([&]() {
        for (int i = 0; i < 10; i++) {
            WorkUnitMessage msg;
            msg.source_file = "/test/jobs.json";
            msg.permutation = "sequential";
            msg.work_unit_id = "prod-001-" + std::to_string(i);
            msg.seq = i;
            msg.timestamp = "2026-01-01T00:00:00.000Z";
            msg.producer_id = "prod-001";
            msg.job = nlohmann::json::parse(R"({"job_id":1,"task":"test"})");
            q.push(std::move(msg));
        }
    });

    std::thread consumer([&]() {
        for (int i = 0; i < 10; i++) {
            auto msg = q.pop();
            EXPECT_EQ(msg.seq, i);
            EXPECT_EQ(msg.work_unit_id, "prod-001-" + std::to_string(i));
        }
    });

    producer.join();
    consumer.join();
}

TEST(Integration, WorkTrackerFullCycle) {
    WorkTracker tracker;

    // Add work units
    for (int i = 0; i < 5; i++) {
        WorkUnitEntry entry;
        entry.work_unit_id = "wu-" + std::to_string(i);
        entry.seq = i;
        entry.job = nlohmann::json::parse(R"({"job_id":1})");
        entry.status = WorkUnitStatus::Pending;
        tracker.add_pending(entry);
    }

    // Dispatch to consumer
    auto pending = tracker.get_pending(3);
    for (auto& e : pending) {
        tracker.mark_sent(e.work_unit_id, "cons-001");
    }

    EXPECT_EQ(tracker.pending_count(), 5);

    // Complete 2
    tracker.mark_completed("wu-0");
    tracker.mark_completed("wu-1");

    EXPECT_EQ(tracker.completed_count(), 2);
    EXPECT_EQ(tracker.last_completed_seq(), 1);

    // Consumer disconnects — re-dispatch
    auto failed = tracker.get_failed_for_consumer("cons-001");
    EXPECT_EQ(failed.size(), 1);

    // Checkpoint
    auto state = tracker.to_checkpoint(5);
    EXPECT_EQ(state.completed_count, 2);
}

TEST(Integration, CheckpointResume) {
#ifdef _WIN32
    const char* t = std::getenv("TEMP");
    std::string dir = (t ? t : "C:\\Temp") + std::string("/pc_resume_test");
#else
    std::string dir = std::string("/tmp/pc_resume_test");
#endif
    fs::create_directories(dir);

    CheckpointManager mgr(dir);

    // Simulate first run
    CheckpointState state;
    state.producer_id = "prod-001";
    state.source_file = "/test/jobs.json";
    state.permutation = "random";
    state.permutation_seed = 42;
    state.total_jobs = 100;
    state.last_completed_seq = 49;
    state.last_completed_work_unit_id = "prod-001-49";
    state.completed_count = 50;
    state.pending_count = 50;
    state.failed_count = 0;
    state.checkpoint_timestamp = "2026-01-01T00:00:00.000Z";
    mgr.save(state);

    // Simulate resume
    auto loaded = mgr.load();
    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ(loaded->last_completed_seq, 49);
    EXPECT_EQ(loaded->permutation_seed, 42);

    int64_t resume_idx = loaded->last_completed_seq + 1;
    EXPECT_EQ(resume_idx, 50);

    fs::remove_all(dir);
}

TEST(Integration, MessageSerializationAllTypes) {
    WorkUnitMessage wu;
    wu.source_file = "/jobs.json";
    wu.permutation = "random";
    wu.permutation_seed = 123;
    wu.work_unit_id = "prod-001-0";
    wu.seq = 0;
    wu.timestamp = "2026-01-01T00:00:00.000Z";
    wu.producer_id = "prod-001";
    wu.job = nlohmann::json::parse(R"({"job_id":1,"task":"render","params":{"w":1920}})");

    ResultMessage result;
    result.work_unit_id = wu.work_unit_id;
    result.seq = wu.seq;
    result.consumer_id = "cons-001";
    result.status = "success";
    result.result = nlohmann::json::parse(R"({"output":"frame.png"})");
    result.timestamp = "2026-01-01T00:00:01.000Z";

    WorkRequestMessage req;
    req.consumer_id = "cons-001";
    req.threads_available = 4;
    req.timestamp = "2026-01-01T00:00:00.500Z";

    // Verify all round-trip
    auto wu2 = WorkUnitMessage::from_string(wu.to_string());
    EXPECT_EQ(wu2.work_unit_id, wu.work_unit_id);

    auto r2 = ResultMessage::from_string(result.to_string());
    EXPECT_EQ(r2.status, result.status);

    auto rq2 = WorkRequestMessage::from_string(req.to_string());
    EXPECT_EQ(rq2.threads_available, req.threads_available);
}

// ── End-to-end integration test: spawn producer + consumer processes ──

#include <sstream>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#endif

static std::string find_executable(const std::string& name) {
    auto check_dir = [&name](const fs::path& dir) -> std::string {
        fs::path candidate = dir / name;
#ifdef _WIN32
        if (candidate.extension().empty()) {
            candidate += ".exe";
        }
#endif
        if (fs::exists(candidate)) return candidate.string();
        return "";
    };

    // Search upward from CWD, checking both the directory itself and
    // its build/Release subdirectory (test binary at build/tests/Release/,
    // executables at build/Release/).
    fs::path dir = fs::current_path();
    while (true) {
        std::string found = check_dir(dir);
        if (!found.empty()) return found;
        found = check_dir(dir / "build" / "Release");
        if (!found.empty()) return found;
        if (!dir.has_parent_path()) break;
        dir = dir.parent_path();
    }
    return name;
}

static std::string read_file_contents(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) return "";
    return std::string((std::istreambuf_iterator<char>(f)),
                       std::istreambuf_iterator<char>());
}

TEST(Integration, EndToEnd_ECHO_FullCycle) {
    // ── 1. Create temp directory and config files ──
    std::string tmpdir;
#ifdef _WIN32
    const char* t = std::getenv("TEMP");
    tmpdir = (t ? t : "C:\\Temp") + std::string("\\pc_e2e_") + std::to_string(GetTickCount64());
#else
    tmpdir = "/tmp/pc_e2e_" + std::to_string(getpid());
#endif
    fs::create_directories(tmpdir);

    // ECHO plugin config: 5 work units, small payload
    std::string echo_config_path = tmpdir + "/echo_config.json";
    {
        std::ofstream cf(echo_config_path);
        cf << R"({"payload_size":16,"total_units":5})";
    }

    // Main producer config (job file) — config_file must be absolute
    std::string job_config_path = tmpdir + "/job_config.json";
    {
        nlohmann::json cfg;
        cfg["test_type"] = "ECHO";
        cfg["config_file"] = echo_config_path;
        cfg["max_units"] = 5;
        cfg["max_idle_seconds"] = 30;
        std::ofstream jf(job_config_path);
        jf << cfg.dump();
    }

    // Directories
    std::string ckpt_dir = tmpdir + "/checkpoints";
    std::string file_dir = tmpdir + "/files";
    fs::create_directories(ckpt_dir);
    fs::create_directories(file_dir);

    std::string result_file = tmpdir + "/results.jsonl";

    // ── 2. Find executables ──
    std::string producer_exe = find_executable("producer");
    std::string consumer_exe = find_executable("consumer");

    ASSERT_TRUE(fs::exists(producer_exe)) << "Producer not found at: " << producer_exe;
    ASSERT_TRUE(fs::exists(consumer_exe)) << "Consumer not found at: " << consumer_exe;

    uint16_t port = 19876;

#ifdef _WIN32
    // ── Windows: CreateProcess ──

    // Launch producer
    std::string prod_cmd = "\"" + producer_exe + "\" --file \"" + job_config_path +
        "\" --port " + std::to_string(port) +
        " --checkpoint-dir \"" + ckpt_dir + "\"" +
        " --max-time 30s";

    STARTUPINFOA si = {};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi_prod = {};
    EXPECT_TRUE(CreateProcessA(nullptr, const_cast<char*>(prod_cmd.c_str()),
                               nullptr, nullptr, FALSE,
                               CREATE_NO_WINDOW,
                               nullptr, nullptr, &si, &pi_prod))
        << "Failed to launch producer";
    CloseHandle(pi_prod.hThread);

    // Give producer time to bind and listen
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    // Launch consumer
    std::string cons_cmd = "\"" + consumer_exe + "\" --host 127.0.0.1" +
        " --port " + std::to_string(port) +
        " --threads 2" +
        " --handler ECHO" +
        " --file-dir \"" + file_dir + "\"" +
        " --max-messages 5" +
        " --result-file \"" + result_file + "\"" +
        " --local" +
        " --timeout 30";

    STARTUPINFOA si2 = {};
    si2.cb = sizeof(si2);
    PROCESS_INFORMATION pi_cons = {};
    EXPECT_TRUE(CreateProcessA(nullptr, const_cast<char*>(cons_cmd.c_str()),
                               nullptr, nullptr, FALSE,
                               CREATE_NO_WINDOW,
                               nullptr, nullptr, &si2, &pi_cons))
        << "Failed to launch consumer";
    CloseHandle(pi_cons.hThread);

    // Wait for both (max 60s each), killing on timeout to avoid orphans
    DWORD prod_wait = WaitForSingleObject(pi_prod.hProcess, 60000);
    if (prod_wait != WAIT_OBJECT_0) TerminateProcess(pi_prod.hProcess, 1);
    DWORD cons_wait = WaitForSingleObject(pi_cons.hProcess, 60000);
    if (cons_wait != WAIT_OBJECT_0) TerminateProcess(pi_cons.hProcess, 1);
    EXPECT_EQ(prod_wait, WAIT_OBJECT_0) << "Producer did not exit within 60s";
    EXPECT_EQ(cons_wait, WAIT_OBJECT_0) << "Consumer did not exit within 60s";

    DWORD exit_code;
    GetExitCodeProcess(pi_prod.hProcess, &exit_code);
    EXPECT_EQ(exit_code, 0u) << "Producer exit code: " << exit_code;
    GetExitCodeProcess(pi_cons.hProcess, &exit_code);
    EXPECT_EQ(exit_code, 0u) << "Consumer exit code: " << exit_code;

    CloseHandle(pi_prod.hProcess);
    CloseHandle(pi_cons.hProcess);

#else
    // ── POSIX: fork + exec ──

    pid_t prod_pid = fork();
    if (prod_pid == 0) {
        execl(producer_exe.c_str(), "producer",
              "--file", job_config_path.c_str(),
              "--port", std::to_string(port).c_str(),
              "--checkpoint-dir", ckpt_dir.c_str(),
              "--max-time", "30s",
              nullptr);
        _exit(127);
    }
    ASSERT_GT(prod_pid, 0);

    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    pid_t cons_pid = fork();
    if (cons_pid == 0) {
        execl(consumer_exe.c_str(), "consumer",
              "--host", "127.0.0.1",
              "--port", std::to_string(port).c_str(),
              "--threads", "2",
              "--handler", "ECHO",
              "--file-dir", file_dir.c_str(),
              "--max-messages", "5",
              "--result-file", result_file.c_str(),
              "--local",
              "--timeout", "30",
              nullptr);
        _exit(127);
    }
    ASSERT_GT(cons_pid, 0);

    int status_prod, status_cons;
    waitpid(prod_pid, &status_prod, 0);
    waitpid(cons_pid, &status_cons, 0);
    EXPECT_TRUE(WIFEXITED(status_prod) && WEXITSTATUS(status_prod) == 0)
        << "Producer exited abnormally";
    EXPECT_TRUE(WIFEXITED(status_cons) && WEXITSTATUS(status_cons) == 0)
        << "Consumer exited abnormally";
#endif

    // ── 3. Verify results ──
    ASSERT_TRUE(fs::exists(result_file)) << "Result file not created at: " << result_file;

    std::string result_content = read_file_contents(result_file);
    ASSERT_FALSE(result_content.empty()) << "Result file is empty";

    int line_count = 0;
    int success_count = 0;
    bool found_match = false;
    std::istringstream ss(result_content);
    std::string line;
    while (std::getline(ss, line)) {
        // Trim whitespace
        size_t end = line.find_last_not_of(" \t\r\n");
        if (end == std::string::npos) continue;
        line = line.substr(0, end + 1);
        if (line.empty()) continue;

        line_count++;
        try {
            nlohmann::json j = nlohmann::json::parse(line);
            if (j.value("status", "") == "success") {
                success_count++;
            }
            if (j.contains("result") && j["result"].contains("match")) {
                if (j["result"]["match"].get<bool>()) {
                    found_match = true;
                }
            }
        } catch (...) {}
    }

    EXPECT_GE(line_count, 5) << "Expected >= 5 result lines, got " << line_count;
    EXPECT_GE(success_count, 5) << "Expected >= 5 successes, got " << success_count;
    EXPECT_TRUE(found_match) << "No hash-matched result found";

    // ── 4. Verify checkpoint ──
    bool checkpoint_exists = false;
    if (fs::exists(ckpt_dir)) {
        for (const auto& entry : fs::directory_iterator(ckpt_dir)) {
            if (entry.path().extension() == ".json") {
                checkpoint_exists = true;
                break;
            }
        }
    }
    EXPECT_TRUE(checkpoint_exists) << "No checkpoint file in " << ckpt_dir;

    // ── 5. Cleanup ──
    fs::remove_all(tmpdir);
}

// ── End-to-end: producer --max-time + consumer --timeout shutdown ──

#ifndef _WIN32
static bool wait_bounded(pid_t pid, int& status, int timeout_ms) {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
        int r = waitpid(pid, &status, WNOHANG);
        if (r == pid) return true;
        if (r < 0) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    kill(pid, SIGKILL);
    waitpid(pid, &status, 0);
    return false;
}
#endif

TEST(Integration, EndToEnd_TimeoutShutdown) {
    std::string tmpdir;
#ifdef _WIN32
    const char* t = std::getenv("TEMP");
    tmpdir = (t ? t : "C:\\Temp") + std::string("\\pc_e2e_to_") + std::to_string(GetTickCount64());
#else
    tmpdir = "/tmp/pc_e2e_to_" + std::to_string(getpid());
#endif
    fs::create_directories(tmpdir);

    // ECHO plugin config: unlimited units — only --max-time stops the producer
    std::string echo_config_path = tmpdir + "/echo_config.json";
    {
        std::ofstream cf(echo_config_path);
        cf << R"({"payload_size":16,"total_units":0})";
    }

    std::string job_config_path = tmpdir + "/job_config.json";
    {
        nlohmann::json cfg;
        cfg["test_type"] = "ECHO";
        cfg["config_file"] = echo_config_path;
        cfg["max_units"] = 0;
        std::ofstream jf(job_config_path);
        jf << cfg.dump();
    }

    std::string ckpt_dir = tmpdir + "/checkpoints";
    std::string file_dir = tmpdir + "/files";
    fs::create_directories(ckpt_dir);
    fs::create_directories(file_dir);

    std::string result_file = tmpdir + "/results.jsonl";
    std::string prod_log = tmpdir + "/producer.log";
    std::string cons_log = tmpdir + "/consumer.log";

    std::string producer_exe = find_executable("producer");
    std::string consumer_exe = find_executable("consumer");
    ASSERT_TRUE(fs::exists(producer_exe)) << "Producer not found at: " << producer_exe;
    ASSERT_TRUE(fs::exists(consumer_exe)) << "Consumer not found at: " << consumer_exe;

    uint16_t port = 19877;

#ifdef _WIN32
    // Launch producer with output redirected to producer.log
    HANDLE prod_log_handle = CreateFileA(prod_log.c_str(), GENERIC_WRITE,
        FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    ASSERT_NE(prod_log_handle, INVALID_HANDLE_VALUE);
    SetHandleInformation(prod_log_handle, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);

    std::string prod_cmd = "\"" + producer_exe + "\" --file \"" + job_config_path +
        "\" --port " + std::to_string(port) +
        " --checkpoint-dir \"" + ckpt_dir + "\"" +
        " --max-time 2s";

    STARTUPINFOA si = {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = prod_log_handle;
    si.hStdError = prod_log_handle;
    PROCESS_INFORMATION pi_prod = {};
    EXPECT_TRUE(CreateProcessA(nullptr, const_cast<char*>(prod_cmd.c_str()),
                               nullptr, nullptr, TRUE,
                               CREATE_NO_WINDOW,
                               nullptr, nullptr, &si, &pi_prod))
        << "Failed to launch producer";
    CloseHandle(prod_log_handle);

    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    HANDLE cons_log_handle = CreateFileA(cons_log.c_str(), GENERIC_WRITE,
        FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    ASSERT_NE(cons_log_handle, INVALID_HANDLE_VALUE);
    SetHandleInformation(cons_log_handle, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);

    std::string cons_cmd = "\"" + consumer_exe + "\" --host 127.0.0.1" +
        " --port " + std::to_string(port) +
        " --threads 2" +
        " --handler ECHO" +
        " --file-dir \"" + file_dir + "\"" +
        " --result-file \"" + result_file + "\"" +
        " --local" +
        " --timeout 5";

    STARTUPINFOA si2 = {};
    si2.cb = sizeof(si2);
    si2.dwFlags = STARTF_USESTDHANDLES;
    si2.hStdOutput = cons_log_handle;
    si2.hStdError = cons_log_handle;
    PROCESS_INFORMATION pi_cons = {};
    EXPECT_TRUE(CreateProcessA(nullptr, const_cast<char*>(cons_cmd.c_str()),
                               nullptr, nullptr, TRUE,
                               CREATE_NO_WINDOW,
                               nullptr, nullptr, &si2, &pi_cons))
        << "Failed to launch consumer";
    CloseHandle(cons_log_handle);

    DWORD prod_wait = WaitForSingleObject(pi_prod.hProcess, 30000);
    if (prod_wait != WAIT_OBJECT_0) TerminateProcess(pi_prod.hProcess, 1);
    DWORD cons_wait = WaitForSingleObject(pi_cons.hProcess, 30000);
    if (cons_wait != WAIT_OBJECT_0) TerminateProcess(pi_cons.hProcess, 1);
    EXPECT_EQ(prod_wait, WAIT_OBJECT_0) << "Producer did not exit within 30s";
    EXPECT_EQ(cons_wait, WAIT_OBJECT_0) << "Consumer did not exit within 30s";

    DWORD exit_code;
    GetExitCodeProcess(pi_prod.hProcess, &exit_code);
    EXPECT_EQ(exit_code, 0u) << "Producer exit code: " << exit_code;
    GetExitCodeProcess(pi_cons.hProcess, &exit_code);
    EXPECT_EQ(exit_code, 0u) << "Consumer exit code: " << exit_code;

    CloseHandle(pi_prod.hProcess);
    CloseHandle(pi_cons.hProcess);

#else
    pid_t prod_pid = fork();
    if (prod_pid == 0) {
        int fd = open(prod_log.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd >= 0) { dup2(fd, 1); dup2(fd, 2); close(fd); }
        execl(producer_exe.c_str(), "producer",
              "--file", job_config_path.c_str(),
              "--port", std::to_string(port).c_str(),
              "--checkpoint-dir", ckpt_dir.c_str(),
              "--max-time", "2s",
              nullptr);
        _exit(127);
    }
    ASSERT_GT(prod_pid, 0);

    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    pid_t cons_pid = fork();
    if (cons_pid == 0) {
        int fd = open(cons_log.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd >= 0) { dup2(fd, 1); dup2(fd, 2); close(fd); }
        execl(consumer_exe.c_str(), "consumer",
              "--host", "127.0.0.1",
              "--port", std::to_string(port).c_str(),
              "--threads", "2",
              "--handler", "ECHO",
              "--file-dir", file_dir.c_str(),
              "--result-file", result_file.c_str(),
              "--local",
              "--timeout", "5",
              nullptr);
        _exit(127);
    }
    ASSERT_GT(cons_pid, 0);

    int status_prod, status_cons;
    bool prod_exited = wait_bounded(prod_pid, status_prod, 30000);
    bool cons_exited = wait_bounded(cons_pid, status_cons, 30000);
    EXPECT_TRUE(prod_exited) << "Producer did not exit within 30s";
    EXPECT_TRUE(cons_exited) << "Consumer did not exit within 30s";
    if (prod_exited) {
        EXPECT_TRUE(WIFEXITED(status_prod) && WEXITSTATUS(status_prod) == 0)
            << "Producer exited abnormally";
    }
    if (cons_exited) {
        EXPECT_TRUE(WIFEXITED(status_cons) && WEXITSTATUS(status_cons) == 0)
            << "Consumer exited abnormally";
    }
#endif

    // Verify the producer stopped due to --max-time
    std::string prod_log_content = read_file_contents(prod_log);
    EXPECT_NE(prod_log_content.find("Max time reached"), std::string::npos)
        << "Producer log does not mention max-time shutdown:\n" << prod_log_content;

    // Verify the consumer stopped due to --timeout (idle)
    std::string cons_log_content = read_file_contents(cons_log);
    EXPECT_NE(cons_log_content.find("Idle timeout"), std::string::npos)
        << "Consumer log does not mention idle timeout shutdown:\n" << cons_log_content;

    fs::remove_all(tmpdir);
}
