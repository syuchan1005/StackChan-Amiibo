/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "st25r3916.h"
#include <cstring>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

static const char* TAG = "ST25R3916";

namespace st25r3916 {

Driver::~Driver()
{
    deinit();
}

/* ================================================================== */
/*  Init / Deinit                                                     */
/* ================================================================== */

esp_err_t Driver::tryAddress(uint8_t addr)
{
    // Remove existing device if any
    if (dev_handle_) {
        i2c_master_bus_rm_device(dev_handle_);
        dev_handle_ = nullptr;
    }

    i2c_device_config_t dev_cfg = {};
    dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev_cfg.device_address  = addr;
    dev_cfg.scl_speed_hz    = kI2cFreqHz;

    esp_err_t ret = i2c_master_bus_add_device(bus_handle_, &dev_cfg, &dev_handle_);
    if (ret != ESP_OK) {
        ESP_LOGD(TAG, "  addr 0x%02X: add_device failed", addr);
        return ret;
    }

    // Try reading IC Identity register (0x1F)
    uint8_t chip_id = 0;
    ret = readRegister(REG_IC_IDENTITY, &chip_id);
    if (ret != ESP_OK) {
        ESP_LOGD(TAG, "  addr 0x%02X: read failed: %s", addr, esp_err_to_name(ret));
        i2c_master_bus_rm_device(dev_handle_);
        dev_handle_ = nullptr;
        return ret;
    }

    // A real ST25R3916 will have a valid IC Identity. ST25R3916 type is 5, ST25R3916B type is 6.
    // An EEPROM will often return 0x00 or whatever happens to be at memory address 0x3F.
    uint8_t chip_type = (chip_id >> 3) & 0x1F;
    if (chip_id == 0x00 || chip_id == 0xFF || (chip_type != 0x05 && chip_type != 0x06)) {
        ESP_LOGI(TAG, "  addr 0x%02X: read OK, but invalid IC_IDENTITY: 0x%02X (not ST25R3916)", addr, chip_id);
        i2c_master_bus_rm_device(dev_handle_);
        dev_handle_ = nullptr;
        return ESP_ERR_NOT_FOUND;
    }

    // Try writing to a register and reading it back
    uint8_t orig_val = 0;
    readRegister(REG_BIT_RATE, &orig_val);
    ret = writeRegister(REG_BIT_RATE, 0x00);
    if (ret != ESP_OK) {
        ESP_LOGI(TAG, "  addr 0x%02X: IC=0x%02X, read OK but write FAILED", addr, chip_id);
        i2c_master_bus_rm_device(dev_handle_);
        dev_handle_ = nullptr;
        return ret;
    }

    // Verify write
    uint8_t readback = 0xFF;
    readRegister(REG_BIT_RATE, &readback);
    writeRegister(REG_BIT_RATE, orig_val);  // Restore

    device_addr_ = addr;
    ESP_LOGI(TAG, "  addr 0x%02X: IC=0x%02X, write OK, readback=0x%02X => FOUND",
             addr, chip_id, readback);
    return ESP_OK;
}

esp_err_t Driver::init()
{
    if (initialized_) return ESP_OK;

    /* ── Create I2C master bus on Port A ──────────────────────── */
    i2c_master_bus_config_t bus_cfg = {};
    bus_cfg.clk_source              = I2C_CLK_SRC_DEFAULT;
    bus_cfg.i2c_port                = I2C_NUM_0;
    bus_cfg.scl_io_num              = static_cast<gpio_num_t>(kSclPin);
    bus_cfg.sda_io_num              = static_cast<gpio_num_t>(kSdaPin);
    bus_cfg.glitch_ignore_cnt       = 7;
    bus_cfg.flags.enable_internal_pullup = true;

    esp_err_t ret = i2c_new_master_bus(&bus_cfg, &bus_handle_);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create Port A I2C bus: %s", esp_err_to_name(ret));
        return ret;
    }
    owns_bus_ = true;

    ret = initWithBus(bus_handle_, kI2cAddr);
    if (ret != ESP_OK) {
        i2c_del_master_bus(bus_handle_);
        bus_handle_ = nullptr;
        owns_bus_ = false;
    }
    return ret;
}

esp_err_t Driver::initWithBus(i2c_master_bus_handle_t bus_handle, uint8_t addr)
{
    if (initialized_) return ESP_OK;
    bus_handle_ = bus_handle;

    vTaskDelay(pdMS_TO_TICKS(10));

    /* ── Candidate NFC addresses to try ──────────────────────── */
    // ST25R3916 default = 0x50, but also check others seen on internal bus
    static const uint8_t candidates[] = { 0x50, 0x29, 0x23, 0x2B, 0x3B, 0x69, 0x6F };

    if (addr != 0) {
        // Try specific address first
        ESP_LOGI(TAG, "Trying specific address 0x%02X...", addr);
        if (tryAddress(addr) == ESP_OK) {
            goto found;
        }
    }

    // Auto-scan candidate addresses
    ESP_LOGI(TAG, "Auto-scanning for NFC chip on I2C bus...");
    for (auto candidate : candidates) {
        if (candidate == addr) continue;  // Already tried
        ESP_LOGI(TAG, "Trying 0x%02X...", candidate);
        if (tryAddress(candidate) == ESP_OK) {
            goto found;
        }
    }

    // Full scan for any writable device that could be an NFC chip
    ESP_LOGI(TAG, "Full I2C scan for NFC chip...");
    for (uint8_t a = 0x08; a <= 0x77; a++) {
        // Skip known non-NFC addresses on internal bus
        if (a == 0x21 || a == 0x34 || a == 0x36 || a == 0x38 ||
            a == 0x40 || a == 0x41 || a == 0x51 || a == 0x58 ||
            a == 0x68) {
            continue;
        }
        // Check if device exists
        esp_err_t probe_ret = i2c_master_probe(bus_handle_, a, 50);
        if (probe_ret == ESP_OK) {
            ESP_LOGI(TAG, "Device at 0x%02X, testing...", a);
            if (tryAddress(a) == ESP_OK) {
                goto found;
            }
        }
    }

    ESP_LOGE(TAG, "No NFC chip found on bus!");
    return ESP_ERR_NOT_FOUND;

found:
    {
        uint8_t chip_id = getChipId();
        uint8_t chip_type = (chip_id >> 3) & 0x1F;
        ESP_LOGI(TAG, "NFC chip found at 0x%02X: IC=0x%02X (type=0x%02X, rev=%d)",
                 device_addr_, chip_id, chip_type, chip_id & 0x07);

        sendCommand(CMD_SET_DEFAULT);
        vTaskDelay(pdMS_TO_TICKS(5));

        initialized_ = true;
        return ESP_OK;
    }
}

void Driver::deinit()
{
    if (!initialized_) return;

    if (dev_handle_) {
        i2c_master_bus_rm_device(dev_handle_);
        dev_handle_ = nullptr;
    }
    if (owns_bus_ && bus_handle_) {
        i2c_del_master_bus(bus_handle_);
    }
    bus_handle_ = nullptr;
    owns_bus_ = false;
    initialized_ = false;
    ESP_LOGI(TAG, "Deinitialized");
}

/* ================================================================== */
/*  Register Access                                                   */
/* ================================================================== */

esp_err_t Driver::readRegister(uint8_t reg, uint8_t* value)
{
    uint8_t sub_addr = kModeRegRead | (reg & 0x3F);
    return i2c_master_transmit_receive(dev_handle_, &sub_addr, 1, value, 1, 100);
}

esp_err_t Driver::writeRegister(uint8_t reg, uint8_t value)
{
    uint8_t buf[2] = { static_cast<uint8_t>(kModeRegWrite | (reg & 0x3F)), value };
    return i2c_master_transmit(dev_handle_, buf, 2, 100);
}

esp_err_t Driver::readRegisters(uint8_t reg, uint8_t* buf, size_t len)
{
    uint8_t sub_addr = kModeRegRead | (reg & 0x3F);
    return i2c_master_transmit_receive(dev_handle_, &sub_addr, 1, buf, len, 100);
}

esp_err_t Driver::modifyRegister(uint8_t reg, uint8_t clear_mask, uint8_t set_mask)
{
    uint8_t val = 0;
    esp_err_t ret = readRegister(reg, &val);
    if (ret != ESP_OK) return ret;
    val = (val & ~clear_mask) | set_mask;
    return writeRegister(reg, val);
}

/* ================================================================== */
/*  Direct Commands                                                   */
/* ================================================================== */

esp_err_t Driver::sendCommand(uint8_t cmd)
{
    uint8_t sub_addr = kModeDirectCmd | (cmd & 0x3F);
    return i2c_master_transmit(dev_handle_, &sub_addr, 1, 100);
}

/* ================================================================== */
/*  FIFO                                                              */
/* ================================================================== */

esp_err_t Driver::writeFifo(const uint8_t* data, size_t len)
{
    if (len == 0) return ESP_OK;

    // Build buffer: [FIFO_LOAD sub-addr, data...]
    uint8_t buf[len + 1];
    buf[0] = kFifoLoad;
    memcpy(&buf[1], data, len);
    return i2c_master_transmit(dev_handle_, buf, len + 1, 100);
}

esp_err_t Driver::readFifo(uint8_t* data, size_t len)
{
    if (len == 0) return ESP_OK;
    uint8_t sub_addr = kFifoRead;
    return i2c_master_transmit_receive(dev_handle_, &sub_addr, 1, data, len, 100);
}

esp_err_t Driver::clearFifo()
{
    return sendCommand(CMD_CLEAR_FIFO);
}

uint16_t Driver::getFifoLength()
{
    uint8_t s1 = 0, s2 = 0;
    readRegister(REG_FIFO_STATUS1, &s1);
    readRegister(REG_FIFO_STATUS2, &s2);
    // FIFO_STATUS2 bit 0 is MSB of FIFO byte count
    return ((uint16_t)(s2 & 0x01) << 8) | s1;
}

/* ================================================================== */
/*  Memory Commands                                                   */
/* ================================================================== */

esp_err_t Driver::loadPTMemoryA(const uint8_t* data, size_t len)
{
    if (len == 0 || len > 15) return ESP_ERR_INVALID_ARG;
    uint8_t buf[16];
    buf[0] = OP_LOAD_PT_MEMORY_A;
    memcpy(&buf[1], data, len);
    return i2c_master_transmit(dev_handle_, buf, len + 1, 100);
}

/* ================================================================== */
/*  Interrupts                                                        */
/* ================================================================== */

esp_err_t Driver::getInterrupts(uint8_t* irq_main, uint8_t* irq_timer,
                                 uint8_t* irq_error, uint8_t* irq_target)
{
    // Reading IRQ registers clears them
    uint8_t buf[4] = {};
    esp_err_t ret = readRegisters(REG_IRQ_MAIN, buf, 4);
    if (ret == ESP_OK) {
        if (irq_main)   *irq_main   = buf[0];
        if (irq_timer)  *irq_timer  = buf[1];
        if (irq_error)  *irq_error  = buf[2];
        if (irq_target) *irq_target = buf[3];
        
        if (buf[0] != 0 || buf[1] != 0 || buf[2] != 0 || buf[3] != 0) {
            ESP_LOGI(TAG, "IRQ [Main:%02X Timer:%02X Err:%02X Target:%02X]", buf[0], buf[1], buf[2], buf[3]);
        }

    }
    return ret;
}

/* ================================================================== */
/*  High-level                                                        */
/* ================================================================== */

uint8_t Driver::getChipId()
{
    uint8_t id = 0;
    readRegister(REG_IC_IDENTITY, &id);
    return id;
}

esp_err_t Driver::configureTargetMode(const uint8_t* uid, size_t uid_len,
                                       const uint8_t atqa[2], uint8_t sak)
{
    // 1. Reset and Stop
    sendCommand(0x16); // CMD_STOP_ALL_ACTIVITIES
    modifyRegister(REG_OP_CONTROL, 0x00, OP_RX_EN); // set rx_en momentarily
    vTaskDelay(pdMS_TO_TICKS(2));
    sendCommand(CMD_SET_DEFAULT);
    vTaskDelay(pdMS_TO_TICKS(5));
    
    uint8_t test_cmd[3] = { static_cast<uint8_t>(kModeDirectCmd | (CMD_TEST_ACCESS & 0x3F)), 0x04, 0x10 };
    i2c_master_transmit(dev_handle_, test_cmd, 3, 100);

    // 2. IO Configuration
    writeRegister(REG_IO_CONF1, 0x27); // i2c_thd1 | MCU_CLK disabled
    writeRegister(REG_IO_CONF2, 0xA0); // sup3v | aat_en
    
    // Space B helper
    auto writeRegB = [&](uint8_t reg, uint8_t val) {
        uint8_t aux = 0; readRegister(REG_AUX, &aux);
        writeRegister(REG_AUX, aux | 0x40);
        writeRegister(reg, val);
        writeRegister(REG_AUX, aux & ~0x40);
    };

    // 3. PT Memory A (UID, ATQA, SAK)
    uint8_t pt_mem[15] = {0};
    pt_mem[0] = atqa[0];
    pt_mem[1] = atqa[1];
    pt_mem[2] = sak; // M5Unit-NFC logic: (picc.sak & ~0x04)
    for (size_t i = 0; i < uid_len && i < 10; ++i) {
        pt_mem[3 + i] = uid[i];
    }
    loadPTMemoryA(pt_mem, 15);

    modifyRegister(REG_AUX, 0x30, 0x10); // set uid_7 (0x10) if 7 bytes, assuming 7 byte here.

    // 4. Emulation A Config
    modifyRegister(REG_PASSIVE_TARGET, 0x20, 0xC0); // d_ac_ap2p | d_212_424_1r, clear d_106_ac_a (0x20)
    modifyRegister(REG_TIMER_EMV_CONTROL, 0xE0, 0x08); // mrt_step 512
    writeRegister(0x14, 0x10); // REG_MASK_RECEIVER_TIMER (0x14) = 16 (approx 100us)

    modifyRegister(REG_ISO14443A_NFC, 0xE0, 0x00); // clear no_tx_par, no_rx_par, nfc_f0

    writeRegister(REG_ANT_TUNE_A, 0x00);
    writeRegister(REG_ANT_TUNE_B, 0xFF);
    writeRegB(0x30, 0x00); // REG_OVERSHOOT_CONF1
    writeRegB(0x31, 0x00); // REG_OVERSHOOT_CONF2
    writeRegB(0x32, 0x00); // REG_UNDERSHOOT_CONF1
    writeRegB(0x33, 0x00); // REG_UNDERSHOOT_CONF2

    ESP_LOGI(TAG, "Target mode configured for auto anti-collision");
    return ESP_OK;
}

esp_err_t Driver::startTargetMode()
{
    // Unmask interrupts we care about
    writeRegister(REG_IRQ_MASK_MAIN, ~(IRQ_MAIN_RXS | IRQ_MAIN_RXE | IRQ_MAIN_TXE | IRQ_MAIN_OSC));
    writeRegister(REG_IRQ_MASK_TIMER_NFC, 0xFF); // Mask all for now
    writeRegister(REG_IRQ_MASK_ERROR_WUP, ~(0x18)); // Unmask I_eon (0x10) and I_eof (0x08)
    writeRegister(REG_IRQ_MASK_TARGET, ~(IRQ_TARGET_WU_A | IRQ_TARGET_WU_A_X));
    
    // Clear pending interrupts
    uint8_t irq[4];
    getInterrupts(&irq[0], &irq[1], &irq[2], &irq[3]);

    // Go to Off (ready to detect field)
    modifyRegister(REG_OP_CONTROL, 0x00, OP_RX_EN); // momentarily set rx_en for goto_sense
    vTaskDelay(pdMS_TO_TICKS(2));
    
    modifyRegister(REG_PASSIVE_TARGET, 0x20, 0x00); // clear d_106_ac_a (0x20)
    sendCommand(CMD_GOTO_SENSE); // ARM EXTERNAL FIELD DETECTOR
    
    modifyRegister(REG_MODE, 0xFF, 0xC8); // Target, NFC-A, Bit rate detection mode

    // Now clear rx_en and en, wait for I_eon
    modifyRegister(REG_OP_CONTROL, OP_EN | OP_RX_EN | OP_TX_EN, 0x00); 

    uint8_t aux_disp = 0;
    readRegister(0x1E, &aux_disp); // REG_AUX_DISPLAY
    if (aux_disp & 0x40) { // efd_o
        ESP_LOGI(TAG, "Target mode started (RF field already present, waking up!)");
        modifyRegister(REG_OP_CONTROL, 0x00, OP_EN | OP_RX_EN);
        vTaskDelay(pdMS_TO_TICKS(2));
        modifyRegister(REG_PASSIVE_TARGET, 0x20, 0x00);
        sendCommand(CMD_GOTO_SENSE);
        sendCommand(CMD_CLEAR_FIFO);
        sendCommand(CMD_UNMASK_RECEIVE_DATA);
    } else {
        ESP_LOGI(TAG, "Target mode started (Waiting for RF field - I_eon)");
    }
    return ESP_OK;
}


esp_err_t Driver::transmitData(const uint8_t* data, size_t len, bool with_crc)
{
    // Load data into FIFO
    esp_err_t ret = clearFifo();
    if (ret != ESP_OK) return ret;

    ret = writeFifo(data, len);
    if (ret != ESP_OK) return ret;

    // Set number of bytes to transmit
    uint8_t num_tx_hi = (len >> 4) & 0xFF;
    uint8_t num_tx_lo = (len << 4) & 0xF0;  // bits [7:4] = lower nibble, [3:0] = 0 (no partial bits)
    writeRegister(REG_NUM_TX_BYTES1, num_tx_hi);
    writeRegister(REG_NUM_TX_BYTES2, num_tx_lo);

    // Transmit
    return sendCommand(with_crc ? CMD_TX_WITH_CRC : CMD_TX_WITHOUT_CRC);
}

}  // namespace st25r3916
