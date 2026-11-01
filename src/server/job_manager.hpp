#pragma once

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

namespace forge::server {

enum class JobState {
    Queued,
    Running,
    Done,
    Failed,
    Cancelled
};

[[nodiscard]] std::string to_string(JobState state);

struct Job {
    std::string id;
    std::atomic<JobState> state{JobState::Queued};
    std::atomic<float> progress{0.0f};
    std::atomic<bool> stop_token{false};
    std::string status_message;

    nlohmann::json result;
    nlohmann::json error;
    std::string export_file_path;
    std::string temp_file_path;

    std::chrono::steady_clock::time_point created_at;
    std::chrono::steady_clock::time_point completed_at;
};

class JobManager {
public:
    JobManager();
    ~JobManager();

    // Disallow copy/move to protect concurrency primitives
    JobManager(const JobManager&) = delete;
    JobManager& operator=(const JobManager&) = delete;

    // Creates a new job and submits work to background execution
    std::string create_job(
        std::function<void(std::shared_ptr<Job>)> work_function,
        std::string export_file_name = "");

    // Retrieves job pointer by ID
    [[nodiscard]] std::shared_ptr<Job> get_job(const std::string& job_id);

    // Cancels a job and cleans up any partial output
    bool cancel_job(const std::string& job_id);

    // Writes data atomically to final path using temporary file and rename
    static bool atomic_export_write(
        const std::string& temp_path,
        const std::string& final_path,
        const std::string& content);

    // Stops all jobs and waits for background workers
    void shutdown();

private:
    std::mutex jobs_mutex_;
    std::unordered_map<std::string, std::shared_ptr<Job>> jobs_;
    std::vector<std::thread> workers_;
    std::atomic<bool> shutting_down_{false};
    std::atomic<uint64_t> job_counter_{1};
};

} // namespace forge::server
