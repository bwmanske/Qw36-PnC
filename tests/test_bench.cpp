#include <gtest/gtest.h>
#include "producer/BENCH_plugin.h"
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <chrono>

namespace fs = std::filesystem;
using namespace pc;

namespace {

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
