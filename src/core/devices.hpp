#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "core/errors.hpp"

namespace forge::core {

enum class DeviceType {
    MCU,
    FPGA,
    JTAG
};

inline std::string_view to_string(DeviceType type) noexcept {
    switch (type) {
        case DeviceType::MCU: return "mcu";
        case DeviceType::FPGA: return "fpga";
        case DeviceType::JTAG: return "jtag";
    }
    return "unknown";
}

enum class DeviceState {
    Connected,
    Degraded,
    Faulted,
    Recovering
};

inline std::string_view to_string(DeviceState state) noexcept {
    switch (state) {
        case DeviceState::Connected: return "connected";
        case DeviceState::Degraded: return "degraded";
        case DeviceState::Faulted: return "faulted";
        case DeviceState::Recovering: return "recovering";
    }
    return "unknown";
}

struct RecoveryLogEntry {
    size_t attempt{0};
    uint32_t backoff_ms{0};
    bool succeeded{false};
    std::string message;
};

struct DeviceTelemetry {
    DeviceType type{DeviceType::MCU};
    DeviceState state{DeviceState::Connected};
    bool is_simulated{true}; // Hard Rule 3: Must always be true
    uint64_t uptime_seconds{0};
    uint64_t sequence_number{0};

    // Hardware operational metrics
    double temperature_c{0.0};
    double voltage_v{0.0};
    double current_ma{0.0};

    // Subsystem-specific measurements
    double extra_metric_1{0.0}; // MCU: cpu_load_pct (%), FPGA: dsp_utilization_pct (%), JTAG: tck_freq_khz
    double extra_metric_2{0.0}; // MCU: ram_used_pct (%), FPGA: vccint_v (V), JTAG: vref_v (V)
    std::string status_word;    // e.g. "PLL_LOCKED", "CRC_OK", "TAP_IDLE"

    std::vector<RecoveryLogEntry> recovery_history;
};

class IDeviceAdapter {
public:
    virtual ~IDeviceAdapter() = default;

    virtual DeviceType type() const noexcept = 0;
    virtual std::string_view device_name() const noexcept = 0;
    virtual std::string_view part_number() const noexcept = 0;

    virtual std::variant<DeviceTelemetry, Error> read_telemetry() = 0;
    virtual std::variant<bool, Error> ping() = 0;
    virtual std::variant<bool, Error> reset() = 0;
    virtual void inject_fault(size_t fail_count = 2) = 0;
    virtual void clear_fault() = 0;
    virtual bool is_fault_injected() const noexcept = 0;
};

// Factory functions
std::unique_ptr<IDeviceAdapter> create_mcu_adapter();
std::unique_ptr<IDeviceAdapter> create_fpga_adapter();
std::unique_ptr<IDeviceAdapter> create_jtag_adapter();

// High-level subsystem coordinating all 3 devices
class DeviceSubsystem {
public:
    DeviceSubsystem();
    ~DeviceSubsystem() = default;

    IDeviceAdapter* get_adapter(DeviceType type) noexcept;
    IDeviceAdapter* get_adapter(std::string_view type_name) noexcept;

    std::vector<DeviceTelemetry> read_all_telemetry();

private:
    std::unique_ptr<IDeviceAdapter> mcu_;
    std::unique_ptr<IDeviceAdapter> fpga_;
    std::unique_ptr<IDeviceAdapter> jtag_;
};

} // namespace forge::core
