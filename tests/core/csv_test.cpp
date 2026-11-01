#include <chrono>
#include <cmath>
#include <iostream>
#include <doctest/doctest.h>

#include "core/csv.hpp"

using namespace forge::core;

TEST_CASE("CSV Parser: Valid 3-node reference fixture parses accurately") {
    auto result = parse_nodal_csv_file("tests/fixtures/valid_3node.csv");
    REQUIRE(std::holds_alternative<NodalCsvData>(result));

    const auto& data = std::get<NodalCsvData>(result);
    CHECK(data.node_count == 3);
    CHECK(data.node_ids.size() == 3);
    CHECK(data.positions.size() == 9);     // 3 nodes * 3 coords
    CHECK(data.temperatures.size() == 3);
    CHECK(data.is_synthetic == true);

    CHECK(data.node_ids[0] == 1);
    CHECK(data.node_ids[1] == 2);
    CHECK(data.node_ids[2] == 3);

    CHECK(data.min_temperature_c == doctest::Approx(25.0));
    CHECK(data.max_temperature_c == doctest::Approx(75.0));

    CHECK(data.bounding_box.min_x == doctest::Approx(0.0));
    CHECK(data.bounding_box.max_x == doctest::Approx(1.0));
}

TEST_CASE("CSV Parser: Synthetic dataset from analytical field satisfies Rule 4") {
    auto result = parse_nodal_csv_file("data/nodal_temperature_synthetic.csv");
    REQUIRE(std::holds_alternative<NodalCsvData>(result));

    const auto& data = std::get<NodalCsvData>(result);
    CHECK(data.is_synthetic == true);
    CHECK(data.node_count == 925);
    CHECK(data.node_ids.size() == 925);
    CHECK(data.positions.size() == 925 * 3);
    CHECK(data.temperatures.size() == 925);

    CHECK(data.bounding_box.min_x == doctest::Approx(0.0));
    CHECK(data.bounding_box.max_x == doctest::Approx(0.1));
    CHECK(data.bounding_box.min_y == doctest::Approx(0.0));
    CHECK(data.bounding_box.max_y == doctest::Approx(0.1));
    CHECK(data.bounding_box.min_z == doctest::Approx(0.0));
    CHECK(data.bounding_box.max_z == doctest::Approx(0.02));

    // Verify analytical temperature bounds
    CHECK(data.min_temperature_c >= 14.0);
    CHECK(data.max_temperature_c <= 125.0);
}

TEST_CASE("CSV Parser: Malformed fixtures are rejected with exact line and error text") {
    // 1. Empty CSV
    auto empty_res = parse_nodal_csv_file("tests/fixtures/empty.csv");
    REQUIRE(std::holds_alternative<Error>(empty_res));
    const auto& empty_err = std::get<Error>(empty_res);
    CHECK(empty_err.line_number == 1);
    CHECK(empty_err.message == "CSV file is completely empty (0 bytes)");

    // 2. Header-only CSV
    auto header_res = parse_nodal_csv_file("tests/fixtures/header_only.csv");
    REQUIRE(std::holds_alternative<Error>(header_res));
    const auto& header_err = std::get<Error>(header_res);
    CHECK(header_err.line_number == 1);
    CHECK(header_err.message == "CSV file contains header but no node records");

    // 3. Wrong header
    auto wrong_res = parse_nodal_csv_file("tests/fixtures/wrong_header.csv");
    REQUIRE(std::holds_alternative<Error>(wrong_res));
    const auto& wrong_err = std::get<Error>(wrong_res);
    CHECK(wrong_err.line_number == 1);
    CHECK(wrong_err.message.find("Invalid CSV header") != std::string::npos);

    // 4. Short row (< 5 fields)
    auto short_res = parse_nodal_csv_file("tests/fixtures/short_row.csv");
    REQUIRE(std::holds_alternative<Error>(short_res));
    const auto& short_err = std::get<Error>(short_res);
    CHECK(short_err.line_number == 3);
    CHECK(short_err.message.find("expected 5 fields, got 4") != std::string::npos);

    // 5. Duplicate node ID
    auto dup_res = parse_nodal_csv_file("tests/fixtures/duplicate_id.csv");
    REQUIRE(std::holds_alternative<Error>(dup_res));
    const auto& dup_err = std::get<Error>(dup_res);
    CHECK(dup_err.line_number == 4);
    CHECK(dup_err.message == "Duplicate node_id encountered: 1");

    // 6. NaN or non-finite values
    auto nan_res = parse_nodal_csv_file("tests/fixtures/nan_value.csv");
    REQUIRE(std::holds_alternative<Error>(nan_res));
    const auto& nan_err = std::get<Error>(nan_res);
    CHECK(nan_err.line_number == 3);
    CHECK(nan_err.message.find("Non-finite number (NaN or Inf)") != std::string::npos);
}

TEST_CASE("CSV Parser: Stress test with huge_ish 25,000 node dataset") {
    const auto start_time = std::chrono::high_resolution_clock::now();
    auto result = parse_nodal_csv_file("tests/fixtures/huge_ish.csv");
    const auto end_time = std::chrono::high_resolution_clock::now();

    REQUIRE(std::holds_alternative<NodalCsvData>(result));
    const auto& data = std::get<NodalCsvData>(result);
    CHECK(data.node_count == 25000);
    CHECK(data.positions.size() == 25000 * 3);
    CHECK(data.temperatures.size() == 25000);

    const double elapsed_ms = std::chrono::duration<double, std::milli>(end_time - start_time).count();
    // High performance requirement: 25,000 nodes parsed under 250 ms in C++
    CHECK(elapsed_ms < 250.0);
}
