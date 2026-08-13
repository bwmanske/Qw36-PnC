#include "consumer/PWD_Handler.h"
#include "common/archive_validator.h"
#include <iostream>
#include <chrono>
#include <thread>
#include <atomic>

namespace pc {

static std::atomic<bool> g_pwd_file_error{false};
static std::string g_pwd_file_error_msg;

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
        std::string archive_path = work.source_file;

        if (g_pwd_file_error) {
            result.status = "failure";
            result.result = nlohmann::json::object();
            result.result["error"] = g_pwd_file_error_msg;
            result.file_error = g_pwd_file_error_msg;
            return result;
        }

        auto start = std::chrono::steady_clock::now();

        auto vr = ArchiveValidator::validate(archive_path, password);

        auto end = std::chrono::steady_clock::now();
        auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

        if (vr.valid) {
            result.status = "success";
            result.found_password = password;
            result.result = nlohmann::json::object();
            result.result["password"] = password;
            result.result["output"] = "password_valid";
            result.result["duration_ms"] = duration_ms;
        } else if (vr.error == ArchiveValidator::Error::FileError) {
            g_pwd_file_error = true;
            g_pwd_file_error_msg = vr.message;
            result.status = "failure";
            result.file_error = vr.message;
            result.result = nlohmann::json::object();
            result.result["error"] = vr.message;
            result.result["duration_ms"] = duration_ms;
        } else {
            result.status = "failure";
            result.result = nlohmann::json::object();
            result.result["password"] = password;
            result.result["output"] = "wrong_password";
            result.result["duration_ms"] = duration_ms;
        }
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
