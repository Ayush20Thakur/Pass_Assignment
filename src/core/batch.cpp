#include "core/batch.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <system_error>

#include "core/threadpool.hpp"

namespace forge::core {

namespace {

BatchItemResult solve_single_problem(ThermalParameters1D params, size_t element_count) {
    params.element_count = element_count;
    auto fem_res = solve_thermal_1d(params);
    BatchItemResult item;
    if (std::holds_alternative<FemResult1D>(fem_res)) {
        const auto& r = std::get<FemResult1D>(fem_res);
        double max_t = -1e9;
        for (double t : r.temperature_c) {
            if (t > max_t) max_t = t;
        }
        item.max_temperature_c = max_t;
        const size_t mid_idx = r.temperature_c.size() / 2;
        item.midpoint_temperature_c = r.temperature_c[mid_idx];
        item.reaction_left_w = r.reaction_left_w;
        item.reaction_right_w = r.reaction_right_w;
        item.energy_balance_error_w = r.energy_balance_error_w;
    }
    return item;
}

} // namespace

std::vector<ThermalParameters1D> generate_thermal_batch(const ThermalBatchSpecification& spec) {
    std::vector<ThermalParameters1D> problems;
    problems.reserve(spec.batch_size);

    std::mt19937_64 rng(spec.seed);

    for (size_t i = 0; i < spec.batch_size; ++i) {
        ThermalParameters1D p;
        p.length_m = spec.length_m;
        p.area_m2 = spec.area_m2;
        p.thermal_conductivity_w_mk = sample_range(rng, spec.k_min, spec.k_max);
        p.volumetric_heat_w_m3 = sample_range(rng, spec.q_min, spec.q_max);
        p.temperature_left_c = sample_range(rng, spec.t_left_min, spec.t_left_max);
        p.temperature_right_c = sample_range(rng, spec.t_right_min, spec.t_right_max);
        problems.push_back(p);
    }

    return problems;
}

BatchSolveResult solve_batch_serial(const std::vector<ThermalParameters1D>& problems,
                                    size_t element_count) {
    BatchSolveResult result;
    result.items.resize(problems.size());

    const auto t_start = std::chrono::high_resolution_clock::now();

    for (size_t i = 0; i < problems.size(); ++i) {
        result.items[i] = solve_single_problem(problems[i], element_count);
    }

    const auto t_end = std::chrono::high_resolution_clock::now();
    result.elapsed_ms = std::chrono::duration<double, std::milli>(t_end - t_start).count();
    return result;
}

BatchSolveResult solve_batch_parallel(const std::vector<ThermalParameters1D>& problems,
                                      size_t element_count,
                                      size_t thread_count,
                                      size_t chunk_size) {
    if (thread_count <= 1) {
        return solve_batch_serial(problems, element_count);
    }

    BatchSolveResult result;
    result.items.resize(problems.size());

    const auto t_start = std::chrono::high_resolution_clock::now();

    ThreadPool pool(thread_count);
    pool.parallel_for(problems.size(), [&problems, element_count, &result](size_t start, size_t end) {
        for (size_t i = start; i < end; ++i) {
            result.items[i] = solve_single_problem(problems[i], element_count);
        }
    }, chunk_size);

    const auto t_end = std::chrono::high_resolution_clock::now();
    result.elapsed_ms = std::chrono::duration<double, std::milli>(t_end - t_start).count();
    return result;
}

BenchmarkSuiteResult run_benchmark_suite(
    const ThermalBatchSpecification& spec,
    const std::vector<size_t>& requested_threads,
    const std::function<void(float progress)>& progress_cb) {
    BenchmarkSuiteResult suite;
    suite.batch_size = spec.batch_size;
    suite.element_count = spec.element_count;
    suite.seed = spec.seed;

    unsigned int hc = std::thread::hardware_concurrency();
    if (hc == 0) hc = 1;
    suite.logical_cpus = hc;

    // Filter and sanitize thread counts (ensure 1 is first, capped at logical_cpus, unique)
    std::vector<size_t> threads;
    threads.push_back(1);
    for (size_t t : requested_threads) {
        if (t > 1 && t <= suite.logical_cpus) {
            if (std::find(threads.begin(), threads.end(), t) == threads.end()) {
                threads.push_back(t);
            }
        }
    }
    std::sort(threads.begin(), threads.end());

    const auto problems = generate_thermal_batch(spec);
    const size_t total_steps = threads.size() * 6; // 1 warmup + 5 repeats per thread count
    size_t completed_steps = 0;

    double t1_median_ms = 0.0;

    for (size_t t_idx = 0; t_idx < threads.size(); ++t_idx) {
        const size_t t = threads[t_idx];
        ThreadBenchmarkMetrics metrics;
        metrics.thread_count = t;

        // 1. Warm-up run (not included in measured repeats)
        auto warmup_res = solve_batch_parallel(problems, spec.element_count, t);
        metrics.warmup_ms = warmup_res.elapsed_ms;
        completed_steps++;
        if (progress_cb) progress_cb(static_cast<float>(completed_steps) / static_cast<float>(total_steps));

        // 2. 5 Measured repeats
        std::vector<double> repeats;
        repeats.reserve(5);
        for (int r = 0; r < 5; ++r) {
            auto run_res = solve_batch_parallel(problems, spec.element_count, t);
            repeats.push_back(run_res.elapsed_ms);
            metrics.repeat_times_ms.push_back(run_res.elapsed_ms);

            completed_steps++;
            if (progress_cb) progress_cb(static_cast<float>(completed_steps) / static_cast<float>(total_steps));
        }

        // 3. Compute median (sorted index 2 of 5)
        std::vector<double> sorted_repeats = repeats;
        std::sort(sorted_repeats.begin(), sorted_repeats.end());
        metrics.median_ms = sorted_repeats[2];

        if (t == 1) {
            t1_median_ms = metrics.median_ms;
            metrics.speedup = 1.0;
            metrics.efficiency = 1.0;
        } else {
            metrics.speedup = t1_median_ms / std::max(1e-6, metrics.median_ms);
            metrics.efficiency = metrics.speedup / static_cast<double>(t);
        }

        suite.results.push_back(metrics);
    }

    return suite;
}

bool export_benchmark_csv(const BenchmarkSuiteResult& suite, const std::filesystem::path& output_path) {
    std::error_code ec;
    if (output_path.has_parent_path()) {
        std::filesystem::create_directories(output_path.parent_path(), ec);
    }

    std::ofstream f(output_path, std::ios::trunc);
    if (!f.is_open()) return false;

    // Header per spec: threads,warmup_ms,repeat1_ms,repeat2_ms,repeat3_ms,repeat4_ms,repeat5_ms,median_ms,speedup,efficiency
    f << "threads,warmup_ms,repeat1_ms,repeat2_ms,repeat3_ms,repeat4_ms,repeat5_ms,median_ms,speedup,efficiency\n";

    for (const auto& m : suite.results) {
        f << m.thread_count << ","
          << m.warmup_ms << ",";
        for (size_t r = 0; r < 5; ++r) {
            if (r < m.repeat_times_ms.size()) {
                f << m.repeat_times_ms[r] << ",";
            } else {
                f << "0.0,";
            }
        }
        f << m.median_ms << ","
          << m.speedup << ","
          << m.efficiency << "\n";
    }

    return true;
}

} // namespace forge::core
