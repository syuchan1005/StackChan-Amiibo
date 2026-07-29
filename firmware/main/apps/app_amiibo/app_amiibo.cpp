/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "app_amiibo.h"
#include "nfc_emulation.h"
#include "amiibo_list.h"
#include <hal/hal.h>
#include <hal/board/hal_bridge.h>
#include <mooncake.h>
#include <mooncake_log.h>
#include <assets/assets.h>
#include <lvgl.h>
#include <smooth_lvgl.hpp>
#include <memory>
#include <vector>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>

using namespace mooncake;
using namespace smooth_ui_toolkit::lvgl_cpp;

static std::unique_ptr<nfc_emu::Emulator> _emulator;

static std::unique_ptr<Button> _button_quit;
static std::unique_ptr<Label>  _label_status;
static lv_obj_t* _header_obj = nullptr;
static lv_obj_t* _amiibo_list = nullptr;
static std::vector<lv_obj_t*> _list_buttons;

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
            _label_status->setText("NFC: Stopped");
            break;
        case nfc_emu::Status::Listening:
            _label_status->setText("NFC: Wait...");
            break;
        case nfc_emu::Status::Selected:
            _label_status->setText("NFC: Reading!");
            break;
        case nfc_emu::Status::Error:
            _label_status->setText("NFC: Error!");
            break;
    }
}

static void list_btn_event_cb(lv_event_t * e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * btn = (lv_obj_t*)lv_event_get_current_target(e);
    if(code == LV_EVENT_CLICKED) {
        size_t index = (size_t)lv_event_get_user_data(e);
        
        LvglLockGuard lock;
        for (auto b : _list_buttons) {
            if (lv_obj_is_valid(b)) {
                lv_obj_remove_state(b, LV_STATE_CHECKED);
            }
        }
        if (lv_obj_is_valid(btn)) {
            lv_obj_add_state(btn, LV_STATE_CHECKED);
        }
        
        if (_emulator) {
            const AmiiboFile& file = amiibo_files[index];
            _emulator->setAmiiboData(file.start, file.size);
            // Let onRunning() handle the UI status updates dynamically based on getStatus()
        }
    }
}

void AppAmiibo::onOpen()
{
    mclog::tagInfo(getAppInfo().name, "on open");

    {
        LvglLockGuard lock;

        // Header Container
        _header_obj = lv_obj_create(lv_screen_active());
        lv_obj_set_size(_header_obj, LV_PCT(100), 40);
        lv_obj_align(_header_obj, LV_ALIGN_TOP_MID, 0, 0);
        lv_obj_set_style_bg_color(_header_obj, lv_color_hex(0xE65100), 0); // Orange
        lv_obj_set_style_border_width(_header_obj, 0, 0);
        lv_obj_set_style_radius(_header_obj, 0, 0);
        lv_obj_set_style_pad_all(_header_obj, 0, 0);
        lv_obj_set_style_pad_left(_header_obj, 10, 0);
        lv_obj_set_style_pad_right(_header_obj, 10, 0);

        _label_status = std::make_unique<Label>(_header_obj);
        _label_status->align(LV_ALIGN_LEFT_MID, 0, 0);
        _label_status->setText("NFC: Init...");
        lv_obj_set_style_text_color(_label_status->get(), lv_color_hex(0xFFFFFF), 0); // White text

        _button_quit = std::make_unique<Button>(_header_obj);
        _button_quit->align(LV_ALIGN_RIGHT_MID, 0, 0);
        _button_quit->setSize(60, 30);
        _button_quit->label().setText("Quit");
        _button_quit->onClick().connect([this]() {
            close();
        });

        // Create list container using flex box
        _amiibo_list = lv_obj_create(lv_screen_active());
        lv_obj_set_size(_amiibo_list, LV_PCT(100), 200); // Remaining 200 pixels
        lv_obj_align(_amiibo_list, LV_ALIGN_BOTTOM_MID, 0, 0);
        lv_obj_set_flex_flow(_amiibo_list, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_bg_color(_amiibo_list, lv_color_hex(0xFFFFFF), 0); // White list background
        lv_obj_set_style_border_width(_amiibo_list, 0, 0);
        lv_obj_set_style_radius(_amiibo_list, 0, 0);
        lv_obj_set_style_pad_all(_amiibo_list, 5, 0);
        
        _list_buttons.clear();
        for (size_t i = 0; i < amiibo_files_count; i++) {
            lv_obj_t * btn = lv_button_create(_amiibo_list);
            lv_obj_set_width(btn, LV_PCT(100));
            lv_obj_add_flag(btn, LV_OBJ_FLAG_CHECKABLE); // Allow CHECKED state
            lv_obj_add_event_cb(btn, list_btn_event_cb, LV_EVENT_CLICKED, (void*)i);
            
            lv_obj_t * label = lv_label_create(btn);
            lv_label_set_text(label, amiibo_files[i].name);
            lv_obj_set_style_text_color(label, lv_color_hex(0x000000), 0); // Black text for contrast
            lv_obj_align(label, LV_ALIGN_LEFT_MID, 10, 0);
            
            _list_buttons.push_back(btn);
            
            // Select first item by default
            if (i == 0) {
                lv_obj_add_state(btn, LV_STATE_CHECKED);
            }
        }
    }

    _emulator = std::make_unique<nfc_emu::Emulator>();

    auto internal_bus = hal_bridge::board_get_i2c_bus();
    if (!_emulator->init(internal_bus)) {
        mclog::tagError(getAppInfo().name, "Emulator init failed");
        _last_status = nfc_emu::Status::Error;
        updateStatusLabel(_last_status);
        return;
    }

    if (amiibo_files_count > 0) {
        _emulator->setAmiiboData(amiibo_files[0].start, amiibo_files[0].size);
        _last_status = nfc_emu::Status::Listening;
        updateStatusLabel(_last_status);
    } else {
        LvglLockGuard lock;
        _last_status = nfc_emu::Status::Error;
        updateStatusLabel(_last_status);
    }
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
    _label_status.reset();
    _button_quit.reset();
    
    if (lv_obj_is_valid(_header_obj)) {
        lv_obj_delete(_header_obj);
        _header_obj = nullptr;
    }
    
    if (lv_obj_is_valid(_amiibo_list)) {
        lv_obj_delete(_amiibo_list);
        _amiibo_list = nullptr;
    }
    _list_buttons.clear();
}

