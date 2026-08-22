#include <gtest/gtest.h>
#include "consumer/file_result_sink.h"
#include <filesystem>
#include <fstream>
#include <cstdlib>
#include <thread>

namespace fs = std::filesystem;
using namespace pc;

static std::string temp_dir() {
#ifdef _WIN32
    const char* t = std::getenv("TEMP");
    return t ? t : "C:\\Temp";
#else
    return std::string("/tmp");
#endif
}

static std::string test_file() {
    return temp_dir() + "/pc_sink_test_" +
           std::to_string(static_cast<unsigned>(std::hash<std::string>{}(
               std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())) % 100000)) +
           ".jsonl";
}

TEST(FileResultSink, WritesJsonLines) {
    std::string path = test_file();

    {
        FileResultSink sink(path);

        ResultMessage r;
        r.work_unit_id = "wu-0";
        r.seq = 0;
        r.consumer_id = "cons-001";
        r.status = "success";
        r.result = nlohmann::json::parse(R"({"output":"ok"})");
        r.timestamp = "2026-01-01T00:00:00.000Z";
        sink.on_result(r);
    }

    EXPECT_TRUE(fs::exists(path));
    std::string line;
    {
        std::ifstream ifs(path);
        std::getline(ifs, line);
    }
    nlohmann::json j = nlohmann::json::parse(line);
    EXPECT_EQ(j["msg_type"], "result");
    EXPECT_EQ(j["status"], "success");
    EXPECT_EQ(j["sink_stats"]["total"], 1);
    EXPECT_EQ(j["sink_stats"]["successes"], 1);
    EXPECT_EQ(j["sink_stats"]["failures"], 0);

    fs::remove(path);
}

TEST(FileResultSink, CountsSuccessesAndFailures) {
    std::string path = test_file();

    {
        FileResultSink sink(path);

        ResultMessage r1;
        r1.work_unit_id = "wu-0";
        r1.seq = 0;
        r1.consumer_id = "cons-001";
        r1.status = "success";
        r1.result = nlohmann::json::object();
        r1.timestamp = "2026-01-01T00:00:00.000Z";
        sink.on_result(r1);

        ResultMessage r2;
        r2.work_unit_id = "wu-1";
        r2.seq = 1;
        r2.consumer_id = "cons-001";
        r2.status = "failure";
        r2.result = nlohmann::json::parse(R"({"error":"timeout"})");
        r2.timestamp = "2026-01-01T00:00:01.000Z";
        sink.on_result(r2);

        ResultMessage r3;
        r3.work_unit_id = "wu-2";
        r3.seq = 2;
        r3.consumer_id = "cons-001";
        r3.status = "success";
        r3.result = nlohmann::json::object();
        r3.timestamp = "2026-01-01T00:00:02.000Z";
        sink.on_result(r3);

        auto summary = sink.summary();
        EXPECT_EQ(summary["total"], 3);
        EXPECT_EQ(summary["successes"], 2);
        EXPECT_EQ(summary["failures"], 1);
        EXPECT_EQ(summary["type"], "file");
    }

    fs::remove(path);
}

TEST(FileResultSink, ConcurrentWrites) {
    std::string path = test_file();

    {
        FileResultSink sink(path);

        std::vector<std::thread> threads;
        for (int i = 0; i < 10; i++) {
            threads.emplace_back([&sink, i]() {
                ResultMessage r;
                r.work_unit_id = "wu-" + std::to_string(i);
                r.seq = i;
                r.consumer_id = "cons-001";
                r.status = (i % 3 == 0) ? "failure" : "success";
                r.result = nlohmann::json::object();
                r.timestamp = "2026-01-01T00:00:00.000Z";
                sink.on_result(r);
            });
        }
        for (auto& t : threads) t.join();

        auto summary = sink.summary();
        EXPECT_EQ(summary["total"], 10);
    }

    // Verify file has exactly 10 lines
    int lineCount = 0;
    {
        std::ifstream ifs(path);
        std::string line;
        while (std::getline(ifs, line)) {
            lineCount++;
        }
    }
    EXPECT_EQ(lineCount, 10);

    fs::remove(path);
}

TEST(FileResultSink, ShouldStop) {
    std::string path = test_file();
    {
        FileResultSink sink(path);
        EXPECT_FALSE(sink.should_stop());
    }
    fs::remove(path);
}

TEST(FileResultSink, ShouldStop_MaxFailures) {
    std::string path = test_file();
    {
        FileResultSink sink(path, 3, 0); // stop after 3 failures

        for (int i = 0; i < 2; i++) {
            ResultMessage r;
            r.work_unit_id = "wu-" + std::to_string(i);
            r.seq = i;
            r.consumer_id = "cons-001";
            r.status = "failure";
            r.result = nlohmann::json::object();
            r.timestamp = "2026-01-01T00:00:00.000Z";
            sink.on_result(r);
        }

        EXPECT_FALSE(sink.should_stop()); // 2 failures < 3 threshold

        ResultMessage r3;
        r3.work_unit_id = "wu-2";
        r3.seq = 2;
        r3.consumer_id = "cons-001";
        r3.status = "failure";
        r3.result = nlohmann::json::object();
        r3.timestamp = "2026-01-01T00:00:00.000Z";
        sink.on_result(r3);

        EXPECT_TRUE(sink.should_stop()); // 3 failures >= 3 threshold
    }
    fs::remove(path);
}

TEST(FileResultSink, ShouldStop_MaxDuration) {
    std::string path = test_file();
    {
        FileResultSink sink(path, 0, 1); // stop after 1 second
        EXPECT_FALSE(sink.should_stop());
        std::this_thread::sleep_for(std::chrono::milliseconds(1100));
        EXPECT_TRUE(sink.should_stop());
    }
    fs::remove(path);
}

TEST(FileResultSink, ShouldStop_NoCriteria) {
    std::string path = test_file();
    {
        FileResultSink sink(path, 0, 0); // no criteria

        for (int i = 0; i < 10; i++) {
            ResultMessage r;
            r.work_unit_id = "wu-" + std::to_string(i);
            r.seq = i;
            r.consumer_id = "cons-001";
            r.status = "failure";
            r.result = nlohmann::json::object();
            r.timestamp = "2026-01-01T00:00:00.000Z";
            sink.on_result(r);
        }

        EXPECT_FALSE(sink.should_stop());
    }
    fs::remove(path);
}

TEST(FileResultSink, Summary_FilePath) {
    std::string path = test_file();
    {
        FileResultSink sink(path);
        auto summary = sink.summary();
        EXPECT_EQ(summary["file"], path);
    }
    fs::remove(path);
}
