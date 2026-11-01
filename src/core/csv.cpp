#include "csv.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <unordered_set>

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

// Splits a CSV line by comma delimiter.
std::vector<std::string_view> split_csv_line(std::string_view line) {
    std::vector<std::string_view> fields;
    size_t start = 0;
    while (start <= line.size()) {
        size_t comma = line.find(',', start);
        if (comma == std::string_view::npos) {
            comma = line.size();
        }
        fields.push_back(trim_whitespace(line.substr(start, comma - start)));
        if (comma == line.size()) {
            break;
        }
        start = comma + 1;
    }
    return fields;
}

// Case-insensitive substring check.
bool contains_case_insensitive(std::string_view haystack, std::string_view needle) noexcept {
    if (needle.empty() || needle.size() > haystack.size()) {
        return false;
    }
    for (size_t i = 0; i <= haystack.size() - needle.size(); ++i) {
        bool match = true;
        for (size_t j = 0; j < needle.size(); ++j) {
            if (std::tolower(static_cast<unsigned char>(haystack[i + j])) !=
                std::tolower(static_cast<unsigned char>(needle[j]))) {
                match = false;
                break;
            }
        }
        if (match) {
            return true;
        }
    }
    return false;
}

// Parses finite double from field with error checking.
std::variant<double, Error> parse_double_field(
    std::string_view field,
    std::string_view field_name,
    size_t line_number) {
    if (field.empty()) {
        return Error{"Empty value for field '" + std::string(field_name) + "'", line_number,
                     "Provide a valid numeric value"};
    }

    char buffer[64];
    if (field.size() >= sizeof(buffer)) {
        return Error{"Value string too long for field '" + std::string(field_name) + "'", line_number,
                     "Numeric fields must be under 64 characters"};
    }
    std::memcpy(buffer, field.data(), field.size());
    buffer[field.size()] = '\0';

    char* end_ptr = nullptr;
    const double val = std::strtod(buffer, &end_ptr);
    if (end_ptr == buffer || *end_ptr != '\0') {
        return Error{"Cannot parse '" + std::string(field) + "' as a number for " + std::string(field_name),
                     line_number, "Provide a valid decimal floating-point number"};
    }
    if (!std::isfinite(val)) {
        return Error{"Non-finite number (NaN or Inf) encountered in field '" + std::string(field_name) + "'",
                     line_number, "All coordinates and temperatures must be finite numbers"};
    }
    return val;
}

// Parses non-negative integer node id.
std::variant<int64_t, Error> parse_node_id(std::string_view field, size_t line_number) {
    if (field.empty()) {
        return Error{"Empty node_id field", line_number, "Provide a non-negative integer node_id"};
    }
    int64_t id = 0;
    const auto [ptr, ec] = std::from_chars(field.data(), field.data() + field.size(), id);
    if (ec != std::errc() || ptr != field.data() + field.size() || id < 0) {
        return Error{"Invalid node_id: '" + std::string(field) + "'", line_number,
                     "node_id must be a non-negative integer"};
    }
    return id;
}

} // namespace

std::variant<NodalCsvData, Error> parse_nodal_csv_string(std::string_view content) {
    if (content.empty()) {
        return Error{"CSV file is completely empty (0 bytes)", 1,
                     "Provide a valid CSV file with header: node_id,x_m,y_m,z_m,temperature_C"};
    }

    NodalCsvData data;
    data.min_temperature_c = std::numeric_limits<double>::infinity();
    data.max_temperature_c = -std::numeric_limits<double>::infinity();

    std::unordered_set<int64_t> seen_node_ids;
    bool header_found = false;
    size_t header_line = 0;
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

        // Header comment lines (e.g., # Synthetic temperature field)
        if (line.front() == '#') {
            data.comment_lines.push_back(std::string(line));
            if (contains_case_insensitive(line, "synthetic")) {
                data.is_synthetic = true;
            }
            continue;
        }

        // Expect column header
        if (!header_found) {
            if (line != "node_id,x_m,y_m,z_m,temperature_C") {
                return Error{"Invalid CSV header: '" + std::string(line) + "'", line_number,
                             "Header must exactly match: node_id,x_m,y_m,z_m,temperature_C"};
            }
            header_found = true;
            header_line = line_number;
            continue;
        }

        // Data row: 5 comma-separated fields
        const auto fields = split_csv_line(line);
        if (fields.size() != 5) {
            return Error{"Invalid number of columns in row: expected 5 fields, got " + std::to_string(fields.size()),
                         line_number, "Each row must contain exactly 5 fields: node_id,x_m,y_m,z_m,temperature_C"};
        }

        // 1. node_id
        auto id_result = parse_node_id(fields[0], line_number);
        if (std::holds_alternative<Error>(id_result)) {
            return std::get<Error>(id_result);
        }
        const int64_t node_id = std::get<int64_t>(id_result);

        if (seen_node_ids.find(node_id) != seen_node_ids.end()) {
            return Error{"Duplicate node_id encountered: " + std::to_string(node_id), line_number,
                         "All node IDs in the CSV file must be unique"};
        }
        seen_node_ids.insert(node_id);

        // 2. x_m, y_m, z_m, temperature_C
        auto x_res = parse_double_field(fields[1], "x_m", line_number);
        if (std::holds_alternative<Error>(x_res)) return std::get<Error>(x_res);

        auto y_res = parse_double_field(fields[2], "y_m", line_number);
        if (std::holds_alternative<Error>(y_res)) return std::get<Error>(y_res);

        auto z_res = parse_double_field(fields[3], "z_m", line_number);
        if (std::holds_alternative<Error>(z_res)) return std::get<Error>(z_res);

        auto temp_res = parse_double_field(fields[4], "temperature_C", line_number);
        if (std::holds_alternative<Error>(temp_res)) return std::get<Error>(temp_res);

        const double x = std::get<double>(x_res);
        const double y = std::get<double>(y_res);
        const double z = std::get<double>(z_res);
        const double temp = std::get<double>(temp_res);

        data.node_ids.push_back(node_id);
        data.positions.push_back(static_cast<float>(x));
        data.positions.push_back(static_cast<float>(y));
        data.positions.push_back(static_cast<float>(z));
        data.temperatures.push_back(static_cast<float>(temp));

        data.bounding_box.update(x, y, z);
        data.min_temperature_c = std::min(data.min_temperature_c, temp);
        data.max_temperature_c = std::max(data.max_temperature_c, temp);
        data.node_count++;
    }

    if (!header_found) {
        return Error{"Missing header row in CSV file", line_number,
                     "File must contain header line: node_id,x_m,y_m,z_m,temperature_C"};
    }

    if (data.node_count == 0) {
        return Error{"CSV file contains header but no node records", header_line,
                     "Provide data rows below the header"};
    }

    return data;
}

std::variant<NodalCsvData, Error> parse_nodal_csv_file(std::string_view file_path) {
    std::ifstream file(std::string(file_path), std::ios::binary);
    if (!file.is_open()) {
        return Error{"Failed to open CSV file: " + std::string(file_path), 0,
                     "Verify that the specified CSV file exists and is readable"};
    }

    std::ostringstream ss;
    ss << file.rdbuf();
    return parse_nodal_csv_string(ss.str());
}

} // namespace forge::core
