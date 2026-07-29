/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "app_amiibo.h"
#include "nfc_emulation.h"
#include <hal/hal.h>
#include <hal/board/hal_bridge.h>
#include <mooncake.h>
#include <mooncake_log.h>
#include <assets/assets.h>
#include <smooth_lvgl.hpp>
#include <memory>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>

using namespace mooncake;
using namespace smooth_ui_toolkit::lvgl_cpp;

static std::unique_ptr<nfc_emu::Emulator> _emulator;

static std::unique_ptr<Button> _button_quit;
static std::unique_ptr<Label>  _label_title;
static std::unique_ptr<Label>  _label_status;
static std::unique_ptr<Label>  _label_url;

static nfc_emu::Status _last_status = nfc_emu::Status::Idle;

AppAmiibo::AppAmiibo()
{
    setAppInfo().name = "AMIIBO";
    static auto icon  = assets::get_image("icon_app_center.bin");
    setAppInfo().icon = (void*)&icon;
    static uint32_t theme_color = 0xE65100;
    setAppInfo().userData       = (void*)&theme_color;
}

void AppAmiibo::onCreate()
{
    mclog::tagInfo(getAppInfo().name, "on create");
}

static void updateStatusLabel(nfc_emu::Status status)
{
    if (!_label_status) return;

    LvglLockGuard lock;

    switch (status) {
        case nfc_emu::Status::Idle:
            _label_status->setText("Status: Stopped");
            break;
        case nfc_emu::Status::Listening:
            _label_status->setText("Status: Waiting for NFC...");
            break;
        case nfc_emu::Status::Selected:
            _label_status->setText("Status: Reader detected!");
            break;
        case nfc_emu::Status::Error:
            _label_status->setText("Status: Error!");
            break;
    }
}

extern const uint8_t inkling_boy_bin_start[] asm("_binary_Inkling_Boy_bin_start");
extern const uint8_t inkling_boy_bin_end[] asm("_binary_Inkling_Boy_bin_end");

void AppAmiibo::onOpen()
{
    mclog::tagInfo(getAppInfo().name, "on open");

    {
        LvglLockGuard lock;

        _label_title = std::make_unique<Label>(lv_screen_active());
        _label_title->align(LV_ALIGN_TOP_MID, 0, 20);
        _label_title->setText("NFC Emulation");

        _label_status = std::make_unique<Label>(lv_screen_active());
        _label_status->align(LV_ALIGN_CENTER, 0, -30);
        _label_status->setText("Initializing...");

        _label_url = std::make_unique<Label>(lv_screen_active());
        _label_url->align(LV_ALIGN_CENTER, 0, 0);
        _label_url->setText("Inkling Boy.bin");

        _button_quit = std::make_unique<Button>(lv_screen_active());
        _button_quit->align(LV_ALIGN_BOTTOM_MID, 0, -20);
        _button_quit->label().setText("QUIT");
        _button_quit->onClick().connect([this]() {
            close();
        });
    }

    _emulator = std::make_unique<nfc_emu::Emulator>();

    auto internal_bus = hal_bridge::board_get_i2c_bus();
    size_t bin_size = inkling_boy_bin_end - inkling_boy_bin_start;
    if (!_emulator->init(internal_bus, inkling_boy_bin_start, bin_size)) {
        mclog::tagError(getAppInfo().name, "Emulator init failed");
        _last_status = nfc_emu::Status::Error;
        updateStatusLabel(_last_status);
        return;
    }

    if (!_emulator->start()) {
        mclog::tagError(getAppInfo().name, "Emulator start failed");
        _last_status = nfc_emu::Status::Error;
        updateStatusLabel(_last_status);
        return;
    }

    _last_status = nfc_emu::Status::Listening;
    updateStatusLabel(_last_status);
    mclog::tagInfo(getAppInfo().name, "NFC emulation started: Inkling_Boy.bin");
}

void AppAmiibo::onRunning()
{
    if (!_emulator) return;

    _emulator->update();

    nfc_emu::Status status = _emulator->getStatus();

    if (status != _last_status) {
        _last_status = status;
        updateStatusLabel(status);
    }
}

void AppAmiibo::onClose()
{
    mclog::tagInfo(getAppInfo().name, "on close");

    if (_emulator) {
        _emulator->stop();
        _emulator.reset();
    }
    
    LvglLockGuard lock;
    _label_title.reset();
    _label_status.reset();
    _label_url.reset();
    _button_quit.reset();
}
