#include "server.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

#include "../core/csv.hpp"
#include "../core/fem1d.hpp"
#include "../core/batch.hpp"
#include "../core/stl.hpp"
#include "../core/version.hpp"

namespace forge::server {

namespace {

nlohmann::json make_error_json(
    std::string_view field,
    std::string_view message,
    std::string_view hint,
    size_t line_number = 0) {
    nlohmann::json err = {
        {"field", std::string(field)},
        {"message", std::string(message)},
        {"hint", std::string(hint)}
    };
    if (line_number > 0) {
        err["line_number"] = line_number;
    }
    return err;
}

std::string extract_file_content(const httplib::Request& req, std::string_view default_field) {
    if (req.is_multipart_form_data()) {
        if (req.has_file(std::string(default_field))) {
            return req.get_file_value(std::string(default_field)).content;
        }
        for (const auto& [name, file] : req.files) {
            return file.content;
        }
    }
    if (!req.body.empty()) {
        try {
            auto body_json = nlohmann::json::parse(req.body);
            if (body_json.contains("content")) {
                return body_json["content"].get<std::string>();
            }
            if (body_json.contains("path")) {
                const std::string path = body_json["path"].get<std::string>();
                std::ifstream f(path, std::ios::binary);
                if (f.is_open()) {
                    std::ostringstream ss;
                    ss << f.rdbuf();
                    return ss.str();
                }
            }
        } catch (...) {
            // Raw text payload
            return req.body;
        }
    }
    return req.body;
}

} // namespace

ForgeServer::ForgeServer(int requested_port)
    : requested_port_(requested_port),
      http_server_(std::make_unique<httplib::Server>()),
      job_manager_(std::make_shared<JobManager>()) {
    setup_cors();
    setup_routes();
}

ForgeServer::~ForgeServer() {
    stop();
}

void ForgeServer::setup_cors() {
    http_server_->set_default_headers({
        {"Access-Control-Allow-Origin", "*"},
        {"Access-Control-Allow-Methods", "GET, POST, DELETE, OPTIONS"},
        {"Access-Control-Allow-Headers", "Content-Type, Authorization"},
        {"Cache-Control", "no-cache"}
    });

    http_server_->Options(".*", [](const httplib::Request&, httplib::Response& res) {
        res.status = 200;
    });
}

void ForgeServer::setup_routes() {
    // Mount static frontend files if dist exists
    if (std::filesystem::exists("dist")) {
        http_server_->set_mount_point("/", "dist");
    } else if (std::filesystem::exists("../dist")) {
        http_server_->set_mount_point("/", "../dist");
    }

    // 1. Health check
    http_server_->Get("/api/health", [](const httplib::Request&, httplib::Response& res) {
        nlohmann::json resp = {
            {"status", "ok"},
            {"version", std::string(forge::core::get_version_string())}
        };
        res.set_content(resp.dump(), "application/json");
    });

    // 2. Graceful shutdown
    http_server_->Post("/api/shutdown", [this](const httplib::Request&, httplib::Response& res) {
        nlohmann::json resp = {{"status", "shutting_down"}};
        res.set_content(resp.dump(), "application/json");
        std::thread([this]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            stop();
        }).detach();
    });

    // 3. Geometry ASCII STL parsing
    http_server_->Post("/api/geometry/stl", [](const httplib::Request& req, httplib::Response& res) {
        const std::string content = extract_file_content(req, "file");
        auto parse_res = forge::core::parse_stl_string(content);

        if (std::holds_alternative<forge::core::Error>(parse_res)) {
            const auto& err = std::get<forge::core::Error>(parse_res);
            res.status = 400;
            res.set_content(make_error_json("stl", err.message, err.actionable_fix, err.line_number).dump(),
                            "application/json");
            return;
        }

        const auto& mesh = std::get<forge::core::StlMesh>(parse_res);
        nlohmann::json resp = {
            {"solid_name", mesh.solid_name},
            {"triangle_count", mesh.triangle_count},
            {"degenerate_count", mesh.degenerate_count},
            {"warnings", mesh.warnings},
            {"bounding_box", {
                {"min_x", mesh.bounding_box.min_x},
                {"max_x", mesh.bounding_box.max_x},
                {"min_y", mesh.bounding_box.min_y},
                {"max_y", mesh.bounding_box.max_y},
                {"min_z", mesh.bounding_box.min_z},
                {"max_z", mesh.bounding_box.max_z},
                {"diagonal", mesh.bounding_box.diagonal_length()}
            }},
            {"vertices", mesh.vertices},
            {"normals", mesh.normals}
        };
        res.set_content(resp.dump(), "application/json");
    });

    // 4. Results Nodal CSV parsing
    http_server_->Post("/api/results/csv", [](const httplib::Request& req, httplib::Response& res) {
        const std::string content = extract_file_content(req, "file");
        auto parse_res = forge::core::parse_nodal_csv_string(content);

        if (std::holds_alternative<forge::core::Error>(parse_res)) {
            const auto& err = std::get<forge::core::Error>(parse_res);
            res.status = 400;
            res.set_content(make_error_json("csv", err.message, err.actionable_fix, err.line_number).dump(),
                            "application/json");
            return;
        }

        const auto& data = std::get<forge::core::NodalCsvData>(parse_res);
        nlohmann::json resp = {
            {"node_count", data.node_count},
            {"min_temperature_c", data.min_temperature_c},
            {"max_temperature_c", data.max_temperature_c},
            {"is_synthetic", data.is_synthetic},
            {"bounding_box", {
                {"min_x", data.bounding_box.min_x},
                {"max_x", data.bounding_box.max_x},
                {"min_y", data.bounding_box.min_y},
                {"max_y", data.bounding_box.max_y},
                {"min_z", data.bounding_box.min_z},
                {"max_z", data.bounding_box.max_z},
                {"diagonal", data.bounding_box.diagonal_length()}
            }},
            {"node_ids", data.node_ids},
            {"positions", data.positions},
            {"temperatures", data.temperatures}
        };
        res.set_content(resp.dump(), "application/json");
    });

    // 5. Thermal solve job creation
    http_server_->Post("/api/thermal/solve", [this](const httplib::Request& req, httplib::Response& res) {
        forge::core::ThermalParameters1D base_params;
        std::vector<size_t> element_counts = {10, 20, 40, 80};

        try {
            if (!req.body.empty()) {
                auto body = nlohmann::json::parse(req.body);
                if (body.contains("length_m")) base_params.length_m = body["length_m"].get<double>();
                if (body.contains("area_m2")) base_params.area_m2 = body["area_m2"].get<double>();
                if (body.contains("thermal_conductivity_w_mk")) base_params.thermal_conductivity_w_mk = body["thermal_conductivity_w_mk"].get<double>();
                if (body.contains("volumetric_heat_w_m3")) base_params.volumetric_heat_w_m3 = body["volumetric_heat_w_m3"].get<double>();
                if (body.contains("temperature_left_c")) base_params.temperature_left_c = body["temperature_left_c"].get<double>();
                if (body.contains("temperature_right_c")) base_params.temperature_right_c = body["temperature_right_c"].get<double>();
                if (body.contains("element_counts")) {
                    element_counts = body["element_counts"].get<std::vector<size_t>>();
                }
            }
        } catch (const std::exception& e) {
            res.status = 400;
            res.set_content(make_error_json("body", "Malformed JSON body", e.what()).dump(), "application/json");
            return;
        }

        const auto val = forge::core::validate_thermal_parameters(base_params);
        if (std::holds_alternative<forge::core::Error>(val)) {
            const auto& err = std::get<forge::core::Error>(val);
            res.status = 400;
            res.set_content(make_error_json("parameters", err.message, err.actionable_fix).dump(), "application/json");
            return;
        }

        if (element_counts.empty()) {
            res.status = 400;
            res.set_content(make_error_json("element_counts", "Element counts list cannot be empty", "Provide array like [10, 20, 40, 80]").dump(), "application/json");
            return;
        }

        const std::string job_id = job_manager_->create_job(
            [base_params, element_counts](std::shared_ptr<Job> job) {
                nlohmann::json meshes_json = nlohmann::json::array();
                const size_t total_steps = element_counts.size();

                for (size_t i = 0; i < total_steps; ++i) {
                    if (job->stop_token.load()) {
                        return; // Cancelled
                    }

                    forge::core::ThermalParameters1D run_params = base_params;
                    run_params.element_count = element_counts[i];
                    auto solve_res = forge::core::solve_thermal_1d(run_params);

                    if (std::holds_alternative<forge::core::Error>(solve_res)) {
                        job->state = JobState::Failed;
                        job->error = {{"message", std::get<forge::core::Error>(solve_res).message}};
                        return;
                    }

                    const auto& fem_data = std::get<forge::core::FemResult1D>(solve_res);
                    nlohmann::json mesh_entry = {
                        {"element_count", fem_data.parameters.element_count},
                        {"mesh_size_h_m", fem_data.mesh_size_h_m},
                        {"node_x_m", fem_data.node_x_m},
                        {"temperature_c", fem_data.temperature_c},
                        {"exact_temperature_c", fem_data.exact_temperature_c},
                        {"max_midpoint_error_c", fem_data.max_midpoint_error_c},
                        {"max_nodal_error_c", fem_data.max_nodal_error_c},
                        {"reaction_left_w", fem_data.reaction_left_w},
                        {"reaction_right_w", fem_data.reaction_right_w},
                        {"total_heat_generated_w", fem_data.total_heat_generated_w},
                        {"energy_balance_error_w", fem_data.energy_balance_error_w}
                    };
                    meshes_json.push_back(mesh_entry);

                    // Update atomic progress (one event per chunk/mesh)
                    job->progress = static_cast<float>(i + 1) / static_cast<float>(total_steps);
                }

                auto conv_rows = forge::core::run_convergence_study(base_params, element_counts);
                nlohmann::json conv_json = nlohmann::json::array();
                for (const auto& r : conv_rows) {
                    conv_json.push_back({
                        {"element_count", r.element_count},
                        {"mesh_size_h_m", r.mesh_size_h_m},
                        {"max_midpoint_error_c", r.max_midpoint_error_c},
                        {"observed_order", r.observed_order}
                    });
                }

                job->result = {
                    {"meshes", meshes_json},
                    {"convergence", conv_json}
                };

                // Atomic CSV export writes for convergence and results
                std::ostringstream conv_csv_ss;
                conv_csv_ss << "N,h,max_mid_error,order\n";
                for (const auto& r : conv_rows) {
                    conv_csv_ss << r.element_count << "," << r.mesh_size_h_m << ","
                                << r.max_midpoint_error_c << "," << r.observed_order << "\n";
                }
                JobManager::atomic_export_write("results/convergence.csv.tmp", "results/convergence.csv", conv_csv_ss.str());
            },
            "convergence.csv"
        );

        res.status = 202;
        res.set_content(nlohmann::json({{"job_id", job_id}, {"state", "queued"}}).dump(), "application/json");
    });

    // 5b. Benchmark suite run
    http_server_->Post("/api/benchmark/run", [this](const httplib::Request& req, httplib::Response& res) {
        forge::core::ThermalBatchSpecification spec;
        std::vector<size_t> threads = {1, 2, 4};

        try {
            if (!req.body.empty()) {
                auto body = nlohmann::json::parse(req.body);
                if (body.contains("batch_size")) spec.batch_size = body["batch_size"].get<size_t>();
                if (body.contains("element_count")) spec.element_count = body["element_count"].get<size_t>();
                if (body.contains("seed")) spec.seed = body["seed"].get<uint64_t>();
                if (body.contains("threads")) threads = body["threads"].get<std::vector<size_t>>();
            }
        } catch (const std::exception& e) {
            res.status = 400;
            res.set_content(make_error_json("body", "Malformed JSON body", e.what()).dump(), "application/json");
            return;
        }

        if (spec.batch_size == 0 || spec.batch_size > 50000) {
            res.status = 400;
            res.set_content(make_error_json("batch_size", "Batch size must be between 1 and 50000", "Provide batch_size in range [1, 50000]").dump(), "application/json");
            return;
        }

        const std::string job_id = job_manager_->create_job(
            [spec, threads](std::shared_ptr<Job> job) {
                auto suite = forge::core::run_benchmark_suite(spec, threads, [&job](float progress) {
                    job->progress = progress;
                });

                if (job->stop_token.load()) {
                    return; // Cancelled
                }

                nlohmann::json results_arr = nlohmann::json::array();
                for (const auto& m : suite.results) {
                    results_arr.push_back({
                        {"thread_count", m.thread_count},
                        {"warmup_ms", m.warmup_ms},
                        {"repeat_times_ms", m.repeat_times_ms},
                        {"median_ms", m.median_ms},
                        {"speedup", m.speedup},
                        {"efficiency", m.efficiency}
                    });
                }

                job->result = {
                    {"batch_size", suite.batch_size},
                    {"element_count", suite.element_count},
                    {"seed", suite.seed},
                    {"logical_cpus", suite.logical_cpus},
                    {"results", results_arr}
                };

                // Atomic CSV export write for results/benchmark.csv
                std::ostringstream csv_ss;
                csv_ss << "threads,warmup_ms,repeat1_ms,repeat2_ms,repeat3_ms,repeat4_ms,repeat5_ms,median_ms,speedup,efficiency\n";
                for (const auto& m : suite.results) {
                    csv_ss << m.thread_count << "," << m.warmup_ms << ",";
                    for (size_t r = 0; r < 5; ++r) {
                        if (r < m.repeat_times_ms.size()) csv_ss << m.repeat_times_ms[r] << ",";
                        else csv_ss << "0.0,";
                    }
                    csv_ss << m.median_ms << "," << m.speedup << "," << m.efficiency << "\n";
                }
                JobManager::atomic_export_write("results/benchmark.csv.tmp", "results/benchmark.csv", csv_ss.str());
            },
            "benchmark.csv"
        );

        res.status = 202;
        res.set_content(nlohmann::json({{"job_id", job_id}, {"state", "queued"}}).dump(), "application/json");
    });

    // 5c. Benchmark latest results
    http_server_->Get("/api/benchmark/results", [](const httplib::Request&, httplib::Response& res) {
        const std::string path = "results/benchmark.csv";
        std::ifstream file(path);
        if (!file.is_open()) {
            res.status = 404;
            res.set_content(make_error_json("benchmark", "No benchmark results found on disk", "Run a benchmark job first").dump(), "application/json");
            return;
        }

        std::string line;
        std::getline(file, line); // Skip header

        nlohmann::json results_arr = nlohmann::json::array();
        while (std::getline(file, line)) {
            if (line.empty()) continue;
            std::stringstream ss(line);
            std::string item;
            std::vector<std::string> tokens;
            while (std::getline(ss, item, ',')) {
                tokens.push_back(item);
            }
            if (tokens.size() >= 10) {
                results_arr.push_back({
                    {"thread_count", std::stoull(tokens[0])},
                    {"warmup_ms", std::stod(tokens[1])},
                    {"repeat_times_ms", {
                        std::stod(tokens[2]),
                        std::stod(tokens[3]),
                        std::stod(tokens[4]),
                        std::stod(tokens[5]),
                        std::stod(tokens[6])
                    }},
                    {"median_ms", std::stod(tokens[7])},
                    {"speedup", std::stod(tokens[8])},
                    {"efficiency", std::stod(tokens[9])}
                });
            }
        }

        nlohmann::json resp = {
            {"status", "ok"},
            {"logical_cpus", std::thread::hardware_concurrency()},
            {"results", results_arr}
        };
        res.set_content(resp.dump(), "application/json");
    });

    // 6. Job state and result inspection
    http_server_->Get(R"(/api/jobs/([a-zA-Z0-9_\-]+))", [this](const httplib::Request& req, httplib::Response& res) {
        const std::string job_id = req.matches[1];
        auto job = job_manager_->get_job(job_id);
        if (!job) {
            res.status = 404;
            res.set_content(make_error_json("job_id", "Job not found", "Verify job ID").dump(), "application/json");
            return;
        }

        nlohmann::json resp = {
            {"job_id", job->id},
            {"state", to_string(job->state.load())},
            {"progress", job->progress.load()},
            {"result", job->result},
            {"error", job->error}
        };
        res.set_content(resp.dump(), "application/json");
    });

    // 7. Job cancellation
    http_server_->Delete(R"(/api/jobs/([a-zA-Z0-9_\-]+))", [this](const httplib::Request& req, httplib::Response& res) {
        const std::string job_id = req.matches[1];
        if (job_manager_->cancel_job(job_id)) {
            res.status = 200;
            res.set_content(nlohmann::json({{"job_id", job_id}, {"state", "cancelled"}}).dump(), "application/json");
        } else {
            res.status = 404;
            res.set_content(make_error_json("job_id", "Job not found", "Verify job ID").dump(), "application/json");
        }
    });

    // 8. SSE Progress endpoint
    http_server_->Get(R"(/api/jobs/([a-zA-Z0-9_\-]+)/events)", [this](const httplib::Request& req, httplib::Response& res) {
        const std::string job_id = req.matches[1];
        auto job = job_manager_->get_job(job_id);
        if (!job) {
            res.status = 404;
            return;
        }

        res.set_chunked_content_provider(
            "text/event-stream",
            [job](size_t /*offset*/, httplib::DataSink& sink) {
                while (!job->stop_token.load()) {
                    const auto state = job->state.load();
                    const float prog = job->progress.load();

                    nlohmann::json event_data = {
                        {"job_id", job->id},
                        {"state", to_string(state)},
                        {"progress", prog}
                    };
                    std::string payload = "data: " + event_data.dump() + "\n\n";
                    if (!sink.write(payload.data(), payload.size())) {
                        break;
                    }

                    if (state == JobState::Done || state == JobState::Failed || state == JobState::Cancelled) {
                        sink.done();
                        break;
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                }
                return true;
            }
        );
    });

    // 9. Atomic export download
    http_server_->Get(R"(/api/export/([a-zA-Z0-9_\-\.]+))", [](const httplib::Request& req, httplib::Response& res) {
        const std::string filename = req.matches[1];
        // Security check: prevent directory traversal
        if (filename.find("..") != std::string::npos || filename.find('/') != std::string::npos || filename.find('\\') != std::string::npos) {
            res.status = 400;
            res.set_content("Invalid file name", "text/plain");
            return;
        }

        const std::string path = "results/" + filename;
        std::ifstream file(path, std::ios::binary);
        if (!file.is_open()) {
            res.status = 404;
            res.set_content("Export file not found", "text/plain");
            return;
        }

        std::ostringstream ss;
        ss << file.rdbuf();
        res.set_content(ss.str(), "text/csv");
    });

    // 10. Hardware devices stub (SIMULATED flag per Rule 3)
    http_server_->Post(R"(/api/devices/([a-zA-Z0-9_]+)/([a-zA-Z0-9_]+))", [](const httplib::Request& req, httplib::Response& res) {
        const std::string dev_type = req.matches[1];
        const std::string action = req.matches[2];
        nlohmann::json resp = {
            {"device", dev_type},
            {"action", action},
            {"simulated", true},
            {"status", "ok"}
        };
        res.set_content(resp.dump(), "application/json");
    });
}

bool ForgeServer::start(bool non_blocking) {
    if (is_running_) {
        return true;
    }

    // Bind to 127.0.0.1 on ephemeral free port or requested port
    if (requested_port_ == 0) {
        actual_port_ = http_server_->bind_to_any_port("127.0.0.1");
    } else {
        if (http_server_->bind_to_port("127.0.0.1", requested_port_)) {
            actual_port_ = requested_port_;
        } else {
            actual_port_ = -1;
        }
    }

    if (actual_port_ <= 0) {
        return false;
    }

    is_running_ = true;

    if (non_blocking) {
        server_thread_ = std::thread([this]() {
            http_server_->listen_after_bind();
            is_running_ = false;
        });
        http_server_->wait_until_ready();
    } else {
        http_server_->listen_after_bind();
        is_running_ = false;
    }

    return true;
}

void ForgeServer::stop() {
    if (!is_running_) {
        return;
    }
    is_running_ = false;
    if (job_manager_) {
        job_manager_->shutdown();
    }
    if (http_server_) {
        http_server_->stop();
    }
    if (server_thread_.joinable()) {
        server_thread_.join();
    }
}

void ForgeServer::wait() {
    if (server_thread_.joinable()) {
        server_thread_.join();
    }
}

} // namespace forge::server
