/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "nfc_emulation.h"
#include <esp_log.h>
#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <hal/board/hal_bridge.h>

#include <assets.h>
extern "C" {
#include "amiitool/nfc3d/amiibo.h"
}

static const char* TAG = "app_amiibo";

namespace nfc_emu {

// A standard NTAG215 UID prefix (NXP) and some random bytes
static const uint8_t dummy_uid[7] = {0x04, 0x1A, 0x2B, 0x3C, 0x4D, 0x5E, 0x6F};

// A dummy signature to prevent the Switch from completely rejecting it at the NFC layer
static const uint8_t dummy_signature[32] = {
    0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
    0x88, 0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF,
    0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
    0x88, 0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF
};

Emulator::~Emulator()
{
    stop();
}

extern const uint8_t locked_secret_bin_start[] asm("_binary_locked_secret_bin_start");
extern const uint8_t locked_secret_bin_end[]   asm("_binary_locked_secret_bin_end");
extern const uint8_t unfixed_info_bin_start[]  asm("_binary_unfixed_info_bin_start");
extern const uint8_t unfixed_info_bin_end[]    asm("_binary_unfixed_info_bin_end");

bool Emulator::init(i2c_master_bus_handle_t i2c, const uint8_t* bin_data, size_t bin_size)
{
    ESP_LOGI(TAG, "Initializing Emulator with Amiibo BIN data (%u bytes)", bin_size);

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

    // Setup PICC Memory from BIN file
    constexpr m5::nfc::a::Type type = m5::nfc::a::Type::NTAG_215;
    
    if (bin_size > sizeof(_picc_memory)) {
        bin_size = sizeof(_picc_memory);
    }
    
    memset(_picc_memory, 0, sizeof(_picc_memory));
    if (bin_data) {
        memcpy(_picc_memory, bin_data, bin_size);
    }

    if (bin_size >= 520) {
        // Load keys from embedded binaries
        size_t locked_size = locked_secret_bin_end - locked_secret_bin_start;
        size_t unfixed_size = unfixed_info_bin_end - unfixed_info_bin_start;
        
        if (locked_size == 80 && unfixed_size == 80) {
            ESP_LOGI(TAG, "Loaded key files from embedded binaries successfully.");
            
            // Dynamic Re-encryption
            nfc3d_amiibo_keys keys;
            memcpy(&keys.tag, locked_secret_bin_start, 80);
            memcpy(&keys.data, unfixed_info_bin_start, 80);

            uint8_t plain[NFC3D_AMIIBO_SIZE];
            if (nfc3d_amiibo_unpack(&keys, _picc_memory, plain)) {
                ESP_LOGI(TAG, "Amiibo data unpacked successfully.");
                
                // First update plain data with the dummy UID
                plain[0x1D4 + 0] = dummy_uid[0];
                plain[0x1D4 + 1] = dummy_uid[1];
                plain[0x1D4 + 2] = dummy_uid[2];
                // BCC0
                plain[0x1D4 + 3] = 0x88 ^ dummy_uid[0] ^ dummy_uid[1] ^ dummy_uid[2]; 
                plain[0x1D4 + 4] = dummy_uid[3];
                plain[0x1D4 + 5] = dummy_uid[4];
                plain[0x1D4 + 6] = dummy_uid[5];
                plain[0x1D4 + 7] = dummy_uid[6];

                // Pack the modified data back into _picc_memory
                nfc3d_amiibo_pack(&keys, plain, _picc_memory);
                ESP_LOGI(TAG, "Amiibo data packed with dummy UID.");
                
                // Ensure the BCC1 in block 1 (byte 4) is correct for the dummy UID
                _picc_memory[8] = dummy_uid[3] ^ dummy_uid[4] ^ dummy_uid[5] ^ dummy_uid[6]; // BCC1
                
                // Calculate and set the correct password for the dummy UID
                _picc_memory[532] = 0xAA ^ dummy_uid[1] ^ dummy_uid[3];
                _picc_memory[533] = 0x55 ^ dummy_uid[2] ^ dummy_uid[4];
                _picc_memory[534] = 0xAA ^ dummy_uid[3] ^ dummy_uid[5];
                _picc_memory[535] = 0x55 ^ dummy_uid[4] ^ dummy_uid[6];
                _picc_memory[536] = 0x80; // PACK0
                _picc_memory[537] = 0x80; // PACK1
                _picc_memory[538] = 0x00; // RFUI
                _picc_memory[539] = 0x00; // RFUI
            } else {
                ESP_LOGE(TAG, "Failed to unpack Amiibo data. Using raw BIN.");
            }
        } else {
            ESP_LOGE(TAG, "Invalid key sizes: locked=%u, unfixed=%u", locked_size, unfixed_size);
        }
    }

    uint8_t uid[7] = {0};
    memcpy(uid, dummy_uid, sizeof(uid));
    
    // Copy the dummy signature to the end of the memory buffer
    memcpy(&_picc_memory[540], dummy_signature, sizeof(dummy_signature));

    if (_picc.emulate(type, uid, sizeof(uid))) {
        _picc.atqa = 0x0044; // ATQA for NTAG215
        _picc.sak  = 0x00;   // SAK for NTAG215 (Type 2 Tag)
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
