#include <chrono>
#include <filesystem>
#include <thread>
#include <doctest/doctest.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#endif

#include <httplib.h>
#include <nlohmann/json.hpp>

#include "server/server.hpp"

using namespace forge::server;

TEST_CASE("Server Integration: Health, Bad STL, Thermal Solve, and Export") {
    // 1. Start server on random ephemeral free port (port 0)
    ForgeServer server(0);
    REQUIRE(server.start(true));

    const int port = server.get_port();
    REQUIRE(port > 0);

    httplib::Client client("127.0.0.1", port);
    client.set_connection_timeout(std::chrono::seconds(5));
    client.set_read_timeout(std::chrono::seconds(5));

    // 2. Health check
    auto health_res = client.Get("/api/health");
    REQUIRE(health_res != nullptr);
    CHECK(health_res->status == 200);

    auto health_json = nlohmann::json::parse(health_res->body);
    CHECK(health_json["status"] == "ok");
    CHECK(health_json["version"] == "1.0.0");

    // 3. Bad STL check (must return HTTP 400 with line number)
    auto bad_stl_res = client.Post("/api/geometry/stl", "not a solid\nsecond line", "text/plain");
    REQUIRE(bad_stl_res != nullptr);
    CHECK(bad_stl_res->status == 400);

    auto bad_stl_json = nlohmann::json::parse(bad_stl_res->body);
    CHECK(bad_stl_json["field"] == "stl");
    CHECK(bad_stl_json.contains("line_number"));
    CHECK(bad_stl_json["line_number"] == 1);
    CHECK(bad_stl_json.contains("message"));
    CHECK(bad_stl_json.contains("hint"));

    // 4. Valid STL check
    std::string cube_stl = "solid cube\n"
                           "  facet normal 0 0 1\n"
                           "    outer loop\n"
                           "      vertex 0 0 0\n"
                           "      vertex 1 0 0\n"
                           "      vertex 0 1 0\n"
                           "    endloop\n"
                           "  endfacet\n"
                           "endsolid cube\n";
    auto good_stl_res = client.Post("/api/geometry/stl", cube_stl, "text/plain");
    REQUIRE(good_stl_res != nullptr);
    CHECK(good_stl_res->status == 200);
    auto good_stl_json = nlohmann::json::parse(good_stl_res->body);
    CHECK(good_stl_json["triangle_count"] == 1);

    // 5. Thermal Solve Job Submission
    nlohmann::json solve_req = {
        {"length_m", 1.0},
        {"area_m2", 1e-4},
        {"thermal_conductivity_w_mk", 10.0},
        {"volumetric_heat_w_m3", 10000.0},
        {"temperature_left_c", 100.0},
        {"temperature_right_c", 20.0},
        {"element_counts", {10, 20}}
    };

    auto solve_post_res = client.Post("/api/thermal/solve", solve_req.dump(), "application/json");
    REQUIRE(solve_post_res != nullptr);
    CHECK(solve_post_res->status == 202);

    auto solve_post_json = nlohmann::json::parse(solve_post_res->body);
    REQUIRE(solve_post_json.contains("job_id"));
    const std::string job_id = solve_post_json["job_id"];

    // Poll until complete
    bool completed = false;
    for (int retry = 0; retry < 50; ++retry) {
        auto job_res = client.Get("/api/jobs/" + job_id);
        REQUIRE(job_res != nullptr);
        auto job_json = nlohmann::json::parse(job_res->body);
        const std::string state = job_json["state"];
        if (state == "done") {
            completed = true;
            CHECK(job_json["progress"] == 1.0f);
            CHECK(job_json["result"].contains("meshes"));
            CHECK(job_json["result"]["meshes"].size() == 2);
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    CHECK(completed);

    // 6. Export file download check
    auto export_res = client.Get("/api/export/convergence.csv");
    REQUIRE(export_res != nullptr);
    CHECK(export_res->status == 200);
    CHECK(export_res->body.find("N,h,max_mid_error,order") != std::string::npos);

    server.stop();
}

TEST_CASE("Server Integration: Job Cancellation within 500 ms and No Partial Export") {
    ForgeServer server(0);
    REQUIRE(server.start(true));

    const int port = server.get_port();
    httplib::Client client("127.0.0.1", port);

    // Submit a large thermal solve job with many element configurations
    std::vector<size_t> large_element_counts;
    for (size_t n = 100; n <= 10000; n += 100) {
        large_element_counts.push_back(n);
    }

    nlohmann::json solve_req = {
        {"length_m", 1.0},
        {"area_m2", 1e-4},
        {"thermal_conductivity_w_mk", 10.0},
        {"volumetric_heat_w_m3", 10000.0},
        {"temperature_left_c", 100.0},
        {"temperature_right_c", 20.0},
        {"element_counts", large_element_counts}
    };

    auto post_res = client.Post("/api/thermal/solve", solve_req.dump(), "application/json");
    REQUIRE(post_res != nullptr);
    REQUIRE(post_res->status == 202);

    auto post_json = nlohmann::json::parse(post_res->body);
    const std::string job_id = post_json["job_id"];

    // Measure cancellation response time: must be under 500 ms
    const auto cancel_start = std::chrono::high_resolution_clock::now();
    auto cancel_res = client.Delete("/api/jobs/" + job_id);
    const auto cancel_end = std::chrono::high_resolution_clock::now();

    REQUIRE(cancel_res != nullptr);
    CHECK(cancel_res->status == 200);
    auto cancel_json = nlohmann::json::parse(cancel_res->body);
    CHECK(cancel_json["state"] == "cancelled");

    const double cancel_ms = std::chrono::duration<double, std::milli>(cancel_end - cancel_start).count();
    CHECK(cancel_ms < 500.0);

    // Verify job status reports cancelled
    auto poll_res = client.Get("/api/jobs/" + job_id);
    REQUIRE(poll_res != nullptr);
    auto poll_json = nlohmann::json::parse(poll_res->body);
    CHECK(poll_json["state"] == "cancelled");

    // Acceptance requirement: leaves no partial export (.tmp) file
    CHECK(!std::filesystem::exists("results/convergence.csv.tmp"));

    server.stop();
}

TEST_CASE("Server Integration: Graceful shutdown endpoint") {
    ForgeServer server(0);
    REQUIRE(server.start(true));

    const int port = server.get_port();
    httplib::Client client("127.0.0.1", port);

    auto shutdown_res = client.Post("/api/shutdown", "{}", "application/json");
    REQUIRE(shutdown_res != nullptr);
    CHECK(shutdown_res->status == 200);
    auto shutdown_json = nlohmann::json::parse(shutdown_res->body);
    CHECK(shutdown_json["status"] == "shutting_down");

    // Wait briefly for server shutdown
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    CHECK(!server.is_running());
}

TEST_CASE("Server Integration: Benchmark run and results endpoints") {
    ForgeServer server(0);
    REQUIRE(server.start(true));

    const int port = server.get_port();
    httplib::Client client("127.0.0.1", port);
    client.set_connection_timeout(std::chrono::seconds(5));
    client.set_read_timeout(std::chrono::seconds(5));

    nlohmann::json run_payload = {
        {"batch_size", 50},
        {"element_count", 10},
        {"seed", 42},
        {"threads", {1, 2}}
    };

    auto run_res = client.Post("/api/benchmark/run", run_payload.dump(), "application/json");
    REQUIRE(run_res != nullptr);
    CHECK(run_res->status == 202);

    auto run_json = nlohmann::json::parse(run_res->body);
    CHECK(run_json.contains("job_id"));
    CHECK(run_json["state"] == "queued");
    const std::string job_id = run_json["job_id"];

    // Poll until completed
    for (int iter = 0; iter < 100; ++iter) {
        auto poll_res = client.Get("/api/jobs/" + job_id);
        REQUIRE(poll_res != nullptr);
        auto poll_json = nlohmann::json::parse(poll_res->body);
        if (poll_json["state"] == "done") {
            CHECK(poll_json.contains("result"));
            CHECK(poll_json["result"]["results"].size() >= 1);
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    // Verify GET /api/benchmark/results
    auto get_res = client.Get("/api/benchmark/results");
    REQUIRE(get_res != nullptr);
    CHECK(get_res->status == 200);
    auto get_json = nlohmann::json::parse(get_res->body);
    CHECK(get_json["status"] == "ok");
    CHECK(get_json["results"].size() >= 1);

    // Verify GET /api/export/benchmark.csv
    auto exp_res = client.Get("/api/export/benchmark.csv");
    REQUIRE(exp_res != nullptr);
    CHECK(exp_res->status == 200);
    CHECK(exp_res->body.find("threads,warmup_ms") != std::string::npos);

    server.stop();
}
