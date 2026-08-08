#include "consumer/PWD_Handler.h"
#include <iostream>
#include <chrono>
#include <thread>

namespace pc {

std::string PWD_Handler::type() const {
    return "PWD";
}

ResultMessage PWD_Handler::handle(const WorkUnitMessage& work) {
    ResultMessage result;
    result.work_unit_id = work.work_unit_id;
    result.seq = work.seq;
    result.timestamp = "";

    try {
        std::string password = work.job.value("password", "");
        std::cout << "[PWD_Handler] Processing work_unit=" << work.work_unit_id
                  << " password=[" << password << "]\n";

        std::this_thread::sleep_for(std::chrono::milliseconds(10));

        result.status = "success";
        result.result = nlohmann::json::object();
        result.result["password"] = password;
        result.result["output"] = "processed";
        result.result["duration_ms"] = 10;
    } catch (const std::exception& e) {
        std::cerr << "[PWD_Handler] Processing failed for " << work.work_unit_id
                  << ": " << e.what() << "\n";
        result.status = "failure";
        result.result = nlohmann::json::object();
        result.result["error"] = e.what();
    }

    return result;
}

} // namespace pc
