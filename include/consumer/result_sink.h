#ifndef PC_RESULT_SINK_H
#define PC_RESULT_SINK_H

#include <string>
#include <functional>
#include <nlohmann/json.hpp>
#include "common/message.h"

namespace pc {

class IResultSink {
public:
    virtual ~IResultSink() = default;

    virtual std::string type() const = 0;

    virtual void on_result(const ResultMessage& result) = 0;

    virtual bool should_stop() const = 0;

    virtual nlohmann::json summary() const = 0;
};

} // namespace pc

#endif // PC_RESULT_SINK_H
