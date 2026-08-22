#ifndef PC_FILE_RESULT_SINK_H
#define PC_FILE_RESULT_SINK_H

#include <string>
#include <mutex>
#include <fstream>
#include <chrono>
#include <nlohmann/json.hpp>
#include "consumer/result_sink.h"

namespace pc {

class FileResultSink : public IResultSink {
public:
    explicit FileResultSink(const std::string& file_path, int max_failures = 0, int max_duration_sec = 0);
    ~FileResultSink() override = default;

    std::string type() const override;
    void on_result(const ResultMessage& result) override;
    bool should_stop() const override;
    nlohmann::json summary() const override;

private:
    std::string file_path_;
    std::ofstream file_;
    mutable std::mutex mtx_;

    int64_t total_ = 0;
    int64_t successes_ = 0;
    int64_t failures_ = 0;

    int max_failures_ = 0;
    int max_duration_sec_ = 0;
    std::chrono::steady_clock::time_point start_time_;
};

} // namespace pc

#endif // PC_FILE_RESULT_SINK_H
