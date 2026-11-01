#include "core/backprop.hpp"

#include <algorithm>
#include <cmath>
#include <random>

#include "core/batch.hpp"

namespace forge::core {

namespace {

inline double sigmoid(double z) noexcept {
    if (z > 40.0) return 1.0;
    if (z < -40.0) return 0.0;
    return 1.0 / (1.0 + std::exp(-z));
}

} // namespace

MultiLayerPerceptron::MultiLayerPerceptron(NetworkDimensions dims, uint64_t seed)
    : dims_(dims) {
    std::mt19937_64 rng(seed);

    // Xavier uniform initialization: U(-sqrt(6 / (fan_in + fan_out)), +sqrt(6 / (fan_in + fan_out)))
    const double limit1 = std::sqrt(6.0 / static_cast<double>(dims_.input_dim + dims_.hidden_dim));
    params_.w1.resize(dims_.hidden_dim * dims_.input_dim);
    for (double& w : params_.w1) {
        w = sample_range(rng, -limit1, limit1);
    }
    params_.b1.assign(dims_.hidden_dim, 0.0);

    const double limit2 = std::sqrt(6.0 / static_cast<double>(dims_.hidden_dim + dims_.output_dim));
    params_.w2.resize(dims_.output_dim * dims_.hidden_dim);
    for (double& w : params_.w2) {
        w = sample_range(rng, -limit2, limit2);
    }
    params_.b2.assign(dims_.output_dim, 0.0);
}

ForwardPassActivations MultiLayerPerceptron::forward(const std::vector<double>& x) const {
    ForwardPassActivations acts;
    acts.z1.resize(dims_.hidden_dim);
    acts.a1.resize(dims_.hidden_dim);
    acts.z2.resize(dims_.output_dim);
    acts.a2.resize(dims_.output_dim);

    // Layer 1 (Hidden)
    for (size_t j = 0; j < dims_.hidden_dim; ++j) {
        double sum = params_.b1[j];
        const size_t row_offset = j * dims_.input_dim;
        for (size_t k = 0; k < dims_.input_dim; ++k) {
            sum += params_.w1[row_offset + k] * x[k];
        }
        acts.z1[j] = sum;
        acts.a1[j] = sigmoid(sum);
    }

    // Layer 2 (Output)
    for (size_t k = 0; k < dims_.output_dim; ++k) {
        double sum = params_.b2[k];
        const size_t row_offset = k * dims_.hidden_dim;
        for (size_t j = 0; j < dims_.hidden_dim; ++j) {
            sum += params_.w2[row_offset + j] * acts.a1[j];
        }
        acts.z2[k] = sum;
        acts.a2[k] = sigmoid(sum);
    }

    return acts;
}

double MultiLayerPerceptron::compute_loss(const std::vector<std::vector<double>>& X,
                                         const std::vector<std::vector<double>>& Y) const {
    if (X.empty() || X.size() != Y.size()) return 0.0;

    double total_sq_error = 0.0;
    const size_t B = X.size();

    for (size_t i = 0; i < B; ++i) {
        auto acts = forward(X[i]);
        for (size_t k = 0; k < dims_.output_dim; ++k) {
            const double diff = acts.a2[k] - Y[i][k];
            total_sq_error += diff * diff;
        }
    }

    // L = 1 / (2 * B) * sum ||y_hat - y||^2
    return total_sq_error / (2.0 * static_cast<double>(B));
}

NetworkGradients MultiLayerPerceptron::backward(const std::vector<std::vector<double>>& X,
                                               const std::vector<std::vector<double>>& Y) const {
    NetworkGradients grads;
    grads.grad_w1.assign(params_.w1.size(), 0.0);
    grads.grad_b1.assign(params_.b1.size(), 0.0);
    grads.grad_w2.assign(params_.w2.size(), 0.0);
    grads.grad_b2.assign(params_.b2.size(), 0.0);

    if (X.empty() || X.size() != Y.size()) return grads;
    const double inv_B = 1.0 / static_cast<double>(X.size());

    std::vector<double> delta2(dims_.output_dim);
    std::vector<double> delta1(dims_.hidden_dim);

    for (size_t i = 0; i < X.size(); ++i) {
        const auto& x = X[i];
        const auto& y = Y[i];
        auto acts = forward(x);

        // Output error delta2 = (a2 - y) * a2 * (1 - a2)
        for (size_t k = 0; k < dims_.output_dim; ++k) {
            const double a2 = acts.a2[k];
            delta2[k] = (a2 - y[k]) * a2 * (1.0 - a2);
            grads.grad_b2[k] += delta2[k] * inv_B;

            const size_t w2_offset = k * dims_.hidden_dim;
            for (size_t j = 0; j < dims_.hidden_dim; ++j) {
                grads.grad_w2[w2_offset + j] += delta2[k] * acts.a1[j] * inv_B;
            }
        }

        // Hidden error delta1 = (W2^T * delta2) * a1 * (1 - a1)
        for (size_t j = 0; j < dims_.hidden_dim; ++j) {
            double sum = 0.0;
            for (size_t k = 0; k < dims_.output_dim; ++k) {
                sum += params_.w2[k * dims_.hidden_dim + j] * delta2[k];
            }
            const double a1 = acts.a1[j];
            delta1[j] = sum * a1 * (1.0 - a1);
            grads.grad_b1[j] += delta1[j] * inv_B;

            const size_t w1_offset = j * dims_.input_dim;
            for (size_t k = 0; k < dims_.input_dim; ++k) {
                grads.grad_w1[w1_offset + k] += delta1[j] * x[k] * inv_B;
            }
        }
    }

    return grads;
}

GradientCheckResult MultiLayerPerceptron::check_gradients(const std::vector<std::vector<double>>& X,
                                                         const std::vector<std::vector<double>>& Y,
                                                         double epsilon,
                                                         double tolerance) const {
    GradientCheckResult result;
    const auto analytical_grads = backward(X, Y);

    // Build vector of pointers to all parameters and corresponding analytical gradients
    std::vector<double*> param_ptrs;
    std::vector<double> analytical_vals;

    auto non_const_this = const_cast<MultiLayerPerceptron*>(this);

    for (size_t i = 0; i < non_const_this->params_.w1.size(); ++i) {
        param_ptrs.push_back(&non_const_this->params_.w1[i]);
        analytical_vals.push_back(analytical_grads.grad_w1[i]);
    }
    for (size_t i = 0; i < non_const_this->params_.b1.size(); ++i) {
        param_ptrs.push_back(&non_const_this->params_.b1[i]);
        analytical_vals.push_back(analytical_grads.grad_b1[i]);
    }
    for (size_t i = 0; i < non_const_this->params_.w2.size(); ++i) {
        param_ptrs.push_back(&non_const_this->params_.w2[i]);
        analytical_vals.push_back(analytical_grads.grad_w2[i]);
    }
    for (size_t i = 0; i < non_const_this->params_.b2.size(); ++i) {
        param_ptrs.push_back(&non_const_this->params_.b2[i]);
        analytical_vals.push_back(analytical_grads.grad_b2[i]);
    }

    result.checked_parameter_count = param_ptrs.size();
    result.passed = true;

    for (size_t p = 0; p < param_ptrs.size(); ++p) {
        const double orig = *param_ptrs[p];

        *param_ptrs[p] = orig + epsilon;
        const double loss_plus = compute_loss(X, Y);

        *param_ptrs[p] = orig - epsilon;
        const double loss_minus = compute_loss(X, Y);

        *param_ptrs[p] = orig; // Restore

        // Central difference numerical gradient: (L(theta + eps) - L(theta - eps)) / (2 * eps)
        const double num_grad = (loss_plus - loss_minus) / (2.0 * epsilon);
        const double ana_grad = analytical_vals[p];

        const double abs_err = std::abs(num_grad - ana_grad);
        const double denom = std::max({std::abs(num_grad), std::abs(ana_grad), 1e-7});
        const double rel_err = abs_err / denom;

        if (rel_err > result.max_relative_error) result.max_relative_error = rel_err;
        if (abs_err > result.max_absolute_error) result.max_absolute_error = abs_err;

        if (rel_err > tolerance) {
            result.passed = false;
        }
    }

    return result;
}

TrainingResult MultiLayerPerceptron::train(const std::vector<std::vector<double>>& X,
                                          const std::vector<std::vector<double>>& Y,
                                          size_t steps,
                                          double learning_rate) {
    TrainingResult result;
    result.loss_history.reserve(steps);

    result.initial_loss = compute_loss(X, Y);

    for (size_t step = 0; step < steps; ++step) {
        const double current_loss = compute_loss(X, Y);
        result.loss_history.push_back(current_loss);

        auto grads = backward(X, Y);

        // Gradient descent update: theta = theta - lr * grad
        for (size_t i = 0; i < params_.w1.size(); ++i) {
            params_.w1[i] -= learning_rate * grads.grad_w1[i];
        }
        for (size_t i = 0; i < params_.b1.size(); ++i) {
            params_.b1[i] -= learning_rate * grads.grad_b1[i];
        }
        for (size_t i = 0; i < params_.w2.size(); ++i) {
            params_.w2[i] -= learning_rate * grads.grad_w2[i];
        }
        for (size_t i = 0; i < params_.b2.size(); ++i) {
            params_.b2[i] -= learning_rate * grads.grad_b2[i];
        }
    }

    result.final_loss = compute_loss(X, Y);
    result.converged = (result.final_loss < result.initial_loss);

    return result;
}

} // namespace forge::core

