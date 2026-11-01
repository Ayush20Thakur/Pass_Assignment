#pragma once

#include <string_view>

namespace forge::core {

// Returns the semantic version string of Forge Core.
[[nodiscard]] std::string_view get_version_string() noexcept;

// Simple arithmetic smoke check helper.
[[nodiscard]] int add_numbers(int first_value, int second_value) noexcept;

} // namespace forge::core
