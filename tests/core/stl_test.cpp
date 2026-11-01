#include <cmath>
#include <doctest/doctest.h>

#include "core/stl.hpp"

using namespace forge::core;

TEST_CASE("STL Parser: Valid ASCII cube loads correct geometry and unit normals") {
    auto result = parse_stl_file("tests/fixtures/valid_cube.stl");
    REQUIRE(std::holds_alternative<StlMesh>(result));

    const auto& mesh = std::get<StlMesh>(result);
    CHECK(mesh.solid_name == "unit_cube");
    CHECK(mesh.triangle_count == 12);
    CHECK(mesh.degenerate_count == 0);
    CHECK(mesh.vertices.size() == 12 * 3 * 3); // 12 triangles * 3 vertices * 3 coords = 108 floats
    CHECK(mesh.normals.size() == 12 * 3 * 3);

    // Bounding box must be exactly [0, 1] across all axes
    CHECK(mesh.bounding_box.min_x == doctest::Approx(0.0));
    CHECK(mesh.bounding_box.max_x == doctest::Approx(1.0));
    CHECK(mesh.bounding_box.min_y == doctest::Approx(0.0));
    CHECK(mesh.bounding_box.max_y == doctest::Approx(1.0));
    CHECK(mesh.bounding_box.min_z == doctest::Approx(0.0));
    CHECK(mesh.bounding_box.max_z == doctest::Approx(1.0));

    // Verify all normals have unit length
    for (size_t i = 0; i < mesh.normals.size(); i += 3) {
        const float nx = mesh.normals[i];
        const float ny = mesh.normals[i + 1];
        const float nz = mesh.normals[i + 2];
        const float length = std::sqrt(nx * nx + ny * ny + nz * nz);
        CHECK(length == doctest::Approx(1.0f).epsilon(1e-5));
    }
}

TEST_CASE("STL Parser: Self-made example L-bracket parses cleanly") {
    auto result = parse_stl_file("data/cube_or_bracket.stl");
    REQUIRE(std::holds_alternative<StlMesh>(result));

    const auto& mesh = std::get<StlMesh>(result);
    CHECK(mesh.solid_name == "engineering_bracket");
    CHECK(mesh.triangle_count == 20);
    CHECK(mesh.degenerate_count == 0);

    CHECK(mesh.bounding_box.min_x == doctest::Approx(0.0));
    CHECK(mesh.bounding_box.max_x == doctest::Approx(0.1));
    CHECK(mesh.bounding_box.min_y == doctest::Approx(0.0));
    CHECK(mesh.bounding_box.max_y == doctest::Approx(0.1));
    CHECK(mesh.bounding_box.min_z == doctest::Approx(0.0));
    CHECK(mesh.bounding_box.max_z == doctest::Approx(0.02));
}

TEST_CASE("STL Parser: Malformed fixtures are rejected with exact line and error text") {
    // 1. Empty STL
    auto empty_res = parse_stl_file("tests/fixtures/empty.stl");
    REQUIRE(std::holds_alternative<Error>(empty_res));
    const auto& empty_err = std::get<Error>(empty_res);
    CHECK(empty_err.line_number == 1);
    CHECK(empty_err.message == "Empty STL input");

    // 2. Binary STL
    auto bin_res = parse_stl_file("tests/fixtures/binary.stl");
    REQUIRE(std::holds_alternative<Error>(bin_res));
    const auto& bin_err = std::get<Error>(bin_res);
    CHECK(bin_err.line_number == 1);
    CHECK(bin_err.message == "binary STL not supported, export ASCII");

    // 3. Zero facets
    auto zero_res = parse_stl_file("tests/fixtures/zero_facets.stl");
    REQUIRE(std::holds_alternative<Error>(zero_res));
    const auto& zero_err = std::get<Error>(zero_res);
    CHECK(zero_err.line_number == 2);
    CHECK(zero_err.message == "valid file but contains no facets");

    // 4. Missing endloop
    auto endloop_res = parse_stl_file("tests/fixtures/missing_endloop.stl");
    REQUIRE(std::holds_alternative<Error>(endloop_res));
    const auto& endloop_err = std::get<Error>(endloop_res);
    CHECK(endloop_err.line_number == 7);
    CHECK(endloop_err.message.find("endfacet") != std::string::npos);

    // 5. NaN vertex
    auto nan_res = parse_stl_file("tests/fixtures/nan_vertex.stl");
    REQUIRE(std::holds_alternative<Error>(nan_res));
    const auto& nan_err = std::get<Error>(nan_res);
    CHECK(nan_err.line_number == 5);
    CHECK(nan_err.message.find("NaN or Inf") != std::string::npos);

    // 6. Truncated STL
    auto trunc_res = parse_stl_file("tests/fixtures/truncated.stl");
    REQUIRE(std::holds_alternative<Error>(trunc_res));
    const auto& trunc_err = std::get<Error>(trunc_res);
    CHECK(trunc_err.message.find("Missing 'endloop'") != std::string::npos);
}

TEST_CASE("STL Parser: Degenerate triangles are flagged with warning count") {
    auto degen_res = parse_stl_file("tests/fixtures/degenerate.stl");
    REQUIRE(std::holds_alternative<StlMesh>(degen_res));

    const auto& mesh = std::get<StlMesh>(degen_res);
    CHECK(mesh.triangle_count == 1);
    CHECK(mesh.degenerate_count == 1);
    REQUIRE(mesh.warnings.size() == 1);
    CHECK(mesh.warnings[0].find("Degenerate triangle") != std::string::npos);
}
