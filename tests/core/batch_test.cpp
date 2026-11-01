#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <doctest/doctest.h>

#include "core/batch.hpp"

using namespace forge::core;

TEST_CASE("PRNG: Seeded generator is strictly deterministic across calls") {
    std::mt19937_64 rng1(42);
    std::mt19937_64 rng2(42);

    for (int i = 0; i < 100; ++i) {
        const double u1 = sample_uniform_01(rng1);
        const double u2 = sample_uniform_01(rng2);
        CHECK(u1 == u2);
        CHECK(u1 >= 0.0);
        CHECK(u1 < 1.0);
    }

    std::mt19937_64 rng3(42);
    const double val1 = sample_range(rng3, 10.0, 50.0);
    std::mt19937_64 rng4(42);
    const double val2 = sample_range(rng4, 10.0, 50.0);
    CHECK(val1 == val2);
    CHECK(val1 >= 10.0);
    CHECK(val1 <= 50.0);
}

TEST_CASE("Batch Solver: Parallel solve matches serial solve to <= 1e-8 degC") {
    ThermalBatchSpecification spec;
    spec.batch_size = 200;
    spec.element_count = 30;
    spec.seed = 12345;

    auto problems = generate_thermal_batch(spec);
    REQUIRE(problems.size() == 200);

    auto serial_res = solve_batch_serial(problems, spec.element_count);
    REQUIRE(serial_res.items.size() == 200);

    // Test with 2 threads
    auto parallel_res_2 = solve_batch_parallel(problems, spec.element_count, 2, 16);
    REQUIRE(parallel_res_2.items.size() == 200);

    // Test with 4 threads
    auto parallel_res_4 = solve_batch_parallel(problems, spec.element_count, 4, 16);
    REQUIRE(parallel_res_4.items.size() == 200);

    for (size_t i = 0; i < problems.size(); ++i) {
        // Determinism requirement: parallel output must equal serial to <= 1e-8
        const double diff_max_2 = std::abs(serial_res.items[i].max_temperature_c - parallel_res_2.items[i].max_temperature_c);
        const double diff_mid_2 = std::abs(serial_res.items[i].midpoint_temperature_c - parallel_res_2.items[i].midpoint_temperature_c);
        const double diff_err_2 = std::abs(serial_res.items[i].energy_balance_error_w - parallel_res_2.items[i].energy_balance_error_w);

        CHECK(diff_max_2 <= 1e-8);
        CHECK(diff_mid_2 <= 1e-8);
        CHECK(diff_err_2 <= 1e-8);

        const double diff_max_4 = std::abs(serial_res.items[i].max_temperature_c - parallel_res_4.items[i].max_temperature_c);
        const double diff_mid_4 = std::abs(serial_res.items[i].midpoint_temperature_c - parallel_res_4.items[i].midpoint_temperature_c);
        const double diff_err_4 = std::abs(serial_res.items[i].energy_balance_error_w - parallel_res_4.items[i].energy_balance_error_w);

        CHECK(diff_max_4 <= 1e-8);
        CHECK(diff_mid_4 <= 1e-8);
        CHECK(diff_err_4 <= 1e-8);
    }
}

TEST_CASE("Benchmark Suite: Computes median, speedup, and efficiency metrics") {
    ThermalBatchSpecification spec;
    spec.batch_size = 100;
    spec.element_count = 20;
    spec.seed = 999;

    auto suite = run_benchmark_suite(spec, {1, 2});
    REQUIRE(suite.results.size() == 2);

    const auto& m1 = suite.results[0];
    CHECK(m1.thread_count == 1);
    CHECK(m1.repeat_times_ms.size() == 5);
    CHECK(m1.median_ms > 0.0);
    CHECK(m1.speedup == doctest::Approx(1.0));
    CHECK(m1.efficiency == doctest::Approx(1.0));

    const auto& m2 = suite.results[1];
    CHECK(m2.thread_count == 2);
    CHECK(m2.repeat_times_ms.size() == 5);
    CHECK(m2.median_ms > 0.0);
    // Speedup = T_1 / T_p
    CHECK(m2.speedup == doctest::Approx(m1.median_ms / m2.median_ms));
    // Efficiency = Speedup / p
    CHECK(m2.efficiency == doctest::Approx(m2.speedup / 2.0));

    // Test CSV export
    const std::string out_csv = "results/benchmark.csv";
    REQUIRE(export_benchmark_csv(suite, out_csv));
    REQUIRE(std::filesystem::exists(out_csv));

    std::ifstream f(out_csv);
    REQUIRE(f.is_open());
    std::string header;
    std::getline(f, header);
    CHECK(header == "threads,warmup_ms,repeat1_ms,repeat2_ms,repeat3_ms,repeat4_ms,repeat5_ms,median_ms,speedup,efficiency");
}
