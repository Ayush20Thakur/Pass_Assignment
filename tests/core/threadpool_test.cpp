#include <atomic>
#include <numeric>
#include <vector>
#include <doctest/doctest.h>

#include "core/threadpool.hpp"

using namespace forge::core;

TEST_CASE("ThreadPool: Basic enqueue and task execution") {
    ThreadPool pool(4);
    CHECK(pool.thread_count() == 4);

    std::atomic<int> counter{0};
    constexpr int total_tasks = 100;
    std::vector<std::future<void>> futures;
    futures.reserve(total_tasks);

    for (int i = 0; i < total_tasks; ++i) {
        futures.push_back(pool.enqueue([&counter]() {
            counter.fetch_add(1, std::memory_order_relaxed);
        }));
    }

    for (auto& fut : futures) {
        fut.get();
    }

    CHECK(counter.load() == total_tasks);
}

TEST_CASE("ThreadPool: parallel_for with chunked atomic distribution") {
    ThreadPool pool(4);

    constexpr size_t array_size = 10000;
    std::vector<int64_t> data(array_size, 0);

    pool.parallel_for(array_size, [&data](size_t start, size_t end) {
        for (size_t i = start; i < end; ++i) {
            data[i] = static_cast<int64_t>(i);
        }
    }, 16);

    // Verify every element was updated correctly
    bool all_correct = true;
    int64_t sum = 0;
    for (size_t i = 0; i < array_size; ++i) {
        if (data[i] != static_cast<int64_t>(i)) {
            all_correct = false;
            break;
        }
        sum += data[i];
    }

    CHECK(all_correct);
    const int64_t expected_sum = static_cast<int64_t>(array_size - 1) * static_cast<int64_t>(array_size) / 2;
    CHECK(sum == expected_sum);
}

TEST_CASE("ThreadPool: Zero tasks and single thread handling") {
    ThreadPool pool(1);
    CHECK(pool.thread_count() == 1);

    bool called = false;
    pool.parallel_for(0, [&called](size_t, size_t) {
        called = true;
    });
    CHECK(!called);

    int count = 0;
    pool.parallel_for(5, [&count](size_t start, size_t end) {
        count += static_cast<int>(end - start);
    });
    CHECK(count == 5);
}
