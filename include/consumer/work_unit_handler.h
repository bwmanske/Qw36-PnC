#ifndef PC_WORK_UNIT_HANDLER_H
#define PC_WORK_UNIT_HANDLER_H

#include <string>
#include "common/message.h"

namespace pc {

class IWorkUnitHandler {
public:
    virtual ~IWorkUnitHandler() = default;

    virtual std::string type() const = 0;

    virtual ResultMessage handle(const WorkUnitMessage& work) = 0;

    virtual void configure(const std::string& config_path) {}
};

} // namespace pc

#endif // PC_WORK_UNIT_HANDLER_H
