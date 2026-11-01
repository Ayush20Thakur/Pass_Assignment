#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "errors.hpp"
#include "geometry_types.hpp"

namespace forge::core {

// Parsed triangle mesh data representation for Three.js rendering and geometry inspection.
struct StlMesh {
    std::string solid_name;
    // Flat float32 buffer: 9 floats per triangle (x0,y0,z0, x1,y1,z1, x2,y2,z2)
    std::vector<float> vertices;
    // Flat float32 buffer: 9 floats per triangle (unit normals for each vertex)
    std::vector<float> normals;
    size_t triangle_count{0};
    size_t degenerate_count{0};
    BoundingBox3D bounding_box;
    std::vector<std::string> warnings;
};

// Checks if the given raw buffer or file size corresponds to binary STL.
[[nodiscard]] bool is_binary_stl(std::string_view raw_content, size_t total_size) noexcept;

// Parses ASCII STL text content.
[[nodiscard]] std::variant<StlMesh, Error> parse_stl_string(std::string_view content);

// Reads and parses an ASCII STL file from disk.
[[nodiscard]] std::variant<StlMesh, Error> parse_stl_file(std::string_view file_path);

} // namespace forge::core
