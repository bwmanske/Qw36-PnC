#include <gtest/gtest.h>
#include "producer/ECHO_plugin.h"
#include "consumer/ECHO_Handler.h"
#include "common/util.h"
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;
using namespace pc;

namespace {

std::string write_echo_config(int payload_size, int64_t total_units) {
    std::string path;
#ifdef _WIN32
    const char* t = std::getenv("TEMP");
    path = std::string(t ? t : "C:\\Temp") + "\\pc_echo_test_" +
           std::to_string(static_cast<unsigned>(
               std::hash<std::string>{}(std::to_string(
                   std::chrono::steady_clock::now().time_since_epoch().count())) % 1000000)) + ".json";
#else
    path = "/tmp/pc_echo_test_" + std::to_string(getpid()) + ".json";
#endif
    std::ofstream f(path);
    f << R"({"payload_size":)" << payload_size << R"(,"total_units":)" << total_units << "}";
    return path;
}

} // namespace

TEST(EchoPlugin, IsValid) {
    TestPlugin plugin = create_echo_plugin();
    EXPECT_TRUE(plugin.is_valid());
}

TEST(EchoPlugin, GeneratesTotalUnitsThenExhausts) {
    std::string cfg = write_echo_config(16, 5);
    TestPlugin plugin = create_echo_plugin();
    plugin.startup(cfg, nlohmann::json::object());

    int generated = 0;
    WorkUnitMessage msg;
    while (plugin.next_unit(msg)) {
        generated++;
        EXPECT_EQ(msg.job.value("task", ""), "ECHO");
        EXPECT_EQ(msg.job.value("payload", "").size(), 16u);
        EXPECT_FALSE(msg.job.value("hash", "").empty());
        if (generated > 100) break;
    }
    EXPECT_EQ(generated, 5);

    fs::remove(cfg);
}

TEST(EchoPlugin, ExitConditions) {
    std::string cfg = write_echo_config(8, 3);
    TestPlugin plugin = create_echo_plugin();
    plugin.startup(cfg, nlohmann::json::object());

    EXPECT_FALSE(plugin.exit_conditions());

    WorkUnitMessage msg;
    plugin.next_unit(msg);
    plugin.next_unit(msg);
    EXPECT_FALSE(plugin.exit_conditions());

    plugin.next_unit(msg);
    EXPECT_TRUE(plugin.exit_conditions());

    fs::remove(cfg);
}

TEST(EchoPlugin, CheckpointState) {
    std::string cfg = write_echo_config(8, 10);
    TestPlugin plugin = create_echo_plugin();
    plugin.startup(cfg, nlohmann::json::object());

    WorkUnitMessage msg;
    plugin.next_unit(msg);
    plugin.next_unit(msg);
    plugin.next_unit(msg);

    nlohmann::json cp = plugin.checkpoint();
    EXPECT_EQ(cp.value("generated", -1), 3);
    EXPECT_EQ(cp.value("seq", -1), 3);

    fs::remove(cfg);
}

TEST(EchoPlugin, ResumeFromCheckpoint) {
    std::string cfg = write_echo_config(8, 5);
    TestPlugin plugin = create_echo_plugin();

    nlohmann::json resume;
    resume["generated"] = 3;
    resume["seq"] = 3;
    plugin.startup(cfg, resume);

    int generated = 0;
    WorkUnitMessage msg;
    while (plugin.next_unit(msg)) {
        generated++;
        if (generated > 100) break;
    }
    // 5 total - 3 resumed = 2 remaining
    EXPECT_EQ(generated, 2);
    EXPECT_TRUE(plugin.exit_conditions());

    fs::remove(cfg);
}

TEST(EchoPlugin, UnlimitedUnits) {
    std::string cfg = write_echo_config(8, 0);
    TestPlugin plugin = create_echo_plugin();
    plugin.startup(cfg, nlohmann::json::object());

    EXPECT_FALSE(plugin.exit_conditions());

    WorkUnitMessage msg;
    for (int i = 0; i < 100; i++) {
        EXPECT_TRUE(plugin.next_unit(msg));
    }
    EXPECT_FALSE(plugin.exit_conditions());

    fs::remove(cfg);
}

TEST(EchoHandler, Type) {
    ECHO_Handler handler;
    EXPECT_EQ(handler.type(), "ECHO");
}

TEST(EchoHandler, HashMatch) {
    ECHO_Handler handler;

    std::string payload = "hello world payload";
    std::string hash = sha256_bytes(
        reinterpret_cast<const uint8_t*>(payload.data()), payload.size());

    WorkUnitMessage work;
    work.work_unit_id = "prod-001-0";
    work.seq = 0;
    work.job = nlohmann::json::object();
    work.job["payload"] = payload;
    work.job["hash"] = hash;

    ResultMessage result = handler.handle(work);

    EXPECT_EQ(result.status, "success");
    EXPECT_EQ(result.work_unit_id, "prod-001-0");
    EXPECT_EQ(result.seq, 0);
    EXPECT_TRUE(result.result.value("match", false));
    EXPECT_EQ(result.result.value("payload_size", -1), static_cast<int>(payload.size()));
    EXPECT_EQ(result.result.value("actual_hash", ""), hash);
}

TEST(EchoHandler, HashMismatch) {
    ECHO_Handler handler;

    WorkUnitMessage work;
    work.work_unit_id = "prod-001-1";
    work.seq = 1;
    work.job = nlohmann::json::object();
    work.job["payload"] = "some payload";
    work.job["hash"] = "0000000000000000000000000000000000000000000000000000000000000000";

    ResultMessage result = handler.handle(work);

    EXPECT_EQ(result.status, "success");
    EXPECT_FALSE(result.result.value("match", true));
}

TEST(EchoHandler, EmptyPayload) {
    ECHO_Handler handler;

    std::string hash = sha256_bytes(nullptr, 0);

    WorkUnitMessage work;
    work.work_unit_id = "prod-001-2";
    work.seq = 2;
    work.job = nlohmann::json::object();
    work.job["payload"] = "";
    work.job["hash"] = hash;

    ResultMessage result = handler.handle(work);

    EXPECT_EQ(result.status, "success");
    EXPECT_TRUE(result.result.value("match", false));
    EXPECT_EQ(result.result.value("payload_size", -1), 0);
}
