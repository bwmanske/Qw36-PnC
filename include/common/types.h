#ifndef PC_TYPES_H
#define PC_TYPES_H

#include <cstdint>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace pc {

enum class Transport {
    TCP,
    UDP
};

enum class MessageType {
    WorkUnit,
    Result,
    WorkRequest
};

enum class WorkUnitStatus {
    Pending,
    Sent,
    Completed,
    Failed
};

struct WorkUnitEntry {
    std::string work_unit_id;
    int64_t seq;
    nlohmann::json job;
    WorkUnitStatus status;
    std::string consumer_id;
    std::string sent_at;
    std::string completed_at;
};

struct ConsumerState {
    std::string consumer_id;
    int pending_units;
};

} // namespace pc

#endif // PC_TYPES_H
