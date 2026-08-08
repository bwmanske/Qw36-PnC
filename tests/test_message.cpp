#include <gtest/gtest.h>
#include "common/message.h"
#include <nlohmann/json.hpp>

using namespace pc;

TEST(WorkUnitMessage, RoundTrip) {
    WorkUnitMessage original;
    original.source_file = "/path/to/jobs.json";
    original.permutation = "random";
    original.permutation_seed = 42;
    original.work_unit_id = "prod-001-5";
    original.seq = 5;
    original.timestamp = "2026-01-01T00:00:00.000Z";
    original.producer_id = "prod-001";
    original.job = nlohmann::json::parse(R"({"job_id":1,"task":"render"})");
    original.source_hash = "abc123";

    std::string s = original.to_string();
    WorkUnitMessage decoded = WorkUnitMessage::from_string(s);

    EXPECT_EQ(decoded.source_file, original.source_file);
    EXPECT_EQ(decoded.permutation, original.permutation);
    EXPECT_EQ(decoded.permutation_seed, original.permutation_seed);
    EXPECT_EQ(decoded.work_unit_id, original.work_unit_id);
    EXPECT_EQ(decoded.seq, original.seq);
    EXPECT_EQ(decoded.timestamp, original.timestamp);
    EXPECT_EQ(decoded.producer_id, original.producer_id);
    EXPECT_EQ(decoded.source_hash, original.source_hash);
    EXPECT_EQ(decoded.job["job_id"], original.job["job_id"]);
}

TEST(WorkUnitMessage, OptionalFields) {
    WorkUnitMessage msg;
    msg.source_file = "/jobs.json";
    msg.permutation = "sequential";
    msg.work_unit_id = "prod-001-0";
    msg.seq = 0;
    msg.timestamp = "2026-01-01T00:00:00.000Z";
    msg.producer_id = "prod-001";
    msg.job = nlohmann::json::parse(R"({"job_id":0})");

    std::string s = msg.to_string();
    nlohmann::json j = nlohmann::json::parse(s);

    EXPECT_FALSE(j.contains("permutation_seed"));
    EXPECT_FALSE(j.contains("source_hash"));
    EXPECT_EQ(j["msg_type"], "work_unit");
}

TEST(ResultMessage, RoundTrip) {
    ResultMessage original;
    original.work_unit_id = "prod-001-5";
    original.seq = 5;
    original.consumer_id = "cons-001";
    original.status = "success";
    original.result = nlohmann::json::parse(R"({"output":"frame.png","duration_ms":1250})");
    original.timestamp = "2026-01-01T00:00:01.000Z";

    std::string s = original.to_string();
    ResultMessage decoded = ResultMessage::from_string(s);

    EXPECT_EQ(decoded.work_unit_id, original.work_unit_id);
    EXPECT_EQ(decoded.seq, original.seq);
    EXPECT_EQ(decoded.consumer_id, original.consumer_id);
    EXPECT_EQ(decoded.status, original.status);
    EXPECT_EQ(decoded.result["output"], original.result["output"]);
}

TEST(ResultMessage, FailureStatus) {
    ResultMessage msg;
    msg.work_unit_id = "prod-001-3";
    msg.seq = 3;
    msg.consumer_id = "cons-001";
    msg.status = "failure";
    msg.result = nlohmann::json::parse(R"({"error":"timeout"})");
    msg.timestamp = "2026-01-01T00:00:02.000Z";

    std::string s = msg.to_string();
    nlohmann::json j = nlohmann::json::parse(s);

    EXPECT_EQ(j["msg_type"], "result");
    EXPECT_EQ(j["status"], "failure");
}

TEST(WorkRequestMessage, RoundTrip) {
    WorkRequestMessage original;
    original.consumer_id = "cons-001";
    original.threads_available = 4;
    original.timestamp = "2026-01-01T00:00:00.500Z";

    std::string s = original.to_string();
    WorkRequestMessage decoded = WorkRequestMessage::from_string(s);

    EXPECT_EQ(decoded.consumer_id, original.consumer_id);
    EXPECT_EQ(decoded.threads_available, original.threads_available);
    EXPECT_EQ(decoded.timestamp, original.timestamp);
}

TEST(WorkRequestMessage, MsgType) {
    WorkRequestMessage msg;
    msg.consumer_id = "cons-001";
    msg.threads_available = 2;
    msg.timestamp = "2026-01-01T00:00:00.000Z";

    nlohmann::json j = msg.to_json();
    EXPECT_EQ(j["msg_type"], "work_request");
}
