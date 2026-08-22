#include <gtest/gtest.h>
#include "consumer/thread_pool.h"
#include <atomic>
#include <chrono>
#include <mutex>
#include <vector>

using namespace pc;

namespace {

class TestHandler : public IWorkUnitHandler {
public:
    explicit TestHandler(bool fail) : fail_(fail) {}

    std::string type() const override { return "TEST"; }

    ResultMessage handle(const WorkUnitMessage& work) override {
        ResultMessage r;
        r.work_unit_id = work.work_unit_id;
        r.seq = work.seq;
        r.consumer_id = "cons-test";
        r.status = fail_ ? "failure" : "success";
        r.result = nlohmann::json::object();
        r.timestamp = "2026-01-01T00:00:00.000Z";
        return r;
    }

private:
    bool fail_;
};

class SlowHandler : public IWorkUnitHandler {
public:
    std::atomic<bool> in_handle_{false};

    std::string type() const override { return "SLOW"; }

    ResultMessage handle(const WorkUnitMessage& work) override {
        in_handle_ = true;
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        in_handle_ = false;
        ResultMessage r;
        r.work_unit_id = work.work_unit_id;
        r.seq = work.seq;
        r.consumer_id = "cons-test";
        r.status = "success";
        r.result = nlohmann::json::object();
        r.timestamp = "2026-01-01T00:00:00.000Z";
        return r;
    }
};

WorkUnitMessage make_work(int i) {
    WorkUnitMessage w;
    w.work_unit_id = "wu-" + std::to_string(i);
    w.seq = i;
    w.job = nlohmann::json::object();
    return w;
}

} // namespace

TEST(ThreadPool, SubmitWithHandler_Completes) {
    ThreadPool pool(2);
    pool.set_handler(std::make_shared<TestHandler>(false));

    std::mutex mtx;
    std::vector<std::string> results;
    pool.set_result_callback([&](const ResultMessage& r) {
        std::lock_guard<std::mutex> lock(mtx);
        results.push_back(r.work_unit_id);
    });

    pool.start();
    for (int i = 0; i < 10; i++) pool.submit(make_work(i));

    for (int i = 0; i < 200 && pool.total_completed() < 10; i++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    EXPECT_EQ(pool.total_completed(), 10u);
    EXPECT_EQ(pool.total_failed(), 0u);
    {
        std::lock_guard<std::mutex> lock(mtx);
        EXPECT_EQ(results.size(), 10u);
    }

    pool.shutdown();
}

TEST(ThreadPool, FailingHandler_CountsFailures) {
    ThreadPool pool(2);
    pool.set_handler(std::make_shared<TestHandler>(true));

    std::atomic<int> result_count{0};
    pool.set_result_callback([&](const ResultMessage& r) {
        EXPECT_EQ(r.status, "failure");
        result_count++;
    });

    pool.start();
    for (int i = 0; i < 5; i++) pool.submit(make_work(i));

    for (int i = 0; i < 200 && pool.total_failed() < 5; i++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    EXPECT_EQ(pool.total_failed(), 5u);
    EXPECT_EQ(pool.total_completed(), 0u);
    EXPECT_EQ(result_count.load(), 5);

    pool.shutdown();
}

TEST(ThreadPool, NoHandler_FailsWithNoHandlerError) {
    ThreadPool pool(1);

    std::atomic<int> result_count{0};
    pool.set_result_callback([&](const ResultMessage& r) {
        EXPECT_EQ(r.status, "failure");
        EXPECT_EQ(r.result.value("error", ""), "no handler registered");
        result_count++;
    });

    pool.start();
    pool.submit(make_work(0));

    for (int i = 0; i < 200 && result_count.load() < 1; i++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    EXPECT_EQ(result_count.load(), 1);
    EXPECT_EQ(pool.total_failed(), 1u);

    pool.shutdown();
}

TEST(ThreadPool, IdleCallback_Invoked) {
    ThreadPool pool(2);
    pool.set_handler(std::make_shared<TestHandler>(false));

    std::atomic<int> idle_calls{0};
    std::atomic<size_t> last_idle{0};
    pool.set_idle_callback([&](size_t idle) {
        idle_calls++;
        last_idle = idle;
    });

    pool.start();
    pool.submit(make_work(0));

    for (int i = 0; i < 200 && pool.total_completed() < 1; i++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    EXPECT_EQ(pool.total_completed(), 1u);
    EXPECT_GT(idle_calls.load(), 0);
    EXPECT_GT(last_idle.load(), 0u);

    pool.shutdown();
}

TEST(ThreadPool, DrainPending_ReturnsQueuedWork) {
    ThreadPool pool(1);
    pool.set_handler(std::make_shared<TestHandler>(false));

    // Not started — submitted work stays in the queue
    for (int i = 0; i < 3; i++) pool.submit(make_work(i));

    auto pending = pool.drain_pending();
    EXPECT_EQ(pending.size(), 3u);
    for (size_t i = 0; i < pending.size(); i++) {
        EXPECT_EQ(pending[i].work_unit_id, "wu-" + std::to_string(i));
    }

    pool.shutdown();
}

TEST(ThreadPool, DrainPending_IncludesActiveWork) {
    auto handler = std::make_shared<SlowHandler>();
    ThreadPool pool(1);
    pool.set_handler(handler);

    pool.start();
    pool.submit(make_work(42));

    // Wait until the worker picks up the work unit
    for (int i = 0; i < 200 && !handler->in_handle_; i++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    ASSERT_TRUE(handler->in_handle_);

    auto pending = pool.drain_pending();
    ASSERT_EQ(pending.size(), 1u);
    EXPECT_EQ(pending[0].work_unit_id, "wu-42");

    pool.shutdown();
}

TEST(ThreadPool, Shutdown_JoinsAndIsIdempotent) {
    ThreadPool pool(2);
    pool.set_handler(std::make_shared<TestHandler>(false));

    pool.start();
    for (int i = 0; i < 4; i++) pool.submit(make_work(i));

    pool.shutdown();
    pool.shutdown();

    EXPECT_EQ(pool.total_completed() + pool.total_failed(), 4u);
}
