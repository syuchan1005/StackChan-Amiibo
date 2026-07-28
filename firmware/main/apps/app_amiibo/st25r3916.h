/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <cstdint>
#include <cstddef>
#include "driver/i2c_master.h"
#include "esp_err.h"

/**
 * @brief Minimal ST25R3916 driver for NFC-A target (card emulation) mode
 *        using ESP-IDF I2C master API.
 *
 * I2C sub-address byte format:
 *   [7:6] Mode: 00=RegWrite, 01=RegRead, 10=FIFO, 11=DirectCmd
 *   [5:0] Register address / FIFO direction / Command code
 */
namespace st25r3916 {

/* ── I2C Configuration ─────────────────────────────────────── */
static constexpr uint8_t  kI2cAddr      = 0x50;   // 7-bit default
static constexpr int      kSdaPin       = 2;      // Port A SDA (CoreS3)
static constexpr int      kSclPin       = 1;      // Port A SCL (CoreS3)
static constexpr uint32_t kI2cFreqHz    = 100000;  // 100 kHz

/* ── Sub-address mode bits ─────────────────────────────────── */
static constexpr uint8_t kModeRegWrite  = 0x00;   // 00xxxxxx
static constexpr uint8_t kModeRegRead   = 0x40;   // 01xxxxxx
static constexpr uint8_t kModeFifo      = 0x80;   // 10xxxxxx
static constexpr uint8_t kModeDirectCmd = 0xC0;   // 11xxxxxx

static constexpr uint8_t kFifoLoad      = 0x80;   // 10|000000 = write to FIFO
static constexpr uint8_t kFifoRead      = 0x9F;   // 10|011111 = read from FIFO

/* ── Registers ─────────────────────────────────────────────── */
static constexpr uint8_t REG_IO_CONF1             = 0x00;
static constexpr uint8_t REG_IO_CONF2             = 0x01;
static constexpr uint8_t REG_OP_CONTROL           = 0x02;
static constexpr uint8_t REG_MODE                 = 0x03;
static constexpr uint8_t REG_BIT_RATE             = 0x04;
static constexpr uint8_t REG_ISO14443A_NFC        = 0x05;
static constexpr uint8_t REG_ISO14443B_1          = 0x06;
static constexpr uint8_t REG_ISO14443B_2          = 0x07;
static constexpr uint8_t REG_PASSIVE_TARGET       = 0x08;
static constexpr uint8_t REG_STREAM_MODE          = 0x09;
static constexpr uint8_t REG_AUX                  = 0x0A;
static constexpr uint8_t REG_RCVR_CONF1           = 0x0B;
static constexpr uint8_t REG_RCVR_CONF2           = 0x0C;
static constexpr uint8_t REG_RCVR_CONF3           = 0x0D;
static constexpr uint8_t REG_RCVR_CONF4           = 0x0E;

static constexpr uint8_t REG_TIMER_EMV_CONTROL    = 0x12;

static constexpr uint8_t REG_IRQ_MASK_MAIN        = 0x16;
static constexpr uint8_t REG_IRQ_MASK_TIMER_NFC   = 0x17;
static constexpr uint8_t REG_IRQ_MASK_ERROR_WUP   = 0x18;
static constexpr uint8_t REG_IRQ_MASK_TARGET      = 0x19;
static constexpr uint8_t REG_IRQ_MAIN             = 0x1A;
static constexpr uint8_t REG_IRQ_TIMER_NFC        = 0x1B;
static constexpr uint8_t REG_IRQ_ERROR_WUP        = 0x1C;
static constexpr uint8_t REG_IRQ_TARGET           = 0x1D;
static constexpr uint8_t REG_FIFO_STATUS1         = 0x1E;
static constexpr uint8_t REG_FIFO_STATUS2         = 0x1F;

static constexpr uint8_t REG_NUM_TX_BYTES1        = 0x22;
static constexpr uint8_t REG_NUM_TX_BYTES2        = 0x23;

static constexpr uint8_t REG_ANT_TUNE_A           = 0x26;
static constexpr uint8_t REG_ANT_TUNE_B           = 0x27;
static constexpr uint8_t REG_TX_DRIVER            = 0x28;
static constexpr uint8_t REG_PT_MODULATION        = 0x29;
static constexpr uint8_t REG_EXT_FIELD_ACTIVATION = 0x2A;
static constexpr uint8_t REG_EXT_FIELD_DEACTIVATION = 0x2B;
static constexpr uint8_t REG_IC_IDENTITY          = 0x3F;

// Space B Registers (Access by setting REG_AUX bit 6)
static constexpr uint8_t REG_EMD_SUPPRESSION      = 0x05;
static constexpr uint8_t REG_RESISTIVE_AM         = 0x2A;
static constexpr uint8_t REG_OVERSHOOT_CONF1      = 0x30;
static constexpr uint8_t REG_OVERSHOOT_CONF2      = 0x31;
static constexpr uint8_t REG_UNDERSHOOT_CONF1     = 0x32;
static constexpr uint8_t REG_UNDERSHOOT_CONF2     = 0x33;

static constexpr uint8_t OP_LOAD_PT_MEMORY_A      = 0xA0;

/* ── Interrupt flag bits ───────────────────────────────────── */
// IRQ_MAIN (0x1A)
static constexpr uint8_t IRQ_MAIN_OSC             = 0x80;
static constexpr uint8_t IRQ_MAIN_FWL             = 0x40;  // FIFO water level
static constexpr uint8_t IRQ_MAIN_RXS             = 0x20;  // Rx start
static constexpr uint8_t IRQ_MAIN_RXE             = 0x10;  // Rx end
static constexpr uint8_t IRQ_MAIN_TXE             = 0x08;  // Tx end

// IRQ_TARGET (0x1D)
static constexpr uint8_t IRQ_TARGET_WU_A          = 0x01;  // NFC-A target wakeup (REQA/WUPA)
static constexpr uint8_t IRQ_TARGET_WU_A_X        = 0x02;  // NFC-A target wakeup extended


/* ── Direct Commands ───────────────────────────────────────── */
static constexpr uint8_t CMD_SET_DEFAULT          = 0x01;
static constexpr uint8_t CMD_CLEAR                = 0x02;
static constexpr uint8_t CMD_CLEAR_FIFO           = 0x04;
static constexpr uint8_t CMD_TX_WITH_CRC          = 0x05;
static constexpr uint8_t CMD_TX_WITHOUT_CRC       = 0x06;
static constexpr uint8_t CMD_TX_REQA              = 0x07;
static constexpr uint8_t CMD_TX_WUPA              = 0x08;
static constexpr uint8_t CMD_NFC_INITIAL_FIELD_ON = 0x09;
static constexpr uint8_t CMD_NFC_RESPONSE_FIELD_ON = 0x0A;
static constexpr uint8_t CMD_GOTO_SENSE           = 0x0C;
static constexpr uint8_t CMD_GOTO_SLEEP           = 0x0D;
static constexpr uint8_t CMD_MASK_RECEIVE_DATA    = 0x10;
static constexpr uint8_t CMD_UNMASK_RECEIVE_DATA  = 0x11;
static constexpr uint8_t CMD_CALIBRATE_ANTENNA    = 0x1A;
static constexpr uint8_t CMD_ADJUST_REGULATORS    = 0x1B;
static constexpr uint8_t CMD_MEASURE_AMPLITUDE    = 0x13;
static constexpr uint8_t CMD_RESET_RX_GAIN        = 0x12;
static constexpr uint8_t CMD_TEST_ACCESS          = 0xFC;

/* ── Passive Target register bits ──────────────────────────── */
static constexpr uint8_t PT_D_106_A_LISTEN        = 0x80;  // Enable NFC-A 106kbps listen
static constexpr uint8_t PT_D_212_F_LISTEN        = 0x40;  // Enable NFC-F 212kbps listen
static constexpr uint8_t PT_D_424_F_LISTEN        = 0x20;  // Enable NFC-F 424kbps listen

/* ── Mode register values ──────────────────────────────────── */
static constexpr uint8_t MODE_TARGET_NFCA         = 0x81;  // Target mode, NFC-A (106 kbps)
static constexpr uint8_t MODE_INITIATOR_NFCA      = 0x08;  // Initiator, ISO14443A

/* ── Operation Control bits ─────────────────────────────────── */
static constexpr uint8_t OP_EN                    = 0x80;  // Enable
static constexpr uint8_t OP_RX_EN                 = 0x40;  // Rx enable
static constexpr uint8_t OP_TX_EN                 = 0x08;  // Tx enable

/**
 * @brief ST25R3916 Driver class
 */
class Driver {
public:
    Driver()  = default;
    ~Driver();

    /**
     * @brief Initialize by creating a new I2C bus on Port A (GPIO2/1)
     * @return ESP_OK on success
     */
    esp_err_t init();

    /**
     * @brief Initialize using an existing I2C bus handle (e.g. internal bus)
     * @param bus_handle  Existing I2C master bus handle
     * @param addr        7-bit I2C address to try (0 = auto-scan)
     * @return ESP_OK on success
     */
    esp_err_t initWithBus(i2c_master_bus_handle_t bus_handle, uint8_t addr = 0);

    /**
     * @brief Deinitialize and release I2C bus
     */
    void deinit();

    /**
     * @brief Check if initialized
     */
    bool isInitialized() const { return initialized_; }

    /* ── Register access ────────────────────────────────────── */
    esp_err_t readRegister(uint8_t reg, uint8_t* value);
    esp_err_t writeRegister(uint8_t reg, uint8_t value);
    esp_err_t readRegisters(uint8_t reg, uint8_t* buf, size_t len);
    esp_err_t modifyRegister(uint8_t reg, uint8_t clear_mask, uint8_t set_mask);

    /* ── Direct commands ────────────────────────────────────── */
    esp_err_t sendCommand(uint8_t cmd);

    /* ── FIFO ───────────────────────────────────────────────── */
    esp_err_t writeFifo(const uint8_t* data, size_t len);
    esp_err_t readFifo(uint8_t* data, size_t len);
    esp_err_t clearFifo();
    uint16_t  getFifoLength();

    /* ── Memory Commands ────────────────────────────────────── */
    esp_err_t loadPTMemoryA(const uint8_t* data, size_t len);

    /* ── Interrupts ─────────────────────────────────────────── */
    /**
     * @brief Read and clear all interrupt registers
     * @param irq_main   Output: main interrupt flags
     * @param irq_timer  Output: timer/NFC interrupt flags
     * @param irq_error  Output: error/wakeup interrupt flags
     * @param irq_target Output: target interrupt flags
     */
    esp_err_t getInterrupts(uint8_t* irq_main, uint8_t* irq_timer,
                            uint8_t* irq_error, uint8_t* irq_target);

    /* ── High-level ─────────────────────────────────────────── */
    uint8_t getChipId();

    /**
     * @brief Configure for NFC-A passive target (card emulation) mode
     * @param uid  UID bytes (4 or 7 bytes)
     * @param uid_len  Length of UID (4 or 7)
     * @param atqa  ATQA response (2 bytes)
     * @param sak   SAK value
     * @return ESP_OK on success
     */
    esp_err_t configureTargetMode(const uint8_t* uid, size_t uid_len,
                                   const uint8_t atqa[2], uint8_t sak);

    /**
     * @brief Start listening for external RF field (target mode)
     */
    esp_err_t startTargetMode();

    /**
     * @brief Transmit data via FIFO (with or without CRC)
     * @param data  Data to transmit
     * @param len   Length in bytes
     * @param with_crc  Append CRC-A
     */
    esp_err_t transmitData(const uint8_t* data, size_t len, bool with_crc);

private:
    esp_err_t tryAddress(uint8_t addr);

    i2c_master_bus_handle_t bus_handle_  = nullptr;
    i2c_master_dev_handle_t dev_handle_ = nullptr;
    bool owns_bus_    = false;  // true if we created the bus and must delete it
    bool initialized_ = false;
    uint8_t device_addr_ = 0;  // Actual address found
};

}  // namespace st25r3916
