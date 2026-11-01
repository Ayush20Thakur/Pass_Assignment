#pragma once

#include <memory>
#include <string>
#include <thread>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#endif

#include <httplib.h>
#include <nlohmann/json.hpp>

#include "job_manager.hpp"
#include "../core/devices.hpp"

namespace forge::server {

class ForgeServer {
public:
    explicit ForgeServer(int requested_port = 0);
    ~ForgeServer();

    ForgeServer(const ForgeServer&) = delete;
    ForgeServer& operator=(const ForgeServer&) = delete;

    // Starts HTTP server on 127.0.0.1. If non-blocking, spawns background thread.
    bool start(bool non_blocking = true);

    // Stops the HTTP server and terminates background worker threads.
    void stop();

    // Returns the port the server is actively listening on.
    [[nodiscard]] int get_port() const noexcept { return actual_port_; }

    // Returns true if server is currently running.
    [[nodiscard]] bool is_running() const noexcept { return is_running_; }

    // Waits for the background server thread to finish if running non-blocking.
    void wait();

private:
    void setup_routes();
    void setup_cors();

    int requested_port_{0};
    int actual_port_{0};
    bool is_running_{false};

    std::unique_ptr<httplib::Server> http_server_;
    std::shared_ptr<JobManager> job_manager_;
    std::shared_ptr<forge::core::DeviceSubsystem> device_subsystem_;
    std::thread server_thread_;
};

} // namespace forge::server
