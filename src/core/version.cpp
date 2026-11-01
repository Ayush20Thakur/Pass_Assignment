#include "version.hpp"

namespace forge::core {

std::string_view get_version_string() noexcept {
    return "1.0.0";
}

int add_numbers(int first_value, int second_value) noexcept {
    return first_value + second_value;
}

} // namespace forge::core
