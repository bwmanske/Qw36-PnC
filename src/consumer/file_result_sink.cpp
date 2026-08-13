#include "consumer/file_result_sink.h"
#include <iostream>

namespace pc {

FileResultSink::FileResultSink(const std::string& file_path)
    : file_path_(file_path) {
    file_.open(file_path_, std::ios::app);
    if (!file_.is_open()) {
        std::cerr << "[sink] Cannot open result file: " << file_path_ << "\n";
    }
}

std::string FileResultSink::type() const {
    return "file";
}

void FileResultSink::on_result(const ResultMessage& result) {
    std::lock_guard<std::mutex> lock(mtx_);
    total_++;
    if (result.status == "success") {
        successes_++;
    } else {
        failures_++;
    }

    if (file_.is_open()) {
        nlohmann::json j = result.to_json();
        j["sink_stats"] = nlohmann::json{
            {"total", total_},
            {"successes", successes_},
            {"failures", failures_}
        };
        file_ << j.dump() << "\n";
        file_.flush();
    }
}

bool FileResultSink::should_stop() const {
    return false;
}

nlohmann::json FileResultSink::summary() const {
    return nlohmann::json{
        {"type", type()},
        {"file", file_path_},
        {"total", total_},
        {"successes", successes_},
        {"failures", failures_}
    };
}

} // namespace pc
