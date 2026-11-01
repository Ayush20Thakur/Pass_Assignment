#include "core/devices.hpp"

#include <chrono>
#include <string>
#include <thread>

namespace forge::core {

namespace {

class BaseDeviceAdapter : public IDeviceAdapter {
public:
    explicit BaseDeviceAdapter(DeviceType type, std::string_view name, std::string_view part)
        : type_(type), name_(name), part_(part), state_(DeviceState::Connected) {
        start_time_ = std::chrono::steady_clock::now();
    }

    DeviceType type() const noexcept override { return type_; }
    std::string_view device_name() const noexcept override { return name_; }
    std::string_view part_number() const noexcept override { return part_; }

    void inject_fault(size_t fail_count) override {
        remaining_failures_ = fail_count;
        state_ = DeviceState::Degraded;
    }

    void clear_fault() override {
        remaining_failures_ = 0;
        state_ = DeviceState::Connected;
    }

    bool is_fault_injected() const noexcept override {
        return remaining_failures_ > 0;
    }

    std::variant<bool, Error> reset() override {
        clear_fault();
        recovery_history_.clear();
        state_ = DeviceState::Connected;
        return true;
    }

    std::variant<bool, Error> ping() override {
        if (remaining_failures_ > 0) {
            auto rec = attempt_recovery();
            if (!rec) {
                return Error{"Device timeout: ping failed", 1, "Send hardware reset to recover link"};
            }
        }
        return true;
    }

    std::variant<DeviceTelemetry, Error> read_telemetry() override {
        std::vector<RecoveryLogEntry> local_history;

        if (remaining_failures_ > 0) {
            state_ = DeviceState::Recovering;
            bool recovered = attempt_recovery(&local_history);
            if (!recovered) {
                state_ = DeviceState::Faulted;
                return Error{
                    "Device timeout: all 3 recovery retries exhausted",
                    1,
                    "Execute reset command via /api/devices/" + std::string(to_string(type_)) + "/reset"
                };
            }
            state_ = DeviceState::Connected;
        }

        DeviceTelemetry tel;
        tel.type = type_;
        tel.state = state_;
        tel.is_simulated = true; // Hard Rule 3 invariant

        const auto now = std::chrono::steady_clock::now();
        tel.uptime_seconds = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::seconds>(now - start_time_).count()
        );
        tel.sequence_number = ++sequence_counter_;
        tel.recovery_history = local_history;

        populate_metrics(tel);
        return tel;
    }

protected:
    virtual void populate_metrics(DeviceTelemetry& tel) = 0;

    bool attempt_recovery(std::vector<RecoveryLogEntry>* history_out = nullptr) {
        constexpr uint32_t backoff_schedule[3] = {200, 400, 800};

        for (size_t attempt = 1; attempt <= 3; ++attempt) {
            const uint32_t wait_ms = backoff_schedule[attempt - 1];
            std::this_thread::sleep_for(std::chrono::milliseconds(wait_ms));

            if (remaining_failures_ > 0) {
                --remaining_failures_;
            }

            if (remaining_failures_ == 0) {
                RecoveryLogEntry entry{
                    attempt,
                    wait_ms,
                    true,
                    "Communication link restored on retry attempt " + std::to_string(attempt)
                };
                if (history_out) history_out->push_back(entry);
                recovery_history_.push_back(entry);
                return true;
            } else {
                RecoveryLogEntry entry{
                    attempt,
                    wait_ms,
                    false,
                    "Timeout: no response after " + std::to_string(wait_ms) + " ms backoff"
                };
                if (history_out) history_out->push_back(entry);
                recovery_history_.push_back(entry);
            }
        }
        return false;
    }

    DeviceType type_;
    std::string name_;
    std::string part_;
    DeviceState state_;
    size_t remaining_failures_{0};
    uint64_t sequence_counter_{0};
    std::chrono::steady_clock::time_point start_time_;
    std::vector<RecoveryLogEntry> recovery_history_;
};

// 1. STM32H753ZI Microcontroller Adapter
class McuAdapter final : public BaseDeviceAdapter {
public:
    McuAdapter() : BaseDeviceAdapter(DeviceType::MCU, "Microcontroller (MCU)", "STM32H753ZI") {}

protected:
    void populate_metrics(DeviceTelemetry& tel) override {
        tel.temperature_c = 42.4 + 0.1 * (tel.sequence_number % 5);
        tel.voltage_v = 3.305;
        tel.current_ma = 145.2;
        tel.extra_metric_1 = 18.5; // CPU Load %
        tel.extra_metric_2 = 32.1; // RAM Usage %
        tel.status_word = "CORE_RUNNING_CLK_480MHZ";
    }
};

// 2. AMD / Xilinx Artix-7 FPGA Adapter
class FpgaAdapter final : public BaseDeviceAdapter {
public:
    FpgaAdapter() : BaseDeviceAdapter(DeviceType::FPGA, "FPGA Coprocessor", "XC7A100T-2FTG256C") {}

protected:
    void populate_metrics(DeviceTelemetry& tel) override {
        tel.temperature_c = 51.2 + 0.2 * (tel.sequence_number % 7);
        tel.voltage_v = 1.002; // VCCINT core 1.0V
        tel.current_ma = 482.0;
        tel.extra_metric_1 = 44.0; // DSP utilization %
        tel.extra_metric_2 = 1.801; // VCCAUX 1.8V
        tel.status_word = "MMCM_LOCKED_BITSTREAM_OK";
    }
};

// 3. FTDI FT2232HL JTAG Programmer Adapter
class JtagAdapter final : public BaseDeviceAdapter {
public:
    JtagAdapter() : BaseDeviceAdapter(DeviceType::JTAG, "JTAG Programmer & Debugger", "FT2232HL") {}

protected:
    void populate_metrics(DeviceTelemetry& tel) override {
        tel.temperature_c = 34.1 + 0.05 * (tel.sequence_number % 3);
        tel.voltage_v = 5.02; // USB VBUS 5.0V
        tel.current_ma = 76.5;
        tel.extra_metric_1 = 15000.0; // TCK 15 MHz
        tel.extra_metric_2 = 3.298;   // Target VREF 3.3V
        tel.status_word = "TAP_RUN_TEST_IDLE";
    }
};

} // namespace

std::unique_ptr<IDeviceAdapter> create_mcu_adapter() {
    return std::make_unique<McuAdapter>();
}

std::unique_ptr<IDeviceAdapter> create_fpga_adapter() {
    return std::make_unique<FpgaAdapter>();
}

std::unique_ptr<IDeviceAdapter> create_jtag_adapter() {
    return std::make_unique<JtagAdapter>();
}

DeviceSubsystem::DeviceSubsystem()
    : mcu_(create_mcu_adapter()),
      fpga_(create_fpga_adapter()),
      jtag_(create_jtag_adapter()) {}

IDeviceAdapter* DeviceSubsystem::get_adapter(DeviceType type) noexcept {
    switch (type) {
        case DeviceType::MCU: return mcu_.get();
        case DeviceType::FPGA: return fpga_.get();
        case DeviceType::JTAG: return jtag_.get();
    }
    return nullptr;
}

IDeviceAdapter* DeviceSubsystem::get_adapter(std::string_view type_name) noexcept {
    if (type_name == "mcu") return mcu_.get();
    if (type_name == "fpga") return fpga_.get();
    if (type_name == "jtag") return jtag_.get();
    return nullptr;
}

std::vector<DeviceTelemetry> DeviceSubsystem::read_all_telemetry() {
    std::vector<DeviceTelemetry> list;
    list.reserve(3);

    auto mcu_res = mcu_->read_telemetry();
    if (std::holds_alternative<DeviceTelemetry>(mcu_res)) list.push_back(std::get<DeviceTelemetry>(mcu_res));

    auto fpga_res = fpga_->read_telemetry();
    if (std::holds_alternative<DeviceTelemetry>(fpga_res)) list.push_back(std::get<DeviceTelemetry>(fpga_res));

    auto jtag_res = jtag_->read_telemetry();
    if (std::holds_alternative<DeviceTelemetry>(jtag_res)) list.push_back(std::get<DeviceTelemetry>(jtag_res));

    return list;
}

} // namespace forge::core
