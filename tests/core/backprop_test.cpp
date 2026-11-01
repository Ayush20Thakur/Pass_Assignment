#include <cmath>
#include <vector>
#include <doctest/doctest.h>

#include "core/backprop.hpp"

using namespace forge::core;

TEST_CASE("Backprop: Forward pass output dimensions and bounds") {
    NetworkDimensions dims{2, 4, 1};
    MultiLayerPerceptron mlp(dims, 12345);

    CHECK(mlp.parameters().w1.size() == 8); // 4 * 2
    CHECK(mlp.parameters().b1.size() == 4);
    CHECK(mlp.parameters().w2.size() == 4); // 1 * 4
    CHECK(mlp.parameters().b2.size() == 1);
    CHECK(mlp.parameters().total_parameter_count() == 17);

    std::vector<double> x = {0.5, -0.2};
    auto acts = mlp.forward(x);

    CHECK(acts.z1.size() == 4);
    CHECK(acts.a1.size() == 4);
    CHECK(acts.z2.size() == 1);
    CHECK(acts.a2.size() == 1);

    // Sigmoid outputs must be strictly in (0, 1)
    for (double a : acts.a1) {
        CHECK(a > 0.0);
        CHECK(a < 1.0);
    }
    CHECK(acts.a2[0] > 0.0);
    CHECK(acts.a2[0] < 1.0);
}

TEST_CASE("Backprop: Central-difference numerical gradient check (eps 1e-6, tol 1e-5)") {
    NetworkDimensions dims{2, 4, 1};
    MultiLayerPerceptron mlp(dims, 42);

    // XOR training dataset
    std::vector<std::vector<double>> X = {
        {0.0, 0.0},
        {0.0, 1.0},
        {1.0, 0.0},
        {1.0, 1.0}
    };
    std::vector<std::vector<double>> Y = {
        {0.0},
        {1.0},
        {1.0},
        {0.0}
    };

    // Central difference gradient check
    auto check = mlp.check_gradients(X, Y, 1e-6, 1e-5);

    CHECK(check.passed == true);
    CHECK(check.checked_parameter_count == 17);
    // Relative error must be strictly within tolerance 1e-5 (measured typically < 1e-8)
    CHECK(check.max_relative_error < 1e-5);
    CHECK(check.max_absolute_error < 1e-5);
}

TEST_CASE("Backprop: 100-step training loss curve decreases monotonically") {
    NetworkDimensions dims{2, 4, 1};
    MultiLayerPerceptron mlp(dims, 42);

    // Continuous function mapping y = 0.5 * (x1 + x2)
    std::vector<std::vector<double>> X = {
        {0.1, 0.2},
        {0.2, 0.8},
        {0.7, 0.3},
        {0.9, 0.6}
    };
    std::vector<std::vector<double>> Y = {
        {0.15},
        {0.50},
        {0.50},
        {0.75}
    };

    auto train_res = mlp.train(X, Y, 100, 3.0);

    REQUIRE(train_res.loss_history.size() == 100);
    CHECK(train_res.initial_loss > train_res.final_loss);
    CHECK(train_res.final_loss < train_res.initial_loss * 0.5);

    // Verify loss decreases consistently across training steps
    size_t non_increasing_count = 0;
    for (size_t i = 1; i < train_res.loss_history.size(); ++i) {
        if (train_res.loss_history[i] <= train_res.loss_history[i - 1] + 1e-12) {
            non_increasing_count++;
        }
    }
    // At least 95 of 100 steps must be monotonically decreasing with gradient descent
    CHECK(non_increasing_count >= 95);
}
