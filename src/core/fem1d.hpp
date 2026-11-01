#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "errors.hpp"

namespace forge::core {

// Parameters for 1D steady-state heat conduction problem:
// -d/dx(k*A*dT/dx) = q*A on x in [0, L] with T(0) = T0, T(L) = TL.
struct ThermalParameters1D {
    double length_m{1.0};
    double area_m2{1e-4};
    double thermal_conductivity_w_mk{10.0};
    double volumetric_heat_w_m3{10000.0};
    double temperature_left_c{100.0};
    double temperature_right_c{20.0};
    size_t element_count{10};
};

// Summary row for multi-mesh convergence analysis.
struct ConvergenceRow {
    size_t element_count{0};
    double mesh_size_h_m{0.0};
    double max_midpoint_error_c{0.0};
    double observed_order{0.0};
};

// Complete results of 1D FEM solve including errors, reactions, and energy balance.
struct FemResult1D {
    ThermalParameters1D parameters;
    double mesh_size_h_m{0.0};

    // Nodal values
    std::vector<double> node_x_m;
    std::vector<double> temperature_c;
    std::vector<double> exact_temperature_c;

    // Midpoint values (FE linear interpolation vs exact quadratic solution)
    std::vector<double> midpoint_x_m;
    std::vector<double> midpoint_fem_c;
    std::vector<double> midpoint_exact_c;

    // Error metrics
    double max_nodal_error_c{0.0};
    double max_midpoint_error_c{0.0};

    // Boundary reaction forces (heat rates in Watts)
    double reaction_left_w{0.0};  // R0
    double reaction_right_w{0.0}; // RN
    double total_heat_generated_w{0.0};
    double energy_balance_error_w{0.0};
};

// Analytical exact temperature at coordinate x.
[[nodiscard]] double compute_exact_temperature(
    double x_m,
    double length_m,
    double thermal_conductivity_w_mk,
    double volumetric_heat_w_m3,
    double temperature_left_c,
    double temperature_right_c) noexcept;

// Validates input parameters against physical and numerical limits.
[[nodiscard]] std::variant<bool, Error> validate_thermal_parameters(
    const ThermalParameters1D& parameters) noexcept;

// Solves 1D steady-state thermal FEM with linear elements and Dirichlet elimination.
[[nodiscard]] std::variant<FemResult1D, Error> solve_thermal_1d(
    const ThermalParameters1D& parameters);

// Computes convergence table across multiple mesh resolutions.
[[nodiscard]] std::vector<ConvergenceRow> run_convergence_study(
    const ThermalParameters1D& base_parameters,
    const std::vector<size_t>& element_counts);

// Exports nodal results to CSV with columns: x_m,T_fem_C,T_exact_C.
[[nodiscard]] std::variant<bool, Error> export_fem_results_csv(
    const FemResult1D& result,
    std::string_view file_path);

// Exports convergence table to CSV with columns: N,h,max_mid_error,order.
[[nodiscard]] std::variant<bool, Error> export_convergence_csv(
    const std::vector<ConvergenceRow>& rows,
    std::string_view file_path);

} // namespace forge::core
