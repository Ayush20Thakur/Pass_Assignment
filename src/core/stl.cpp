#include "stl.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstring>
#include <fstream>
#include <sstream>

namespace forge::core {

namespace {

// Trims leading and trailing whitespace from string_view.
std::string_view trim_whitespace(std::string_view text) noexcept {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) {
        text.remove_prefix(1);
    }
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) {
        text.remove_suffix(1);
    }
    return text;
}

// Splits line into whitespace-separated tokens.
std::vector<std::string_view> split_tokens(std::string_view line) {
    std::vector<std::string_view> tokens;
    size_t start = 0;
    while (start < line.size()) {
        while (start < line.size() && std::isspace(static_cast<unsigned char>(line[start]))) {
            ++start;
        }
        if (start >= line.size()) {
            break;
        }
        size_t end = start;
        while (end < line.size() && !std::isspace(static_cast<unsigned char>(line[end]))) {
            ++end;
        }
        tokens.push_back(line.substr(start, end - start));
        start = end;
    }
    return tokens;
}

// Parses a single float token and checks for finiteness and validity.
std::variant<float, Error> parse_float_token(std::string_view token, size_t line_number) {
    // std::from_chars for float or strtod fallback for compatibility
    char buffer[64];
    if (token.size() >= sizeof(buffer)) {
        return Error{"Numeric token too long", line_number, "Ensure coordinate tokens are under 64 characters"};
    }
    std::memcpy(buffer, token.data(), token.size());
    buffer[token.size()] = '\0';

    char* end_ptr = nullptr;
    const double value = std::strtod(buffer, &end_ptr);
    if (end_ptr == buffer || *end_ptr != '\0') {
        return Error{"Invalid floating-point number: '" + std::string(token) + "'", line_number,
                     "Provide a valid decimal floating-point number"};
    }
    if (!std::isfinite(value)) {
        return Error{"Non-finite number encountered (NaN or Inf): '" + std::string(token) + "'", line_number,
                     "Ensure all vertex coordinates are finite numbers"};
    }
    return static_cast<float>(value);
}

// Triangle vertex accumulator during ASCII parsing
struct TriangleData {
    std::array<std::array<float, 3>, 3> vertices;
    size_t vertex_count{0};
};

// Computes geometric normal and checks for degeneracy (area < 1e-18).
void process_triangle(
    StlMesh& mesh,
    const TriangleData& triangle,
    size_t line_number) {
    const auto& v0 = triangle.vertices[0];
    const auto& v1 = triangle.vertices[1];
    const auto& v2 = triangle.vertices[2];

    const double ax = static_cast<double>(v1[0]) - static_cast<double>(v0[0]);
    const double ay = static_cast<double>(v1[1]) - static_cast<double>(v0[1]);
    const double az = static_cast<double>(v1[2]) - static_cast<double>(v0[2]);

    const double bx = static_cast<double>(v2[0]) - static_cast<double>(v0[0]);
    const double by = static_cast<double>(v2[1]) - static_cast<double>(v0[1]);
    const double bz = static_cast<double>(v2[2]) - static_cast<double>(v0[2]);

    // Cross product A x B
    const double nx = ay * bz - az * by;
    const double ny = az * bx - ax * bz;
    const double nz = ax * by - ay * bx;

    const double cross_magnitude_sq = nx * nx + ny * ny + nz * nz;
    const double triangle_area = 0.5 * std::sqrt(cross_magnitude_sq);

    float unit_nx = 0.0f;
    float unit_ny = 0.0f;
    float unit_nz = 1.0f;

    if (triangle_area < 1e-18) {
        mesh.degenerate_count++;
        mesh.warnings.push_back("Line " + std::to_string(line_number) +
                               ": Degenerate triangle with area " + std::to_string(triangle_area) + " < 1e-18");
    } else {
        const double length = std::sqrt(cross_magnitude_sq);
        unit_nx = static_cast<float>(nx / length);
        unit_ny = static_cast<float>(ny / length);
        unit_nz = static_cast<float>(nz / length);
    }

    // Append vertices and normal coordinates (flat 9 floats each)
    for (size_t i = 0; i < 3; ++i) {
        mesh.vertices.push_back(triangle.vertices[i][0]);
        mesh.vertices.push_back(triangle.vertices[i][1]);
        mesh.vertices.push_back(triangle.vertices[i][2]);

        mesh.normals.push_back(unit_nx);
        mesh.normals.push_back(unit_ny);
        mesh.normals.push_back(unit_nz);

        mesh.bounding_box.update(
            triangle.vertices[i][0],
            triangle.vertices[i][1],
            triangle.vertices[i][2]);
    }
    mesh.triangle_count++;
}

} // namespace

bool is_binary_stl(std::string_view raw_content, size_t total_size) noexcept {
    if (total_size < 84) {
        return false;
    }
    if ((total_size - 84) % 50 != 0) {
        return false;
    }

    // Read 32-bit triangle count at byte 80
    uint32_t expected_facets = 0;
    std::memcpy(&expected_facets, raw_content.data() + 80, sizeof(uint32_t));
    const size_t expected_size = 84 + static_cast<size_t>(expected_facets) * 50;
    if (expected_size == total_size) {
        return true;
    }

    // Detect non-printable characters in the header
    for (size_t i = 0; i < std::min<size_t>(80, raw_content.size()); ++i) {
        const unsigned char c = static_cast<unsigned char>(raw_content[i]);
        if (c < 9 || (c > 13 && c < 32)) {
            return true;
        }
    }
    return false;
}

std::variant<StlMesh, Error> parse_stl_string(std::string_view content) {
    if (content.empty()) {
        return Error{"Empty STL input", 1, "Provide a valid ASCII STL file containing solid definitions"};
    }

    if (is_binary_stl(content, content.size())) {
        return Error{"binary STL not supported, export ASCII", 1,
                     "Re-export the CAD model in ASCII STL format from your 3D CAD application"};
    }

    enum class State {
        WaitingForSolid,
        InsideSolid,
        InsideFacet,
        InsideLoop,
        LoopEnded,
        FacetEnded,
        SolidEnded
    };

    State state = State::WaitingForSolid;
    StlMesh mesh;
    TriangleData current_triangle;
    size_t line_number = 0;

    size_t offset = 0;
    while (offset < content.size()) {
        size_t next_line = content.find('\n', offset);
        if (next_line == std::string_view::npos) {
            next_line = content.size();
        }

        std::string_view raw_line = content.substr(offset, next_line - offset);
        offset = next_line + 1;
        line_number++;

        std::string_view line = trim_whitespace(raw_line);
        if (line.empty()) {
            continue;
        }

        const auto tokens = split_tokens(line);
        if (tokens.empty()) {
            continue;
        }

        const auto command = tokens[0];

        switch (state) {
        case State::WaitingForSolid:
            if (command == "solid") {
                if (tokens.size() > 1) {
                    mesh.solid_name = std::string(tokens[1]);
                }
                state = State::InsideSolid;
            } else {
                return Error{"Expected 'solid' at start of ASCII STL, found: '" + std::string(command) + "'",
                             line_number, "ASCII STL files must begin with 'solid [name]'"};
            }
            break;

        case State::InsideSolid:
        case State::FacetEnded:
            if (command == "facet") {
                if (tokens.size() < 2 || tokens[1] != "normal") {
                    return Error{"Expected 'facet normal' but found: '" + std::string(line) + "'", line_number,
                                 "Specify facet header as 'facet normal nx ny nz'"};
                }
                state = State::InsideFacet;
            } else if (command == "endsolid") {
                state = State::SolidEnded;
            } else {
                return Error{"Unexpected command inside solid: '" + std::string(command) + "'", line_number,
                             "Expected 'facet normal' or 'endsolid'"};
            }
            break;

        case State::InsideFacet:
            if (command == "outer" && tokens.size() >= 2 && tokens[1] == "loop") {
                current_triangle.vertex_count = 0;
                state = State::InsideLoop;
            } else {
                return Error{"Expected 'outer loop' inside facet but found: '" + std::string(line) + "'", line_number,
                             "Facet definitions must contain an 'outer loop' block"};
            }
            break;

        case State::InsideLoop:
            if (command == "vertex") {
                if (tokens.size() != 4) {
                    return Error{"'vertex' command requires exactly 3 coordinates (x y z)", line_number,
                                 "Format vertex coordinates as: 'vertex <x> <y> <z>'"};
                }
                if (current_triangle.vertex_count >= 3) {
                    return Error{"More than 3 vertices defined in loop", line_number,
                                 "An STL facet outer loop must contain exactly 3 vertices"};
                }
                for (size_t c = 0; c < 3; ++c) {
                    auto parsed_coord = parse_float_token(tokens[c + 1], line_number);
                    if (std::holds_alternative<Error>(parsed_coord)) {
                        return std::get<Error>(parsed_coord);
                    }
                    current_triangle.vertices[current_triangle.vertex_count][c] = std::get<float>(parsed_coord);
                }
                current_triangle.vertex_count++;
            } else if (command == "endloop") {
                if (current_triangle.vertex_count != 3) {
                    return Error{"Incomplete triangle: expected 3 vertices, got " +
                                     std::to_string(current_triangle.vertex_count),
                                 line_number, "Provide 3 valid vertices before 'endloop'"};
                }
                process_triangle(mesh, current_triangle, line_number);
                state = State::LoopEnded;
            } else {
                return Error{"Unexpected keyword in loop: '" + std::string(command) + "'", line_number,
                             "Expected 'vertex x y z' or 'endloop'"};
            }
            break;

        case State::LoopEnded:
            if (command == "endfacet") {
                state = State::FacetEnded;
            } else {
                return Error{"Expected 'endfacet' after loop but found: '" + std::string(line) + "'", line_number,
                             "Close facet definition with 'endfacet'"};
            }
            break;

        case State::SolidEnded:
            // Excess content after endsolid
            return Error{"Unexpected data encountered after 'endsolid'", line_number,
                         "Remove trailing content after 'endsolid'"};
        }
    }

    if (state != State::SolidEnded) {
        if (state == State::InsideLoop) {
            return Error{"Missing 'endloop' before end of file", line_number,
                         "Ensure each 'outer loop' is closed with 'endloop'"};
        }
        if (state == State::InsideFacet || state == State::LoopEnded) {
            return Error{"Missing 'endfacet' before end of file", line_number,
                         "Ensure each 'facet' is closed with 'endfacet'"};
        }
        return Error{"Unexpected end of file before 'endsolid'", line_number,
                     "Ensure file ends with 'endsolid [name]'"};
    }

    if (mesh.triangle_count == 0) {
        return Error{"valid file but contains no facets", line_number,
                     "File defines a valid solid block but contains zero facet geometry"};
    }

    return mesh;
}

std::variant<StlMesh, Error> parse_stl_file(std::string_view file_path) {
    std::ifstream file(std::string(file_path), std::ios::binary);
    if (!file.is_open()) {
        return Error{"Failed to open STL file: " + std::string(file_path), 0,
                     "Verify that the specified STL file path exists and is readable"};
    }

    std::ostringstream ss;
    ss << file.rdbuf();
    return parse_stl_string(ss.str());
}

} // namespace forge::core
