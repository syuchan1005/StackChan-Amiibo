/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "nfc_emulation.h"
#include <esp_log.h>
#include <cstring>
#include <hal/board/hal_bridge.h>

static const char* TAG = "NFC_EMU";

namespace nfc_emu {

static uint8_t bcc8(const uint8_t* p, const uint8_t len, const uint8_t init = 0)
{
    uint8_t v = init;
    for (uint_fast8_t i = 0; i < len; ++i) {
        v ^= p[i];
    }
    return v;
}

static void embed_uid(uint8_t mem[9], const uint8_t uid[7])
{
    memcpy(mem, uid, 3);
    mem[3] = bcc8(uid, 3, 0x88);
    memcpy(mem + 4, uid + 3, 4);
    mem[8] = bcc8(uid + 3, 4);
}

Emulator::~Emulator()
{
    stop();
}

bool Emulator::init(i2c_master_bus_handle_t i2c, const char* url)
{
    ESP_LOGI(TAG, "Initializing Emulator with URL: %s", url);

    if (!i2c) {
        ESP_LOGE(TAG, "I2C bus is null");
        return false;
    }

    _units = std::make_unique<m5::unit::UnitUnified>();
    _unit = std::make_unique<m5::unit::UnitNFC>();
    _emu_a = std::make_unique<m5::nfc::EmulationLayerA>(*_unit);

    auto cfg = _unit->config();
    cfg.emulation = true;
    cfg.mode = m5::nfc::NFC::A;
    _unit->config(cfg);

    bool unit_ready = _units->add(*_unit, i2c) && _units->begin();
    if (!unit_ready) {
        ESP_LOGE(TAG, "Failed to begin M5UnitUnified (ST25R3916 not found)");
        _status = Status::Error;
        return false;
    }
    ESP_LOGI(TAG, "ST25R3916 found on In_I2C!");

    // Setup PICC Memory (NDEF URI)
    constexpr m5::nfc::a::Type type = m5::nfc::a::Type::MIFARE_Ultralight;
    constexpr uint8_t default_uid[] = {0x04, 0x34, 0x56, 0x78, 0x9A, 0xBC, 0xDE};

    memset(_picc_memory, 0, sizeof(_picc_memory));
    
    // Create NDEF Message
    size_t url_len = strlen(url);
    if (url_len > 40) url_len = 40;

    _picc_memory[12] = 0xE1;
    _picc_memory[13] = 0x10;
    _picc_memory[14] = 0x06;
    _picc_memory[15] = 0x00;
    
    _picc_memory[16] = 0x03; // NDEF Message
    _picc_memory[17] = url_len + 5; // Length
    _picc_memory[18] = 0xD1; // NDEF Record Header
    _picc_memory[19] = 0x01; // Type Length
    _picc_memory[20] = url_len + 1; // Payload Length
    _picc_memory[21] = 0x55; // 'U' (URI)
    _picc_memory[22] = 0x00; // No prefix
    
    memcpy(&_picc_memory[23], url, url_len);
    _picc_memory[23 + url_len] = 0xFE; // Terminator

    if (_picc.emulate(type, default_uid, sizeof(default_uid))) {
        embed_uid(_picc_memory, default_uid);
    }

    _status = Status::Idle;
    return true;
}

bool Emulator::start()
{
    if (!_unit || !_emu_a) return false;
    
    if (_emu_a->begin(_picc, _picc_memory, sizeof(_picc_memory))) {
        ESP_LOGI(TAG, "Emulation started!");
        _status = Status::Listening;
        return true;
    }
    ESP_LOGE(TAG, "Failed to start emulation layer!");
    return false;
}

void Emulator::stop()
{
    if (_emu_a) {
        _emu_a->end();
    }
    _status = Status::Idle;
}

Status Emulator::getStatus()
{
    return _status;
}

void Emulator::update()
{
    if (!_units || !_emu_a) return;
    
    _units->update();
    _emu_a->update();

    auto layer_state = _emu_a->state();
    
    switch (layer_state) {
        case m5::nfc::EmulationLayerA::State::None:
            _status = Status::Idle;
            break;
        case m5::nfc::EmulationLayerA::State::Off:
        case m5::nfc::EmulationLayerA::State::Idle:
            _status = Status::Listening;
            break;
        case m5::nfc::EmulationLayerA::State::Ready:
        case m5::nfc::EmulationLayerA::State::Active:
        case m5::nfc::EmulationLayerA::State::Halt:
            _status = Status::Selected;
            break;
    }
}

} // namespace nfc_emu
