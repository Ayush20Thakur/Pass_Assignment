#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace forge::core {

// Error representation ensuring line number, context, and actionable guidance
// are propagated without throwing exceptions across module boundaries.
struct Error {
    std::string message;
    size_t line_number{0};
    std::string actionable_fix;

    [[nodiscard]] std::string format() const {
        if (line_number > 0) {
            return "Line " + std::to_string(line_number) + ": " + message + ". Fix: " + actionable_fix;
        }
        return message + ". Fix: " + actionable_fix;
    }
};

} // namespace forge::core
