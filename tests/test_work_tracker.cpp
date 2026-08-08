#include <gtest/gtest.h>
#include "producer/work_tracker.h"
#include <nlohmann/json.hpp>

using namespace pc;

TEST(WorkTracker, AddAndFind) {
    WorkTracker tracker;
    WorkUnitEntry entry;
    entry.work_unit_id = "prod-001-0";
    entry.seq = 0;
    entry.job = nlohmann::json::parse(R"({"job_id":1})");
    entry.status = WorkUnitStatus::Pending;

    tracker.add_pending(entry);
    auto found = tracker.find("prod-001-0");
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->seq, 0);
    EXPECT_EQ(found->status, WorkUnitStatus::Pending);
}

TEST(WorkTracker, MarkSent) {
    WorkTracker tracker;
    WorkUnitEntry entry;
    entry.work_unit_id = "wu-1";
    entry.seq = 1;
    entry.job = nlohmann::json::object();
    entry.status = WorkUnitStatus::Pending;
    tracker.add_pending(entry);

    tracker.mark_sent("wu-1", "cons-001");
    auto found = tracker.find("wu-1");
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->status, WorkUnitStatus::Sent);
    EXPECT_EQ(found->consumer_id, "cons-001");
}

TEST(WorkTracker, MarkCompleted) {
    WorkTracker tracker;
    WorkUnitEntry entry;
    entry.work_unit_id = "wu-1";
    entry.seq = 1;
    entry.job = nlohmann::json::object();
    entry.status = WorkUnitStatus::Pending;
    tracker.add_pending(entry);

    tracker.mark_sent("wu-1", "cons-001");
    tracker.mark_completed("wu-1");

    EXPECT_EQ(tracker.last_completed_seq(), 1);
    EXPECT_EQ(tracker.completed_count(), 1);
}

TEST(WorkTracker, MarkFailedReturnsToPending) {
    WorkTracker tracker;
    WorkUnitEntry entry;
    entry.work_unit_id = "wu-1";
    entry.seq = 1;
    entry.job = nlohmann::json::object();
    entry.status = WorkUnitStatus::Pending;
    tracker.add_pending(entry);

    tracker.mark_sent("wu-1", "cons-001");
    tracker.mark_failed("wu-1");

    auto found = tracker.find("wu-1");
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->status, WorkUnitStatus::Pending);
    EXPECT_EQ(found->consumer_id, "");
    EXPECT_EQ(tracker.failed_count(), 1);
}

TEST(WorkTracker, GetPending) {
    WorkTracker tracker;
    for (int i = 0; i < 5; i++) {
        WorkUnitEntry entry;
        entry.work_unit_id = "wu-" + std::to_string(i);
        entry.seq = i;
        entry.job = nlohmann::json::object();
        entry.status = WorkUnitStatus::Pending;
        tracker.add_pending(entry);
    }

    auto pending = tracker.get_pending(3);
    EXPECT_EQ(pending.size(), 3u);
}

TEST(WorkTracker, GetPendingSkipsSent) {
    WorkTracker tracker;
    for (int i = 0; i < 5; i++) {
        WorkUnitEntry entry;
        entry.work_unit_id = "wu-" + std::to_string(i);
        entry.seq = i;
        entry.job = nlohmann::json::object();
        entry.status = WorkUnitStatus::Pending;
        tracker.add_pending(entry);
    }

    tracker.mark_sent("wu-0", "cons-001");
    tracker.mark_sent("wu-1", "cons-001");

    auto pending = tracker.get_pending(10);
    EXPECT_EQ(pending.size(), 3u);
}

TEST(WorkTracker, FailedForConsumer) {
    WorkTracker tracker;
    for (int i = 0; i < 3; i++) {
        WorkUnitEntry entry;
        entry.work_unit_id = "wu-" + std::to_string(i);
        entry.seq = i;
        entry.job = nlohmann::json::object();
        entry.status = WorkUnitStatus::Pending;
        tracker.add_pending(entry);
    }

    tracker.mark_sent("wu-0", "cons-001");
    tracker.mark_sent("wu-1", "cons-001");
    tracker.mark_sent("wu-2", "cons-002");

    auto failed = tracker.get_failed_for_consumer("cons-001");
    EXPECT_EQ(failed.size(), 2u);

    auto wu0 = tracker.find("wu-0");
    ASSERT_TRUE(wu0.has_value());
    EXPECT_EQ(wu0->status, WorkUnitStatus::Pending);

    auto wu2 = tracker.find("wu-2");
    ASSERT_TRUE(wu2.has_value());
    EXPECT_EQ(wu2->status, WorkUnitStatus::Sent);
}

TEST(WorkTracker, PendingCount) {
    WorkTracker tracker;
    for (int i = 0; i < 5; i++) {
        WorkUnitEntry entry;
        entry.work_unit_id = "wu-" + std::to_string(i);
        entry.seq = i;
        entry.job = nlohmann::json::object();
        entry.status = WorkUnitStatus::Pending;
        tracker.add_pending(entry);
    }

    tracker.mark_sent("wu-0", "cons-001");
    tracker.mark_completed("wu-0");

    EXPECT_EQ(tracker.pending_count(), 4);
}

TEST(WorkTracker, ToCheckpoint) {
    WorkTracker tracker;
    for (int i = 0; i < 10; i++) {
        WorkUnitEntry entry;
        entry.work_unit_id = "wu-" + std::to_string(i);
        entry.seq = i;
        entry.job = nlohmann::json::object();
        entry.status = WorkUnitStatus::Pending;
        tracker.add_pending(entry);
    }

    tracker.mark_sent("wu-0", "cons-001");
    tracker.mark_completed("wu-0");

    auto state = tracker.to_checkpoint(10);
    EXPECT_EQ(state.total_jobs, 10);
    EXPECT_EQ(state.last_completed_seq, 0);
    EXPECT_EQ(state.completed_count, 1);
    EXPECT_EQ(state.pending_count, 9);
}

TEST(WorkTracker, NotFound) {
    WorkTracker tracker;
    auto found = tracker.find("nonexistent");
    EXPECT_FALSE(found.has_value());
}
