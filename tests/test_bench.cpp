#include <gtest/gtest.h>
#include "producer/BENCH_plugin.h"
#include "consumer/BENCH_Handler.h"
#include "common/util.h"
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <chrono>

namespace fs = std::filesystem;
using namespace pc;

namespace {

// BENCH_Handler resolves the source file by *filename in the CWD*, so the
// handler tests run inside a temp directory and restore the CWD on exit.
struct ChdirGuard {
    std::string old_cwd;
    explicit ChdirGuard(const std::string& dir) {
        old_cwd = fs::current_path().string();
        fs::create_directories(dir);
        fs::current_path(dir);
    }
    ~ChdirGuard() { fs::current_path(old_cwd); }
};

std::string temp_path(const std::string& prefix) {
#ifdef _WIN32
    const char* t = std::getenv("TEMP");
    return std::string(t ? t : "C:\\Temp") + "\\" + prefix + "_" +
           std::to_string(static_cast<unsigned>(
               std::hash<std::string>{}(std::to_string(
                   std::chrono::steady_clock::now().time_since_epoch().count())) % 1000000));
#else
    return "/tmp/" + prefix + "_" + std::to_string(getpid());
#endif
}

std::string write_bench_source(size_t bytes) {
    std::string path = temp_path("pc_bench_src");
    std::ofstream f(path, std::ios::binary);
    std::vector<uint8_t> data(bytes);
    for (size_t i = 0; i < bytes; i++) data[i] = static_cast<uint8_t>(i % 251);
    f.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(bytes));
    return path;
}

std::string write_bench_config(int chunk_size) {
    std::string path = temp_path("pc_bench_cfg");
    std::ofstream f(path);
    f << R"({"chunk_size":)" << chunk_size << "}";
    return path;
}

} // namespace

TEST(BENCHPlugin, IsValid) {
    TestPlugin plugin = create_bench_plugin();
    EXPECT_TRUE(plugin.is_valid());
}

TEST(BENCHPlugin, CheckpointState) {
    std::string src = write_bench_source(1000);
    std::string cfg = write_bench_config(128);

    TestPlugin plugin = create_bench_plugin();
    plugin.startup(cfg, nlohmann::json::object());
    set_bench_source_file(src);

    WorkUnitMessage msg;
    for (int i = 0; i < 3; i++) {
        ASSERT_TRUE(plugin.next_unit(msg));
    }

    // 3 chunks of 128 bytes = 384 bytes consumed
    nlohmann::json cp = plugin.checkpoint();
    EXPECT_EQ(cp.value("offset", -1), 384);
    EXPECT_EQ(cp.value("seq", -1), 3);

    fs::remove(src);
    fs::remove(cfg);
}

TEST(BENCHPlugin, ResumeFromCheckpoint) {
    std::string src = write_bench_source(1000);
    std::string cfg = write_bench_config(128);

    TestPlugin a = create_bench_plugin();
    a.startup(cfg, nlohmann::json::object());
    set_bench_source_file(src);

    // Advance 3 units (offset now 384)
    WorkUnitMessage msg;
    for (int i = 0; i < 3; i++) {
        ASSERT_TRUE(a.next_unit(msg));
    }

    // Capture the plugin's checkpoint JSON
    nlohmann::json cp = a.checkpoint();
    EXPECT_EQ(cp.value("offset", -1), 384);

    // The next chunk the original would produce (4th)
    ASSERT_TRUE(a.next_unit(msg));
    int64_t expected_offset = msg.job.value("offset", -1);
    std::string expected_data = msg.job.value("data", "");
    std::string expected_hash = msg.job.value("hash", "");

    // Resume a fresh plugin from the captured checkpoint
    TestPlugin b = create_bench_plugin();
    b.startup(cfg, cp);
    set_bench_source_file(src);

    // b's first chunk must match the original's 4th
    WorkUnitMessage msg2;
    ASSERT_TRUE(b.next_unit(msg2));
    EXPECT_EQ(msg2.job.value("offset", -1), expected_offset);
    EXPECT_EQ(msg2.job.value("data", ""), expected_data);
    EXPECT_EQ(msg2.job.value("hash", ""), expected_hash);

    fs::remove(src);
    fs::remove(cfg);
}

// ── BENCH handler (consumer side) ────────────────────────────────

namespace {

// Write `bytes` of deterministic content to `filename` in the CWD and return
// the SHA-256 of the first `chunk` bytes (the handler hashes the local chunk).
std::string write_local_chunk(const std::string& filename, size_t bytes, size_t chunk) {
    std::vector<uint8_t> content(bytes);
    for (size_t i = 0; i < bytes; i++) content[i] = static_cast<uint8_t>(i % 251);
    std::ofstream f(filename, std::ios::binary);
    f.write(reinterpret_cast<const char*>(content.data()), static_cast<std::streamsize>(bytes));
    return sha256_bytes(content.data(), chunk);
}

} // namespace

TEST(BENCHHandler, Type) {
    BENCH_Handler handler;
    EXPECT_EQ(handler.type(), "BENCH");
}

TEST(BENCHHandler, Match) {
    ChdirGuard guard(temp_path("pc_bench_handler_dir"));
    const std::string filename = "bench_src.bin";
    std::string hash = write_local_chunk(filename, 256, 128);

    WorkUnitMessage work;
    work.work_unit_id = "prod-001-0";
    work.seq = 0;
    work.source_file = filename;
    work.job = nlohmann::json::object();
    work.job["offset"] = 0;
    work.job["chunk_size"] = 128;
    work.job["data"] = "";  // decoded by the handler but not used for the match
    work.job["hash"] = hash;

    BENCH_Handler handler;
    ResultMessage result = handler.handle(work);

    EXPECT_EQ(result.status, "success");
    EXPECT_TRUE(result.result.value("match", false));
    EXPECT_EQ(result.result.value("actual_hash", ""), hash);
    EXPECT_EQ(result.result.value("offset", -1), 0);
}

TEST(BENCHHandler, Mismatch) {
    ChdirGuard guard(temp_path("pc_bench_handler_dir"));
    const std::string filename = "bench_src.bin";
    write_local_chunk(filename, 256, 128);

    WorkUnitMessage work;
    work.work_unit_id = "prod-001-1";
    work.seq = 1;
    work.source_file = filename;
    work.job = nlohmann::json::object();
    work.job["offset"] = 0;
    work.job["chunk_size"] = 128;
    work.job["data"] = "";
    work.job["hash"] = "0000000000000000000000000000000000000000000000000000000000000000";

    BENCH_Handler handler;
    ResultMessage result = handler.handle(work);

    EXPECT_EQ(result.status, "success");
    EXPECT_FALSE(result.result.value("match", true));
}

TEST(BENCHHandler, MissingSourceFile) {
    WorkUnitMessage work;
    work.work_unit_id = "prod-001-2";
    work.seq = 2;
    work.source_file = "no_such_file.bin";
    work.job = nlohmann::json::object();
    work.job["offset"] = 0;
    work.job["chunk_size"] = 128;
    work.job["data"] = "";
    work.job["hash"] = "0000000000000000000000000000000000000000000000000000000000000000";

    BENCH_Handler handler;
    ResultMessage result = handler.handle(work);

    // No local file → empty chunk → no match (reported, not an error)
    EXPECT_EQ(result.status, "success");
    EXPECT_FALSE(result.result.value("match", true));
    EXPECT_EQ(result.result.value("actual_hash", "x"), "");
}
