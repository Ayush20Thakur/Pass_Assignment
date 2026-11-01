#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace forge::core {

// 3D Axis-Aligned Bounding Box (AABB)
struct BoundingBox3D {
    double min_x{std::numeric_limits<double>::infinity()};
    double max_x{-std::numeric_limits<double>::infinity()};
    double min_y{std::numeric_limits<double>::infinity()};
    double max_y{-std::numeric_limits<double>::infinity()};
    double min_z{std::numeric_limits<double>::infinity()};
    double max_z{-std::numeric_limits<double>::infinity()};

    void update(double x, double y, double z) noexcept {
        min_x = std::min(min_x, x);
        max_x = std::max(max_x, x);
        min_y = std::min(min_y, y);
        max_y = std::max(max_y, y);
        min_z = std::min(min_z, z);
        max_z = std::max(max_z, z);
    }

    [[nodiscard]] bool is_valid() const noexcept {
        return min_x <= max_x && min_y <= max_y && min_z <= max_z;
    }

    [[nodiscard]] std::array<double, 3> center() const noexcept {
        if (!is_valid()) {
            return {0.0, 0.0, 0.0};
        }
        return {0.5 * (min_x + max_x), 0.5 * (min_y + max_y), 0.5 * (min_z + max_z)};
    }

    [[nodiscard]] double diagonal_length() const noexcept {
        if (!is_valid()) {
            return 0.0;
        }
        const double dx = max_x - min_x;
        const double dy = max_y - min_y;
        const double dz = max_z - min_z;
        return std::sqrt(dx * dx + dy * dy + dz * dz);
    }
};

} // namespace forge::core
