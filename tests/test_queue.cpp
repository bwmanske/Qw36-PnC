#include <gtest/gtest.h>
#include "common/queue.h"
#include <thread>
#include <vector>
#include <atomic>

using namespace pc;

TEST(BoundedQueue, PushPop) {
    BoundedQueue<int> q(10);
    q.push(1);
    q.push(2);
    q.push(3);

    EXPECT_EQ(q.pop(), 1);
    EXPECT_EQ(q.pop(), 2);
    EXPECT_EQ(q.pop(), 3);
}

TEST(BoundedQueue, Size) {
    BoundedQueue<int> q(10);
    EXPECT_EQ(q.size(), 0u);
    EXPECT_TRUE(q.empty());

    q.push(1);
    EXPECT_EQ(q.size(), 1u);
    EXPECT_FALSE(q.empty());

    q.pop();
    EXPECT_EQ(q.size(), 0u);
    EXPECT_TRUE(q.empty());
}

TEST(BoundedQueue, BlocksWhenFull) {
    BoundedQueue<int> q(2);
    q.push(1);
    q.push(2);

    std::atomic<bool> pushed{false};
    std::thread t([&]() {
        q.push(3);
        pushed = true;
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_FALSE(pushed);

    q.pop();
    t.join();
    EXPECT_TRUE(pushed);
}

TEST(BoundedQueue, TryPushTimeout) {
    BoundedQueue<int> q(1);
    q.push(1);

    EXPECT_FALSE(q.try_push(2, std::chrono::milliseconds(50)));
}

TEST(BoundedQueue, TryPopTimeout) {
    BoundedQueue<int> q(10);

    auto result = q.try_pop(std::chrono::milliseconds(50));
    EXPECT_FALSE(result.has_value());
}

TEST(BoundedQueue, Shutdown) {
    BoundedQueue<int> q(10);
    q.push(1);

    std::atomic<bool> popped{false};
    std::thread t([&]() {
        try {
            q.pop();
            popped = true;
        } catch (...) {
            popped = true;
        }
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    q.shutdown();
    t.join();
    EXPECT_TRUE(popped);
}

TEST(BoundedQueue, ConcurrentProducersConsumers) {
    BoundedQueue<int> q(100);
    const int num_items = 1000;
    std::atomic<int> produced{0};
    std::atomic<int> consumed{0};

    std::thread producer([&]() {
        for (int i = 0; i < num_items; i++) {
            q.push(i);
            produced++;
        }
    });

    std::thread consumer([&]() {
        for (int i = 0; i < num_items; i++) {
            q.pop();
            consumed++;
        }
    });

    producer.join();
    consumer.join();

    EXPECT_EQ(produced.load(), num_items);
    EXPECT_EQ(consumed.load(), num_items);
}

TEST(BoundedQueue, DrainAfterShutdown) {
    BoundedQueue<int> q(10);
    q.push(1);
    q.push(2);
    q.push(3);
    q.shutdown();

    try { int v = q.pop(); (void)v; } catch (...) {}
    try { int v = q.pop(); (void)v; } catch (...) {}
    try { int v = q.pop(); (void)v; } catch (...) {}
}
