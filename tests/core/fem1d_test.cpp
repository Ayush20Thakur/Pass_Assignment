#include <cmath>
#include <fstream>
#include <string>
#include <vector>

#include <doctest/doctest.h>

#include "core/fem1d.hpp"

using namespace forge::core;

TEST_CASE("FEM 1D: Midpoint error and second-order convergence on uniform meshes") {
    ThermalParameters1D params;
    params.length_m = 1.0;
    params.area_m2 = 1e-4;
    params.thermal_conductivity_w_mk = 10.0;
    params.volumetric_heat_w_m3 = 10000.0;
    params.temperature_left_c = 100.0;
    params.temperature_right_c = 20.0;

    const double expected_factor = params.volumetric_heat_w_m3 / (8.0 * params.thermal_conductivity_w_mk); // 125.0

    struct ReferenceMesh {
        size_t n;
        double h;
        double expected_mid_error;
    };

    const std::vector<ReferenceMesh> reference_meshes = {
        {10, 0.1, 1.25},
        {20, 0.05, 0.3125},
        {40, 0.025, 0.078125},
        {80, 0.0125, 0.01953125}
    };

    for (const auto& ref : reference_meshes) {
        params.element_count = ref.n;
        auto solve_result = solve_thermal_1d(params);

        REQUIRE(std::holds_alternative<FemResult1D>(solve_result));
        const auto& result = std::get<FemResult1D>(solve_result);

        // Assert midpoint formula: error = q * h^2 / (8 * k) within 1e-9
        const double analytic_mid_error = expected_factor * ref.h * ref.h;
        CHECK(std::abs(analytic_mid_error - ref.expected_mid_error) < 1e-12);
        CHECK(std::abs(result.max_midpoint_error_c - ref.expected_mid_error) < 1e-9);

        // Assert nodal exactness: error < 1e-10 C
        CHECK(result.max_nodal_error_c < 1e-10);

        // Assert energy balance: (-R0) + (-RN) = q * A * L within 1e-9 W
        const double expected_heat_w = params.volumetric_heat_w_m3 * params.area_m2 * params.length_m;
        CHECK(std::abs(result.total_heat_generated_w - expected_heat_w) < 1e-12);
        CHECK(result.energy_balance_error_w < 1e-9);
    }
}

TEST_CASE("FEM 1D: Convergence study order of accuracy equals 2.00 (+-0.01)") {
    ThermalParameters1D base_params;
    base_params.length_m = 1.0;
    base_params.area_m2 = 1e-4;
    base_params.thermal_conductivity_w_mk = 10.0;
    base_params.volumetric_heat_w_m3 = 10000.0;
    base_params.temperature_left_c = 100.0;
    base_params.temperature_right_c = 20.0;

    const std::vector<size_t> mesh_counts = {10, 20, 40, 80};
    const auto convergence_rows = run_convergence_study(base_params, mesh_counts);

    REQUIRE(convergence_rows.size() == 4);

    // Check convergence rate for N=20, 40, 80
    for (size_t i = 1; i < convergence_rows.size(); ++i) {
        CHECK(convergence_rows[i].observed_order >= 1.99);
        CHECK(convergence_rows[i].observed_order <= 2.01);
    }
}

TEST_CASE("FEM 1D: Boundary and negative parameter validation") {
    ThermalParameters1D params;

    // N = 0 rejected
    params.element_count = 0;
    auto res_n0 = solve_thermal_1d(params);
    CHECK(std::holds_alternative<Error>(res_n0));

    // Reset N, test k <= 0
    params.element_count = 10;
    params.thermal_conductivity_w_mk = 0.0;
    auto res_k0 = solve_thermal_1d(params);
    CHECK(std::holds_alternative<Error>(res_k0));

    params.thermal_conductivity_w_mk = -5.0;
    auto res_k_neg = solve_thermal_1d(params);
    CHECK(std::holds_alternative<Error>(res_k_neg));

    // Test Area <= 0
    params.thermal_conductivity_w_mk = 10.0;
    params.area_m2 = 0.0;
    auto res_a0 = solve_thermal_1d(params);
    CHECK(std::holds_alternative<Error>(res_a0));

    params.area_m2 = -1e-4;
    auto res_a_neg = solve_thermal_1d(params);
    CHECK(std::holds_alternative<Error>(res_a_neg));

    // Test Length <= 0
    params.area_m2 = 1e-4;
    params.length_m = -1.0;
    auto res_l_neg = solve_thermal_1d(params);
    CHECK(std::holds_alternative<Error>(res_l_neg));

    // Test NaN inputs
    params.length_m = 1.0;
    params.volumetric_heat_w_m3 = std::numeric_limits<double>::quiet_NaN();
    auto res_nan = solve_thermal_1d(params);
    CHECK(std::holds_alternative<Error>(res_nan));

    // Test Infinite inputs
    params.volumetric_heat_w_m3 = std::numeric_limits<double>::infinity();
    auto res_inf = solve_thermal_1d(params);
    CHECK(std::holds_alternative<Error>(res_inf));
}

TEST_CASE("FEM 1D: Minimal single-element mesh N=1 solves correctly") {
    ThermalParameters1D params;
    params.element_count = 1;
    params.length_m = 1.0;
    params.area_m2 = 1e-4;
    params.thermal_conductivity_w_mk = 10.0;
    params.volumetric_heat_w_m3 = 10000.0;
    params.temperature_left_c = 100.0;
    params.temperature_right_c = 20.0;

    auto solve_result = solve_thermal_1d(params);
    REQUIRE(std::holds_alternative<FemResult1D>(solve_result));

    const auto& result = std::get<FemResult1D>(solve_result);
    REQUIRE(result.node_x_m.size() == 2);
    CHECK(result.temperature_c[0] == doctest::Approx(100.0));
    CHECK(result.temperature_c[1] == doctest::Approx(20.0));

    // Energy balance must still hold for N=1
    CHECK(result.energy_balance_error_w < 1e-9);

    // Midpoint error for N=1: h=1.0, q*h^2/(8*k) = 10000*1 / 80 = 125.0 C
    CHECK(std::abs(result.max_midpoint_error_c - 125.0) < 1e-9);
}

TEST_CASE("FEM 1D: CSV export produces valid files and required columns") {
    ThermalParameters1D params;
    params.length_m = 1.0;
    params.area_m2 = 1e-4;
    params.thermal_conductivity_w_mk = 10.0;
    params.volumetric_heat_w_m3 = 10000.0;
    params.temperature_left_c = 100.0;
    params.temperature_right_c = 20.0;

    const std::vector<size_t> mesh_sizes = {10, 20, 40, 80};
    for (size_t n : mesh_sizes) {
        params.element_count = n;
        auto solve_result = solve_thermal_1d(params);
        REQUIRE(std::holds_alternative<FemResult1D>(solve_result));
        const auto& result = std::get<FemResult1D>(solve_result);

        const std::string fem_csv_path = "results/fem_N" + std::to_string(n) + ".csv";
        auto export_res = export_fem_results_csv(result, fem_csv_path);
        REQUIRE(std::holds_alternative<bool>(export_res));
        CHECK(std::get<bool>(export_res) == true);

        std::ifstream infile(fem_csv_path);
        REQUIRE(infile.is_open());
        std::string header;
        std::getline(infile, header);
        CHECK(header == "x_m,T_fem_C,T_exact_C");
    }

    // Also verify convergence CSV export
    const auto convergence_rows = run_convergence_study(params, mesh_sizes);
    const std::string conv_csv_path = "results/convergence.csv";
    auto conv_export_res = export_convergence_csv(convergence_rows, conv_csv_path);
    REQUIRE(std::holds_alternative<bool>(conv_export_res));
    CHECK(std::get<bool>(conv_export_res) == true);

    std::ifstream conv_file(conv_csv_path);
    REQUIRE(conv_file.is_open());
    std::string conv_header;
    std::getline(conv_file, conv_header);
    CHECK(conv_header == "N,h,max_mid_error,order");
}
