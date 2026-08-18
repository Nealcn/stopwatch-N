/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 *
 * @brief 番茄钟时长设置 Worker（阶段四）：专注/休息分钟滑杆，存 NVS ns "pomodoro"
 * （app_pomodoro 每次 onOpen 读取 focus_min / break_min 生效）
 */
#include "workers.h"
#include <assets/assets.h>
#include <mooncake_log.h>
#include <hal/hal.h>
#include <utils/settings/settings.h>
#include <cstdint>
#include <cstdio>
#include <vector>

using namespace smooth_ui_toolkit::lvgl_cpp;
using namespace setup_workers;

static const std::string_view _tag = "Setup-Pomodoro";

namespace {

constexpr const char* kNsPomodoro  = "pomodoro";
constexpr const char* kFocusKey    = "focus_min";
constexpr const char* kBreakKey    = "break_min";
constexpr int kFocusMin = 25;   // 专注分钟范围
constexpr int kFocusMax = 90;
constexpr int kBreakMin = 5;    // 休息分钟范围
constexpr int kBreakMax = 30;
constexpr int kStep     = 5;

int load_min(const char* key, int fallback)
{
    return Settings(kNsPomodoro).GetInt(key, fallback);
}

}  // namespace

namespace setup_workers {

class PomodoroWorker::PomodoroConfigView {
public:
    PomodoroConfigView(int focusMin, int breakMin)
        : _focus(focusMin), _break(breakMin)
    {
        _panel = std::make_unique<Container>(lv_screen_active());
        _panel->align(LV_ALIGN_CENTER, 0, 0);
        _panel->setSize(466, 466);
        _panel->setRadius(0);
        _panel->setBorderWidth(0);
        _panel->setPaddingAll(0);
        _panel->setBgColor(lv_color_hex(0x000000));
        _panel->setBgOpa(LV_OPA_COVER);
        _panel->removeFlag(LV_OBJ_FLAG_SCROLLABLE);

        createSliderRow(96, "专注 (分钟)", _focus, kFocusMin, kFocusMax,
                        [this](int v) { _focus = v; });
        createSliderRow(212, "休息 (分钟)", _break, kBreakMin, kBreakMax,
                        [this](int v) { _break = v; });

        _ok_button = std::make_unique<Button>(_panel->get());
        _ok_button->align(LV_ALIGN_CENTER, 0, 175);
        _ok_button->setSize(374, 130);
        _ok_button->setRadius(77);
        _ok_button->setBorderWidth(0);
        _ok_button->setShadowWidth(0);
        _ok_button->setBgColor(lv_color_hex(0x4AD78C));
        _ok_button->label().setText("OK");
        _ok_button->label().setTextFont(&lv_font_montserrat_28);
        _ok_button->label().setTextColor(lv_color_hex(0x0F5831));
        _ok_button->label().align(LV_ALIGN_CENTER, 0, 0);
        _ok_button->onClick().connect([this]() { _save_requested = true; });
    }

    int focusMin() const { return _focus; }
    int breakMin() const { return _break; }

    bool consumeSaveRequested()
    {
        bool requested  = _save_requested;
        _save_requested = false;
        return requested;
    }

private:
    void createSliderRow(int y, const char* title, int initialValue, int minValue, int maxValue,
                         const std::function<void(int)>& onChanged)
    {
        auto row = std::make_unique<Container>(_panel->get());
        row->setSize(374, 88);
        row->align(LV_ALIGN_TOP_MID, 0, y);
        row->setBorderWidth(0);
        row->setShadowWidth(0);
        row->setRadius(44);
        row->setPaddingAll(0);
        row->setBgColor(lv_color_hex(0x2A2A2A));
        row->setBgOpa(LV_OPA_COVER);

        auto label = std::make_unique<Label>(row->get());
        label->setText(title);
        label->setTextFont(&lv_font_montserrat_24);
        label->setTextColor(lv_color_hex(0xFFFFFF));
        label->align(LV_ALIGN_LEFT_MID, 24, -12);

        auto value = std::make_unique<Label>(row->get());
        char buf[8];
        std::snprintf(buf, sizeof(buf), "%d", initialValue);
        value->setText(buf);
        value->setTextFont(&CommissionerMedium64);
        value->setTextColor(lv_color_hex(0x4AD78C));
        value->align(LV_ALIGN_LEFT_MID, 24, 26);

        auto slider = std::make_unique<Slider>(row->get());
        slider->align(LV_ALIGN_RIGHT_MID, -24, 0);
        slider->setSize(200, 24);
        slider->setRange(minValue, maxValue, false);
        slider->setValue(initialValue);
        slider->setBgColor(lv_color_hex(0x3A3A3A), LV_PART_MAIN);
        slider->setBgOpa(LV_OPA_COVER, LV_PART_MAIN);
        slider->setBorderWidth(0, LV_PART_MAIN);
        slider->setRadius(LV_RADIUS_CIRCLE, LV_PART_MAIN);
        slider->setBgColor(lv_color_hex(0x4AD78C), LV_PART_INDICATOR);
        slider->setBgOpa(LV_OPA_COVER, LV_PART_INDICATOR);
        slider->setBorderWidth(0, LV_PART_INDICATOR);
        slider->setRadius(LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(slider->get(), lv_color_hex(0xFFFFFF), LV_PART_KNOB);
        lv_obj_set_style_bg_opa(slider->get(), LV_OPA_COVER, LV_PART_KNOB);
        lv_obj_set_style_border_width(slider->get(), 0, LV_PART_KNOB);
        lv_obj_set_style_radius(slider->get(), LV_RADIUS_CIRCLE, LV_PART_KNOB);
        slider->onValueChanged().connect([this, slider_raw = slider.get(), value_raw = value.get(), minValue, maxValue, onChanged](int32_t v) {
            // 按 step 吸附
            int snapped = minValue + ((v - minValue + kStep / 2) / kStep) * kStep;
            if (snapped > maxValue) {
                snapped = maxValue;
            }
            if (snapped != v) {
                slider_raw->setValue(snapped);
            }
            char buf[8];
            std::snprintf(buf, sizeof(buf), "%d", snapped);
            value_raw->setText(buf);
            onChanged(snapped);
        });

        _rows.push_back(std::move(row));
        _labels.push_back(std::move(label));
        _values.push_back(std::move(value));
        _sliders.push_back(std::move(slider));
    }

    std::unique_ptr<Container> _panel;
    std::vector<std::unique_ptr<Container>> _rows;
    std::vector<std::unique_ptr<Label>> _labels;
    std::vector<std::unique_ptr<Label>> _values;
    std::vector<std::unique_ptr<Slider>> _sliders;
    std::unique_ptr<Button> _ok_button;
    int _focus = kFocusMin;
    int _break = kBreakMin;
    bool _save_requested = false;
};

}  // namespace setup_workers

PomodoroWorker::PomodoroWorker()
{
    mclog::tagInfo(_tag, "start pomodoro worker");

    _view = std::make_unique<PomodoroConfigView>(load_min(kFocusKey, kFocusMin),
                                                 load_min(kBreakKey, kBreakMin));
}

void PomodoroWorker::update()
{
    if (_view && _view->consumeSaveRequested()) {
        Settings(kNsPomodoro).SetInt(kFocusKey, _view->focusMin());
        Settings(kNsPomodoro).SetInt(kBreakKey, _view->breakMin());
        mclog::tagInfo(_tag, "pomodoro saved: focus={}min break={}min",
                       _view->focusMin(), _view->breakMin());
        _is_done = true;
    }
}

PomodoroWorker::~PomodoroWorker()
{
}
