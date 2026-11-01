#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "errors.hpp"
#include "geometry_types.hpp"

namespace forge::core {

// Representation of parsed nodal temperature data for point cloud visualization.
struct NodalCsvData {
    std::vector<int64_t> node_ids;
    // Flat float32 positions: 3 floats per node (x, y, z)
    std::vector<float> positions;
    // Temperature per node in degrees Celsius
    std::vector<float> temperatures;

    double min_temperature_c{0.0};
    double max_temperature_c{0.0};
    size_t node_count{0};
    BoundingBox3D bounding_box;

    std::vector<std::string> comment_lines;
    bool is_synthetic{false};
};

// Parses nodal CSV text content.
[[nodiscard]] std::variant<NodalCsvData, Error> parse_nodal_csv_string(std::string_view content);

// Reads and parses a nodal CSV file from disk.
[[nodiscard]] std::variant<NodalCsvData, Error> parse_nodal_csv_file(std::string_view file_path);

} // namespace forge::core
