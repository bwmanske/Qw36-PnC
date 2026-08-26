#include <gtest/gtest.h>
#include "common/checkpoint.h"
#include <filesystem>
#include <fstream>
#include <chrono>
#include <functional>
#include <cstdlib>

namespace fs = std::filesystem;
using namespace pc;

static std::string temp_dir() {
#ifdef _WIN32
    const char* t = std::getenv("TEMP");
    return t ? t : "C:\\Temp";
#else
    return std::string("/tmp");
#endif
}

static std::string test_dir() {
    return temp_dir() + "/pc_checkpoint_test_" +
           std::to_string(static_cast<unsigned>(std::hash<std::string>{}(
               std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())) % 100000));
}

TEST(CheckpointState, RoundTrip) {
    CheckpointState original;
    original.producer_id = "prod-001";
    original.source_file = "/path/to/jobs.json";
    original.permutation = "random";
    original.permutation_seed = 42;
    original.total_jobs = 100;
    original.last_completed_seq = 42;
    original.last_completed_work_unit_id = "prod-001-42";
    original.completed_count = 43;
    original.pending_count = 57;
    original.failed_count = 0;
    original.checkpoint_timestamp = "2026-01-01T00:00:00.000Z";
    original.consumers_connected.push_back({"cons-001", 3});
    original.consumers_connected.push_back({"cons-002", 5});

    nlohmann::json j = original.to_json();
    CheckpointState decoded = CheckpointState::from_json(j);

    EXPECT_EQ(decoded.producer_id, original.producer_id);
    EXPECT_EQ(decoded.source_file, original.source_file);
    EXPECT_EQ(decoded.permutation, original.permutation);
    EXPECT_EQ(decoded.permutation_seed, original.permutation_seed);
    EXPECT_EQ(decoded.total_jobs, original.total_jobs);
    EXPECT_EQ(decoded.last_completed_seq, original.last_completed_seq);
    EXPECT_EQ(decoded.completed_count, original.completed_count);
    EXPECT_EQ(decoded.pending_count, original.pending_count);
    EXPECT_EQ(decoded.consumers_connected.size(), original.consumers_connected.size());
}

TEST(CheckpointManager, SaveAndLoad) {
    std::string dir = test_dir();
    fs::create_directories(dir);

    CheckpointManager mgr(dir);

    CheckpointState state;
    state.producer_id = "prod-test";
    state.source_file = "/test/jobs.json";
    state.permutation = "sequential";
    state.permutation_seed = 0;
    state.total_jobs = 50;
    state.last_completed_seq = 25;
    state.last_completed_work_unit_id = "prod-test-25";
    state.completed_count = 26;
    state.pending_count = 24;
    state.failed_count = 0;
    state.checkpoint_timestamp = "2026-01-01T00:00:00.000Z";

    mgr.save(state);
    EXPECT_TRUE(mgr.exists());

    auto loaded = mgr.load();
    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ(loaded->producer_id, "prod-test");
    EXPECT_EQ(loaded->last_completed_seq, 25);
    EXPECT_EQ(loaded->completed_count, 26);

    fs::remove_all(dir);
}

TEST(CheckpointManager, BackupCreated) {
    std::string dir = test_dir();
    fs::create_directories(dir);

    CheckpointManager mgr(dir);

    CheckpointState state1;
    state1.producer_id = "prod-test";
    state1.source_file = "/test/jobs.json";
    state1.permutation = "sequential";
    state1.permutation_seed = 0;
    state1.total_jobs = 50;
    state1.last_completed_seq = 10;
    state1.last_completed_work_unit_id = "prod-test-10";
    state1.completed_count = 11;
    state1.pending_count = 39;
    state1.failed_count = 0;
    state1.checkpoint_timestamp = "2026-01-01T00:00:00.000Z";

    mgr.save(state1);

    CheckpointState state2;
    state2 = state1;
    state2.last_completed_seq = 20;
    state2.last_completed_work_unit_id = "prod-test-20";
    state2.completed_count = 21;
    state2.pending_count = 29;
    state2.checkpoint_timestamp = "2026-01-01T00:01:00.000Z";

    mgr.save(state2);

    EXPECT_TRUE(fs::exists(mgr.backup_path()));

    auto backup = nlohmann::json::parse(std::ifstream(mgr.backup_path()));
    EXPECT_EQ(backup["last_completed_seq"], 10);

    auto primary = nlohmann::json::parse(std::ifstream(mgr.primary_path()));
    EXPECT_EQ(primary["last_completed_seq"], 20);

    fs::remove_all(dir);
}

TEST(CheckpointManager, CorruptPrimaryUsesBackup) {
    std::string dir = test_dir();
    fs::create_directories(dir);

    CheckpointManager mgr(dir);

    CheckpointState state;
    state.producer_id = "prod-test";
    state.source_file = "/test/jobs.json";
    state.permutation = "sequential";
    state.permutation_seed = 0;
    state.total_jobs = 50;
    state.last_completed_seq = 15;
    state.last_completed_work_unit_id = "prod-test-15";
    state.completed_count = 16;
    state.pending_count = 34;
    state.failed_count = 0;
    state.checkpoint_timestamp = "2026-01-01T00:00:00.000Z";

    mgr.save(state);

    // Second save creates backup from prior primary
    state.last_completed_seq = 25;
    state.completed_count = 26;
    state.pending_count = 24;
    state.checkpoint_timestamp = "2026-01-01T00:01:00.000Z";
    mgr.save(state);

    // Corrupt primary — load should fall back to backup (seq == 15)
    std::string corrupt = "this is not valid json {{{";
    std::ofstream(mgr.primary_path(), std::ios::trunc) << corrupt;

    auto loaded = mgr.load();
    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ(loaded->last_completed_seq, 15);

    fs::remove_all(dir);
}

TEST(CheckpointManager, NotExists) {
    std::string dir = test_dir();
    fs::create_directories(dir);

    CheckpointManager mgr(dir);
    EXPECT_FALSE(mgr.exists());

    auto loaded = mgr.load();
    EXPECT_FALSE(loaded.has_value());

    fs::remove_all(dir);
}

TEST(CheckpointManager, Paths) {
    std::string dir = test_dir();
    CheckpointManager mgr(dir);
    EXPECT_EQ(mgr.directory(), dir);
    EXPECT_EQ(mgr.primary_path(), dir + "/state.json");
    EXPECT_EQ(mgr.backup_path(), dir + "/state.backup.json");
    fs::remove_all(dir);
}
