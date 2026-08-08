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
