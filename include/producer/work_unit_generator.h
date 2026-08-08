#ifndef PC_WORK_UNIT_GENERATOR_H
#define PC_WORK_UNIT_GENERATOR_H

#include <string>
#include <nlohmann/json.hpp>
#include "common/message.h"

namespace pc {

class IWorkUnitGenerator {
public:
    virtual ~IWorkUnitGenerator() = default;

    virtual std::string type() const = 0;

    virtual bool next(WorkUnitMessage& out) = 0;

    virtual void reset() = 0;

    virtual nlohmann::json config() const = 0;
    virtual void load_config(const nlohmann::json& j) = 0;
};

} // namespace pc

#endif // PC_WORK_UNIT_GENERATOR_H
