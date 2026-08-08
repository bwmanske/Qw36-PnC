#ifndef PC_PWD_HANDLER_H
#define PC_PWD_HANDLER_H

#include "consumer/work_unit_handler.h"

namespace pc {

class PWD_Handler : public IWorkUnitHandler {
public:
    std::string type() const override;
    ResultMessage handle(const WorkUnitMessage& work) override;
};

} // namespace pc

#endif // PC_PWD_HANDLER_H
