#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <random>
#include <string>
#include <vector>

#include "core/fem1d.hpp"

namespace forge::core {

// Seeded portable uniform [0, 1) random generator: (r >> 11) * 2^-53
inline double sample_uniform_01(std::mt19937_64& rng) noexcept {
    constexpr double scale = 1.0 / 9007199254740992.0; // 2^-53
    return static_cast<double>(rng() >> 11) * scale;
}

inline double sample_range(std::mt19937_64& rng, double min_val, double max_val) noexcept {
    return min_val + sample_uniform_01(rng) * (max_val - min_val);
}

struct ThermalBatchSpecification {
    size_t batch_size{1000};
    size_t element_count{50};
    uint64_t seed{42};
    double length_m{1.0};
    double area_m2{1e-4};
    double k_min{10.0};
    double k_max{50.0};
    double q_min{1000.0};
    double q_max{20000.0};
    double t_left_min{50.0};
    double t_left_max{150.0};
    double t_right_min{10.0};
    double t_right_max{30.0};
};

struct BatchItemResult {
    double max_temperature_c{0.0};
    double midpoint_temperature_c{0.0};
    double reaction_left_w{0.0};
    double reaction_right_w{0.0};
    double energy_balance_error_w{0.0};
};

struct BatchSolveResult {
    std::vector<BatchItemResult> items;
    double elapsed_ms{0.0};
};

// Generates M deterministic problems from specification
std::vector<ThermalParameters1D> generate_thermal_batch(const ThermalBatchSpecification& spec);

// Solves sequentially (1 thread)
BatchSolveResult solve_batch_serial(const std::vector<ThermalParameters1D>& problems,
                                    size_t element_count);

// Solves in parallel with bounded ThreadPool and atomic chunking
BatchSolveResult solve_batch_parallel(const std::vector<ThermalParameters1D>& problems,
                                      size_t element_count,
                                      size_t thread_count,
                                      size_t chunk_size = 16);

// Benchmark harness structures
struct ThreadBenchmarkMetrics {
    size_t thread_count{1};
    double warmup_ms{0.0};
    std::vector<double> repeat_times_ms; // 5 repeats
    double median_ms{0.0};
    double speedup{1.0};
    double efficiency{1.0};
};

struct BenchmarkSuiteResult {
    size_t batch_size{0};
    size_t element_count{0};
    uint64_t seed{0};
    size_t logical_cpus{0};
    std::vector<ThreadBenchmarkMetrics> results;
};

// Runs the benchmark harness: 1 warm-up, 5 repeats, median, speedup, efficiency for requested thread counts
BenchmarkSuiteResult run_benchmark_suite(
    const ThermalBatchSpecification& spec,
    const std::vector<size_t>& requested_threads = {1, 2, 4},
    const std::function<void(float progress)>& progress_cb = nullptr);

// Exporter: writes results/benchmark.csv
bool export_benchmark_csv(const BenchmarkSuiteResult& suite, const std::filesystem::path& output_path);

} // namespace forge::core

