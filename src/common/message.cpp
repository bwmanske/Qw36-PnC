#include "common/message.h"
#include <stdexcept>

namespace pc {

// ── WorkUnitMessage ──────────────────────────────────────────────

nlohmann::json WorkUnitMessage::to_json() const {
    nlohmann::json j;
    j["msg_type"] = "work_unit";
    j["test_type"] = test_type;
    j["source_file"] = source_file;
    j["permutation"] = permutation;
    if (permutation_seed.has_value())
        j["permutation_seed"] = permutation_seed.value();
    j["work_unit_id"] = work_unit_id;
    j["seq"] = seq;
    j["timestamp"] = timestamp;
    j["producer_id"] = producer_id;
    j["job"] = job;
    if (source_hash.has_value())
        j["source_hash"] = source_hash.value();
    return j;
}

WorkUnitMessage WorkUnitMessage::from_json(const nlohmann::json& j) {
    WorkUnitMessage msg;
    msg.test_type = j.value("test_type", "");
    msg.source_file = j.value("source_file", "");
    msg.permutation = j.value("permutation", "");
    if (j.contains("permutation_seed"))
        msg.permutation_seed = j["permutation_seed"].get<int64_t>();
    msg.work_unit_id = j.value("work_unit_id", "");
    msg.seq = j.value("seq", 0);
    msg.timestamp = j.value("timestamp", "");
    msg.producer_id = j.value("producer_id", "");
    msg.job = j.value("job", nlohmann::json::object());
    if (j.contains("source_hash"))
        msg.source_hash = j["source_hash"].get<std::string>();
    return msg;
}

std::string WorkUnitMessage::to_string() const {
    return to_json().dump();
}

WorkUnitMessage WorkUnitMessage::from_string(const std::string& s) {
    return from_json(nlohmann::json::parse(s));
}

// ── ResultMessage ────────────────────────────────────────────────

nlohmann::json ResultMessage::to_json() const {
    nlohmann::json j;
    j["msg_type"] = "result";
    j["work_unit_id"] = work_unit_id;
    j["seq"] = seq;
    j["consumer_id"] = consumer_id;
    j["status"] = status;
    j["result"] = result;
    j["timestamp"] = timestamp;
    if (found_password.has_value())
        j["found_password"] = found_password.value();
    if (file_error.has_value())
        j["file_error"] = file_error.value();
    return j;
}

ResultMessage ResultMessage::from_json(const nlohmann::json& j) {
    ResultMessage msg;
    msg.work_unit_id = j.value("work_unit_id", "");
    msg.seq = j.value("seq", 0);
    msg.consumer_id = j.value("consumer_id", "");
    msg.status = j.value("status", "");
    msg.result = j.value("result", nlohmann::json::object());
    msg.timestamp = j.value("timestamp", "");
    if (j.contains("found_password"))
        msg.found_password = j["found_password"].get<std::string>();
    if (j.contains("file_error"))
        msg.file_error = j["file_error"].get<std::string>();
    return msg;
}

std::string ResultMessage::to_string() const {
    return to_json().dump();
}

ResultMessage ResultMessage::from_string(const std::string& s) {
    return from_json(nlohmann::json::parse(s));
}

// ── WorkRequestMessage ───────────────────────────────────────────

nlohmann::json WorkRequestMessage::to_json() const {
    nlohmann::json j;
    j["msg_type"] = "work_request";
    j["consumer_id"] = consumer_id;
    j["threads_available"] = threads_available;
    j["timestamp"] = timestamp;
    return j;
}

WorkRequestMessage WorkRequestMessage::from_json(const nlohmann::json& j) {
    WorkRequestMessage msg;
    msg.consumer_id = j.value("consumer_id", "");
    msg.threads_available = j.value("threads_available", 0);
    msg.timestamp = j.value("timestamp", "");
    return msg;
}

std::string WorkRequestMessage::to_string() const {
    return to_json().dump();
}

WorkRequestMessage WorkRequestMessage::from_string(const std::string& s) {
    return from_json(nlohmann::json::parse(s));
}

// ── HeartbeatMessage ─────────────────────────────────────────────

nlohmann::json HeartbeatMessage::to_json() const {
    nlohmann::json j;
    j["msg_type"] = "heartbeat";
    j["consumer_id"] = consumer_id;
    j["timestamp"] = timestamp;
    return j;
}

HeartbeatMessage HeartbeatMessage::from_json(const nlohmann::json& j) {
    HeartbeatMessage msg;
    msg.consumer_id = j.value("consumer_id", "");
    msg.timestamp = j.value("timestamp", "");
    return msg;
}

std::string HeartbeatMessage::to_string() const {
    return to_json().dump();
}

HeartbeatMessage HeartbeatMessage::from_string(const std::string& s) {
    return from_json(nlohmann::json::parse(s));
}

} // namespace pc
