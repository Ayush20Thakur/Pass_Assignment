#include <chrono>
#include <iostream>
#include <string>
#include <string_view>
#include <doctest/doctest.h>

#include "core/devices.hpp"

using namespace forge::core;

TEST_CASE("Embedded Devices: Creation and metadata compliance") {
    auto mcu = create_mcu_adapter();
    REQUIRE(mcu != nullptr);
    CHECK(mcu->type() == DeviceType::MCU);
    CHECK(mcu->device_name() == "Microcontroller (MCU)");
    CHECK(mcu->part_number() == "STM32H753ZI");

    auto fpga = create_fpga_adapter();
    REQUIRE(fpga != nullptr);
    CHECK(fpga->type() == DeviceType::FPGA);
    CHECK(fpga->device_name() == "FPGA Coprocessor");
    CHECK(fpga->part_number() == "XC7A100T-2FTG256C");

    auto jtag = create_jtag_adapter();
    REQUIRE(jtag != nullptr);
    CHECK(jtag->type() == DeviceType::JTAG);
    CHECK(jtag->device_name() == "JTAG Programmer & Debugger");
    CHECK(jtag->part_number() == "FT2232HL");
}

TEST_CASE("Embedded Devices: Telemetry reads and Rule 3 SIMULATED invariant") {
    DeviceSubsystem subsystem;

    auto telemetry_list = subsystem.read_all_telemetry();
    REQUIRE(telemetry_list.size() == 3);

    for (const auto& tel : telemetry_list) {
        // Hard Rule 3: The ONLY simulated subsystem is the embedded devices. Label it SIMULATED in UI, JSON ("simulated": true) and in docs.
        CHECK(tel.is_simulated == true);
        CHECK(tel.state == DeviceState::Connected);
        CHECK(tel.temperature_c > 0.0);
        CHECK(tel.voltage_v > 0.0);
    }
}

TEST_CASE("Embedded Devices: Timeout injection and automated recovery with 200, 400, 800 ms backoff") {
    auto mcu = create_mcu_adapter();
    REQUIRE(mcu != nullptr);

    // 1. Initial healthy state
    auto t1 = mcu->read_telemetry();
    REQUIRE(std::holds_alternative<DeviceTelemetry>(t1));
    CHECK(std::get<DeviceTelemetry>(t1).state == DeviceState::Connected);

    // 2. Inject fault for 2 failed attempts (should recover on attempt 2: 200 ms then 400 ms)
    mcu->inject_fault(2);
    CHECK(mcu->is_fault_injected());

    const auto start_time = std::chrono::steady_clock::now();
    auto t2 = mcu->read_telemetry();
    const auto end_time = std::chrono::steady_clock::now();

    // Must successfully recover after 2 retries
    REQUIRE(std::holds_alternative<DeviceTelemetry>(t2));
    const auto& tel2 = std::get<DeviceTelemetry>(t2);
    CHECK(tel2.state == DeviceState::Connected);
    REQUIRE(tel2.recovery_history.size() == 2);

    CHECK(tel2.recovery_history[0].attempt == 1);
    CHECK(tel2.recovery_history[0].backoff_ms == 200);
    CHECK(tel2.recovery_history[0].succeeded == false);

    CHECK(tel2.recovery_history[1].attempt == 2);
    CHECK(tel2.recovery_history[1].backoff_ms == 400);
    CHECK(tel2.recovery_history[1].succeeded == true);

    // Elapsed time should reflect backoff delays (~600 ms)
    const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();
    CHECK(elapsed_ms >= 550);

    // 3. Inject permanent fault exceeding 3 retries (fail_count = 5)
    mcu->inject_fault(5);
    auto t3 = mcu->read_telemetry();

    // Must fail and return Error object
    REQUIRE(std::holds_alternative<Error>(t3));
    const auto& err = std::get<Error>(t3);
    CHECK(err.message.find("timeout") != std::string::npos);
    CHECK(err.actionable_fix.find("reset") != std::string::npos);

    // 4. Hardware reset recovers from faulted state
    auto reset_res = mcu->reset();
    REQUIRE(std::holds_alternative<bool>(reset_res));
    CHECK(std::get<bool>(reset_res) == true);

    auto t4 = mcu->read_telemetry();
    REQUIRE(std::holds_alternative<DeviceTelemetry>(t4));
    CHECK(std::get<DeviceTelemetry>(t4).state == DeviceState::Connected);
}
