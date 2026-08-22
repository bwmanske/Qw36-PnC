#ifndef PC_ECHO_HANDLER_H
#define PC_ECHO_HANDLER_H

#include <string>
#include <random>
#include <mutex>
#include "consumer/work_unit_handler.h"

namespace pc {

class ECHO_Handler : public IWorkUnitHandler {
public:
    std::string type() const override;
    ResultMessage handle(const WorkUnitMessage& work) override;
    void configure(const std::string& config_path) override;

private:
    double max_delay_sec_ = 0.0;
    std::mt19937 rng_;
    std::uniform_real_distribution<double> dist_;
    std::mutex rng_mutex_;
};

} // namespace pc

#endif // PC_ECHO_HANDLER_H
