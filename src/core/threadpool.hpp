#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <utility>
#include <vector>

namespace forge::core {

class ThreadPool {
public:
    explicit ThreadPool(size_t thread_count = 0)
        : stop_(false) {
        if (thread_count == 0) {
            thread_count = std::thread::hardware_concurrency();
            if (thread_count == 0) thread_count = 1;
        }

        workers_.reserve(thread_count);
        for (size_t i = 0; i < thread_count; ++i) {
            workers_.emplace_back([this]() {
                while (true) {
                    std::function<void()> task;
                    {
                        std::unique_lock<std::mutex> lock(queue_mutex_);
                        condition_.wait(lock, [this]() {
                            return stop_ || !tasks_.empty();
                        });
                        if (stop_ && tasks_.empty()) {
                            return;
                        }
                        task = std::move(tasks_.front());
                        tasks_.pop();
                    }
                    task();
                }
            });
        }
    }

    ~ThreadPool() {
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            stop_ = true;
        }
        condition_.notify_all();
        for (std::thread& worker : workers_) {
            if (worker.joinable()) {
                worker.join();
            }
        }
    }

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
    ThreadPool(ThreadPool&&) = delete;
    ThreadPool& operator=(ThreadPool&&) = delete;

    size_t thread_count() const noexcept {
        return workers_.size();
    }

    template <class F, class... Args>
    auto enqueue(F&& f, Args&&... args)
        -> std::future<typename std::invoke_result<F, Args...>::type> {
        using return_type = typename std::invoke_result<F, Args...>::type;

        auto task = std::make_shared<std::packaged_task<return_type()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)
        );

        std::future<return_type> res = task->get_future();
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            if (stop_) {
                throw std::runtime_error("enqueue on stopped ThreadPool");
            }
            tasks_.emplace([task]() { (*task)(); });
        }
        condition_.notify_one();
        return res;
    }

    void parallel_for(size_t total_items,
                      const std::function<void(size_t start, size_t end)>& body,
                      size_t chunk_size = 16) {
        if (total_items == 0) return;

        if (workers_.empty() || workers_.size() == 1) {
            body(0, total_items);
            return;
        }

        std::atomic<size_t> next_idx{0};
        const size_t num_workers = workers_.size();
        std::vector<std::future<void>> futures;
        futures.reserve(num_workers);

        for (size_t w = 0; w < num_workers; ++w) {
            futures.push_back(enqueue([&next_idx, total_items, chunk_size, &body]() {
                while (true) {
                    const size_t start = next_idx.fetch_add(chunk_size, std::memory_order_relaxed);
                    if (start >= total_items) {
                        break;
                    }
                    const size_t end = std::min(start + chunk_size, total_items);
                    body(start, end);
                }
            }));
        }

        for (auto& fut : futures) {
            fut.get();
        }
    }

private:
    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> tasks_;
    std::mutex queue_mutex_;
    std::condition_variable condition_;
    bool stop_;
};

} // namespace forge::core
