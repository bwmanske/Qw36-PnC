#ifndef PC_MESSAGE_H
#define PC_MESSAGE_H

#include <string>
#include <optional>
#include <nlohmann/json.hpp>
#include "common/types.h"

namespace pc {

struct WorkUnitMessage {
    std::string test_type;
    std::string source_file;
    std::string permutation;
    std::optional<int64_t> permutation_seed;
    std::string work_unit_id;
    int64_t seq;
    std::string timestamp;
    std::string producer_id;
    nlohmann::json job;
    std::optional<std::string> source_hash;

    nlohmann::json to_json() const;
    static WorkUnitMessage from_json(const nlohmann::json& j);
    std::string to_string() const;
    static WorkUnitMessage from_string(const std::string& s);
};

struct ResultMessage {
    std::string work_unit_id;
    int64_t seq;
    std::string consumer_id;
    std::string status;
    nlohmann::json result;
    std::string timestamp;
    std::optional<std::string> found_password;
    std::optional<std::string> file_error;

    nlohmann::json to_json() const;
    static ResultMessage from_json(const nlohmann::json& j);
    std::string to_string() const;
    static ResultMessage from_string(const std::string& s);
};

struct WorkRequestMessage {
    std::string consumer_id;
    int threads_available;
    std::string timestamp;

    nlohmann::json to_json() const;
    static WorkRequestMessage from_json(const nlohmann::json& j);
    std::string to_string() const;
    static WorkRequestMessage from_string(const std::string& s);
};

struct HeartbeatMessage {
    std::string consumer_id;
    std::string timestamp;

    nlohmann::json to_json() const;
    static HeartbeatMessage from_json(const nlohmann::json& j);
    std::string to_string() const;
    static HeartbeatMessage from_string(const std::string& s);
};

struct VersionMessage {
    std::string version;
    std::string consumer_id;

    nlohmann::json to_json() const;
    static VersionMessage from_json(const nlohmann::json& j);
    std::string to_string() const;
    static VersionMessage from_string(const std::string& s);
};

} // namespace pc

#endif // PC_MESSAGE_H
