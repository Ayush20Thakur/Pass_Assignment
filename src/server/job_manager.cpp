#include "job_manager.hpp"

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace forge::server {

std::string to_string(JobState state) {
    switch (state) {
    case JobState::Queued:
        return "queued";
    case JobState::Running:
        return "running";
    case JobState::Done:
        return "done";
    case JobState::Failed:
        return "failed";
    case JobState::Cancelled:
        return "cancelled";
    }
    return "unknown";
}

JobManager::JobManager() = default;

JobManager::~JobManager() {
    shutdown();
}

void JobManager::shutdown() {
    shutting_down_ = true;
    {
        std::lock_guard<std::mutex> lock(jobs_mutex_);
        for (auto& [id, job] : jobs_) {
            job->stop_token = true;
            if (job->state == JobState::Running || job->state == JobState::Queued) {
                job->state = JobState::Cancelled;
            }
            if (!job->temp_file_path.empty()) {
                std::error_code ec;
                std::filesystem::remove(job->temp_file_path, ec);
            }
        }
    }
    for (auto& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    workers_.clear();
}

std::string JobManager::create_job(
    std::function<void(std::shared_ptr<Job>)> work_function,
    std::string export_file_name) {
    auto job = std::make_shared<Job>();
    const uint64_t count = job_counter_.fetch_add(1);

    std::ostringstream ss;
    ss << "job-" << std::setfill('0') << std::setw(6) << count;
    job->id = ss.str();
    job->created_at = std::chrono::steady_clock::now();

    if (!export_file_name.empty()) {
        job->export_file_path = "results/" + export_file_name;
        job->temp_file_path = "results/" + export_file_name + ".tmp";
    }

    {
        std::lock_guard<std::mutex> lock(jobs_mutex_);
        jobs_[job->id] = job;
    }

    workers_.emplace_back([job, work_function]() {
        job->state = JobState::Running;
        try {
            work_function(job);
            if (job->stop_token.load()) {
                job->state = JobState::Cancelled;
                if (!job->temp_file_path.empty()) {
                    std::error_code ec;
                    std::filesystem::remove(job->temp_file_path, ec);
                }
            } else if (job->state == JobState::Running) {
                job->state = JobState::Done;
                job->progress = 1.0f;
            }
        } catch (const std::exception& e) {
            job->state = JobState::Failed;
            job->error = {{"message", e.what()}};
            if (!job->temp_file_path.empty()) {
                std::error_code ec;
                std::filesystem::remove(job->temp_file_path, ec);
            }
        }
        job->completed_at = std::chrono::steady_clock::now();
    });

    return job->id;
}

std::shared_ptr<Job> JobManager::get_job(const std::string& job_id) {
    std::lock_guard<std::mutex> lock(jobs_mutex_);
    auto it = jobs_.find(job_id);
    if (it != jobs_.end()) {
        return it->second;
    }
    return nullptr;
}

bool JobManager::cancel_job(const std::string& job_id) {
    std::lock_guard<std::mutex> lock(jobs_mutex_);
    auto it = jobs_.find(job_id);
    if (it == jobs_.end()) {
        return false;
    }

    auto job = it->second;
    job->stop_token = true;
    job->state = JobState::Cancelled;

    // Discard any partial export file immediately
    if (!job->temp_file_path.empty()) {
        std::error_code ec;
        std::filesystem::remove(job->temp_file_path, ec);
    }

    return true;
}

bool JobManager::atomic_export_write(
    const std::string& temp_path,
    const std::string& final_path,
    const std::string& content) {
    std::error_code ec;
    const std::filesystem::path temp_file(temp_path);
    const std::filesystem::path final_file(final_path);

    if (final_file.has_parent_path()) {
        std::filesystem::create_directories(final_file.parent_path(), ec);
    }

    std::ofstream out(temp_file, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        return false;
    }
    out.write(content.data(), static_cast<std::streamsize>(content.size()));
    out.close();

    // Atomic rename from temp to final
    std::filesystem::rename(temp_file, final_file, ec);
    return !ec;
}

} // namespace forge::server
