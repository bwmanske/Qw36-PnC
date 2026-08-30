#ifndef PC_TEST_PLUGIN_H
#define PC_TEST_PLUGIN_H

#include <string>
#include <functional>
#include <nlohmann/json.hpp>
#include "common/message.h"

namespace pc {

struct TestPlugin {
    std::function<void(const std::string&, const nlohmann::json&)> startup;
    std::function<bool(WorkUnitMessage&)> next_unit;
    std::function<nlohmann::json()> checkpoint;
    std::function<bool()> exit_conditions;
    // Optional "output plugin": returns plugin-specific status lines for the
    // producer's in-place console display. May be empty.
    std::function<std::string()> status;

    bool is_valid() const {
        return startup && next_unit && checkpoint && exit_conditions;
    }
};

} // namespace pc

#endif // PC_TEST_PLUGIN_H
