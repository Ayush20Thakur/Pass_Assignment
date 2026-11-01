#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace forge::core {

struct NetworkDimensions {
    size_t input_dim{2};
    size_t hidden_dim{4};
    size_t output_dim{1};
};

struct NetworkParameters {
    // W1 is hidden_dim x input_dim (row-major: hidden_dim rows, input_dim cols)
    std::vector<double> w1;
    // b1 is hidden_dim
    std::vector<double> b1;
    // W2 is output_dim x hidden_dim (row-major: output_dim rows, hidden_dim cols)
    std::vector<double> w2;
    // b2 is output_dim
    std::vector<double> b2;

    size_t total_parameter_count() const noexcept {
        return w1.size() + b1.size() + w2.size() + b2.size();
    }
};

struct NetworkGradients {
    std::vector<double> grad_w1;
    std::vector<double> grad_b1;
    std::vector<double> grad_w2;
    std::vector<double> grad_b2;
};

struct ForwardPassActivations {
    std::vector<double> z1; // pre-activation hidden
    std::vector<double> a1; // post-activation hidden (sigmoid)
    std::vector<double> z2; // pre-activation output
    std::vector<double> a2; // post-activation output (prediction)
};

struct GradientCheckResult {
    bool passed{false};
    double max_relative_error{0.0};
    double max_absolute_error{0.0};
    size_t checked_parameter_count{0};
};

struct TrainingResult {
    std::vector<double> loss_history; // 100 steps
    double initial_loss{0.0};
    double final_loss{0.0};
    bool converged{false};
};

class MultiLayerPerceptron {
public:
    explicit MultiLayerPerceptron(NetworkDimensions dims, uint64_t seed = 42);

    const NetworkDimensions& dimensions() const noexcept { return dims_; }
    const NetworkParameters& parameters() const noexcept { return params_; }
    NetworkParameters& parameters() noexcept { return params_; }

    // Forward pass for a single input vector
    ForwardPassActivations forward(const std::vector<double>& x) const;

    // Computes Mean Squared Error loss on dataset: L = 1/(2B) sum ||y - y_hat||^2
    double compute_loss(const std::vector<std::vector<double>>& X,
                        const std::vector<std::vector<double>>& Y) const;

    // Computes analytical gradients via backpropagation
    NetworkGradients backward(const std::vector<std::vector<double>>& X,
                              const std::vector<std::vector<double>>& Y) const;

    // Performs central-difference numerical gradient checking (eps = 1e-6, tol = 1e-5)
    GradientCheckResult check_gradients(const std::vector<std::vector<double>>& X,
                                       const std::vector<std::vector<double>>& Y,
                                       double epsilon = 1e-6,
                                       double tolerance = 1e-5) const;

    // Trains the network for steps iterations with learning rate
    TrainingResult train(const std::vector<std::vector<double>>& X,
                         const std::vector<std::vector<double>>& Y,
                         size_t steps = 100,
                         double learning_rate = 1.0);

private:
    NetworkDimensions dims_;
    NetworkParameters params_;
};

} // namespace forge::core
