#ifndef PC_BENCH_HANDLER_H
#define PC_BENCH_HANDLER_H

#include "consumer/work_unit_handler.h"

namespace pc {

class BENCH_Handler : public IWorkUnitHandler {
public:
    std::string type() const override;
    ResultMessage handle(const WorkUnitMessage& work) override;
};

} // namespace pc

#endif // PC_BENCH_HANDLER_H
