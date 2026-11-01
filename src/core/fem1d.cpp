#include "fem1d.hpp"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>

namespace forge::core {

double compute_exact_temperature(
    double x_m,
    double length_m,
    double thermal_conductivity_w_mk,
    double volumetric_heat_w_m3,
    double temperature_left_c,
    double temperature_right_c) noexcept {
    // Solves -k*T''(x) = q with boundary conditions T(0)=T0 and T(L)=TL.
    // Exact analytical form: T(x) = T0 + (TL - T0)*(x/L) + (q/(2*k))*x*(L - x).
    const double linear_slope = (temperature_right_c - temperature_left_c) / length_m;
    const double linear_part = temperature_left_c + linear_slope * x_m;
    const double curvature = volumetric_heat_w_m3 / (2.0 * thermal_conductivity_w_mk);
    const double quadratic_part = curvature * x_m * (length_m - x_m);
    return linear_part + quadratic_part;
}

std::variant<bool, Error> validate_thermal_parameters(
    const ThermalParameters1D& parameters) noexcept {
    if (!std::isfinite(parameters.length_m) || parameters.length_m <= 0.0) {
        return Error{"Rod length must be positive and finite", 0, "Set length_m > 0 (e.g. 1.0)"};
    }
    if (!std::isfinite(parameters.area_m2) || parameters.area_m2 <= 0.0) {
        return Error{"Cross-sectional area must be positive and finite", 0, "Set area_m2 > 0 (e.g. 1e-4)"};
    }
    if (!std::isfinite(parameters.thermal_conductivity_w_mk) || parameters.thermal_conductivity_w_mk <= 0.0) {
        return Error{"Thermal conductivity must be positive and finite", 0, "Set k > 0 (e.g. 10.0)"};
    }
    if (!std::isfinite(parameters.volumetric_heat_w_m3)) {
        return Error{"Volumetric heat generation must be finite", 0, "Provide a finite real number for q"};
    }
    if (!std::isfinite(parameters.temperature_left_c) || !std::isfinite(parameters.temperature_right_c)) {
        return Error{"Boundary temperatures must be finite", 0, "Provide valid numbers for T0 and TL"};
    }
    if (parameters.element_count < 1 || parameters.element_count > 1000000) {
        return Error{"Element count must be between 1 and 1,000,000", 0, "Set element_count in range [1, 1000000]"};
    }
    return true;
}

// Solves tridiagonal SPD system using the O(N) Thomas algorithm without pivoting.
static std::vector<double> solve_tridiagonal_thomas(
    size_t system_size,
    double sub_diagonal,
    double main_diagonal,
    double super_diagonal,
    std::vector<double> right_hand_side) {
    if (system_size == 0) {
        return {};
    }

    std::vector<double> modified_super(system_size, 0.0);
    std::vector<double> solution(system_size, 0.0);

    // Forward sweep
    modified_super[0] = super_diagonal / main_diagonal;
    right_hand_side[0] = right_hand_side[0] / main_diagonal;

    for (size_t i = 1; i < system_size; ++i) {
        const double denominator = main_diagonal - sub_diagonal * modified_super[i - 1];
        if (i < system_size - 1) {
            modified_super[i] = super_diagonal / denominator;
        }
        right_hand_side[i] = (right_hand_side[i] - sub_diagonal * right_hand_side[i - 1]) / denominator;
    }

    // Back substitution
    solution[system_size - 1] = right_hand_side[system_size - 1];
    for (size_t i = system_size - 1; i-- > 0;) {
        solution[i] = right_hand_side[i] - modified_super[i] * solution[i + 1];
    }

    return solution;
}

// Builds nodal grid coordinates and computes exact analytical temperatures.
static void initialize_nodal_data(
    FemResult1D& result,
    const ThermalParameters1D& parameters,
    double mesh_size) {
    const size_t node_count = parameters.element_count + 1;
    result.node_x_m.resize(node_count);
    result.exact_temperature_c.resize(node_count);

    for (size_t i = 0; i < node_count; ++i) {
        const double x = static_cast<double>(i) * mesh_size;
        result.node_x_m[i] = x;
        result.exact_temperature_c[i] = compute_exact_temperature(
            x,
            parameters.length_m,
            parameters.thermal_conductivity_w_mk,
            parameters.volumetric_heat_w_m3,
            parameters.temperature_left_c,
            parameters.temperature_right_c);
    }
}

// Computes element midpoint values using FE linear interpolation and exact reference.
static void compute_midpoint_errors(
    FemResult1D& result,
    const ThermalParameters1D& parameters,
    double mesh_size) {
    const size_t element_count = parameters.element_count;
    result.midpoint_x_m.resize(element_count);
    result.midpoint_fem_c.resize(element_count);
    result.midpoint_exact_c.resize(element_count);

    double maximum_error = 0.0;
    for (size_t i = 0; i < element_count; ++i) {
        const double mid_x = result.node_x_m[i] + 0.5 * mesh_size;
        // Linear interpolation across element i: (T_i + T_{i+1}) / 2
        const double mid_fem = 0.5 * (result.temperature_c[i] + result.temperature_c[i + 1]);
        const double mid_exact = compute_exact_temperature(
            mid_x,
            parameters.length_m,
            parameters.thermal_conductivity_w_mk,
            parameters.volumetric_heat_w_m3,
            parameters.temperature_left_c,
            parameters.temperature_right_c);

        result.midpoint_x_m[i] = mid_x;
        result.midpoint_fem_c[i] = mid_fem;
        result.midpoint_exact_c[i] = mid_exact;

        const double element_error = std::abs(mid_fem - mid_exact);
        if (element_error > maximum_error) {
            maximum_error = element_error;
        }
    }
    result.max_midpoint_error_c = maximum_error;
}

// Assembles boundary reaction forces and checks global thermal energy balance.
static void compute_reactions_and_balance(
    FemResult1D& result,
    const ThermalParameters1D& parameters,
    double element_stiffness,
    double half_element_source) {
    const size_t last_node = parameters.element_count;
    // R0 = K0j * Tj - F0 = k_elem * (T0 - T1) - F0
    result.reaction_left_w = element_stiffness * (result.temperature_c[0] - result.temperature_c[1]) - half_element_source;
    // RN = KNj * Tj - FN = k_elem * (TN - T_{N-1}) - FN
    result.reaction_right_w = element_stiffness * (result.temperature_c[last_node] - result.temperature_c[last_node - 1]) - half_element_source;

    result.total_heat_generated_w = parameters.volumetric_heat_w_m3 * parameters.area_m2 * parameters.length_m;
    // Heat escaping boundaries is (-R0) + (-RN). Conservation dictates (-R0 - RN) == q*A*L.
    const double net_boundary_heat_flow = -result.reaction_left_w - result.reaction_right_w;
    result.energy_balance_error_w = std::abs(net_boundary_heat_flow - result.total_heat_generated_w);
}

std::variant<FemResult1D, Error> solve_thermal_1d(
    const ThermalParameters1D& parameters) {
    const auto validation = validate_thermal_parameters(parameters);
    if (std::holds_alternative<Error>(validation)) {
        return std::get<Error>(validation);
    }

    const size_t element_count = parameters.element_count;
    const size_t node_count = element_count + 1;
    const double mesh_size = parameters.length_m / static_cast<double>(element_count);
    const double element_stiffness = (parameters.thermal_conductivity_w_mk * parameters.area_m2) / mesh_size;
    const double half_element_source = 0.5 * parameters.volumetric_heat_w_m3 * parameters.area_m2 * mesh_size;

    FemResult1D result;
    result.parameters = parameters;
    result.mesh_size_h_m = mesh_size;
    result.temperature_c.assign(node_count, 0.0);
    result.temperature_c[0] = parameters.temperature_left_c;
    result.temperature_c[element_count] = parameters.temperature_right_c;

    initialize_nodal_data(result, parameters, mesh_size);

    if (element_count > 1) {
        const size_t internal_count = element_count - 1;
        const double main_diagonal = 2.0 * element_stiffness;
        const double off_diagonal = -element_stiffness;
        const double internal_source = 2.0 * half_element_source; // q * A * h

        std::vector<double> right_hand_side(internal_count, internal_source);
        // Dirichlet boundary elimination onto internal right-hand side
        right_hand_side[0] += element_stiffness * parameters.temperature_left_c;
        right_hand_side[internal_count - 1] += element_stiffness * parameters.temperature_right_c;

        const auto internal_temperatures = solve_tridiagonal_thomas(
            internal_count,
            off_diagonal,
            main_diagonal,
            off_diagonal,
            std::move(right_hand_side));

        for (size_t i = 0; i < internal_count; ++i) {
            result.temperature_c[i + 1] = internal_temperatures[i];
        }
    }

    // Maximum nodal error evaluation
    double max_nodal_error = 0.0;
    for (size_t i = 0; i < node_count; ++i) {
        const double error = std::abs(result.temperature_c[i] - result.exact_temperature_c[i]);
        if (error > max_nodal_error) {
            max_nodal_error = error;
        }
    }
    result.max_nodal_error_c = max_nodal_error;

    compute_midpoint_errors(result, parameters, mesh_size);
    compute_reactions_and_balance(result, parameters, element_stiffness, half_element_source);

    return result;
}

std::vector<ConvergenceRow> run_convergence_study(
    const ThermalParameters1D& base_parameters,
    const std::vector<size_t>& element_counts) {
    std::vector<ConvergenceRow> rows;
    rows.reserve(element_counts.size());

    for (size_t i = 0; i < element_counts.size(); ++i) {
        ThermalParameters1D run_params = base_parameters;
        run_params.element_count = element_counts[i];

        auto solve_result = solve_thermal_1d(run_params);
        if (!std::holds_alternative<FemResult1D>(solve_result)) {
            continue;
        }
        const auto& result = std::get<FemResult1D>(solve_result);

        ConvergenceRow row;
        row.element_count = element_counts[i];
        row.mesh_size_h_m = result.mesh_size_h_m;
        row.max_midpoint_error_c = result.max_midpoint_error_c;

        if (i == 0) {
            row.observed_order = 0.0; // Baseline mesh
        } else {
            // Observed order: ln(e1 / e2) / ln(h1 / h2)
            const double error_ratio = rows[i - 1].max_midpoint_error_c / row.max_midpoint_error_c;
            const double mesh_ratio = rows[i - 1].mesh_size_h_m / row.mesh_size_h_m;
            row.observed_order = std::log(error_ratio) / std::log(mesh_ratio);
        }
        rows.push_back(row);
    }

    return rows;
}

std::variant<bool, Error> export_fem_results_csv(
    const FemResult1D& result,
    std::string_view file_path) {
    std::error_code ec;
    const std::filesystem::path path(file_path);
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path(), ec);
    }
    std::ofstream file(path, std::ios::trunc);
    if (!file.is_open()) {
        return Error{"Cannot open file for writing", 0, "Check file path permissions: " + std::string(file_path)};
    }

    file << "x_m,T_fem_C,T_exact_C\n";
    file << std::setprecision(12);
    for (size_t i = 0; i < result.node_x_m.size(); ++i) {
        file << result.node_x_m[i] << ","
             << result.temperature_c[i] << ","
             << result.exact_temperature_c[i] << "\n";
    }

    return true;
}

std::variant<bool, Error> export_convergence_csv(
    const std::vector<ConvergenceRow>& rows,
    std::string_view file_path) {
    std::error_code ec;
    const std::filesystem::path path(file_path);
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path(), ec);
    }
    std::ofstream file(path, std::ios::trunc);
    if (!file.is_open()) {
        return Error{"Cannot open file for writing", 0, "Check file path permissions: " + std::string(file_path)};
    }

    file << "N,h,max_mid_error,order\n";
    file << std::setprecision(12);
    for (const auto& row : rows) {
        file << row.element_count << ","
             << row.mesh_size_h_m << ","
             << row.max_midpoint_error_c << ","
             << row.observed_order << "\n";
    }

    return true;
}

} // namespace forge::core
