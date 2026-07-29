/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <memory>

#include <M5UnitUnified.hpp>
#include <M5UnitUnifiedNFC.h>
#include <driver/i2c_master.h>

namespace nfc_emu {

enum class Status {
    Idle,           
    Listening,      
    Selected,       
    Error,          
};

class Emulator {
public:
    Emulator() = default;
    ~Emulator();

    bool init(i2c_master_bus_handle_t i2c, const uint8_t* bin_data, size_t bin_size);
    bool start();
    void stop();
    Status getStatus();
    void update();

private:
    std::unique_ptr<m5::unit::UnitUnified> _units;
    std::unique_ptr<m5::unit::UnitNFC> _unit;
    std::unique_ptr<m5::nfc::EmulationLayerA> _emu_a;
    m5::nfc::a::PICC _picc;
    uint8_t _picc_memory[572];
    Status _status = Status::Idle;
};

} // namespace nfc_emu
