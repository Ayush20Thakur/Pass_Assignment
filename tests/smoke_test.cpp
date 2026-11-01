#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "core/version.hpp"

TEST_CASE("Forge Core smoke test verifies version and basic sanity") {
    CHECK(forge::core::get_version_string() == "1.0.0");
    CHECK(forge::core::add_numbers(2, 3) == 5);
}
