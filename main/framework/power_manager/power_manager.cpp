/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "power_manager.h"
#include <hal/hal.h>
#include <mooncake_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <mutex>

// 电源后台常驻线程（阶段二实现）
//
// 当前覆盖（分级休眠第一级）：
//  - 后台 FreeRTOS 任务（优先级 2，低于 UI 线程）
//  - 电量/充电状态监测与事件回调（电量采样本身复用 hal_pmic 既有线程，本组件只消费）
//  - 闲置关屏：超过阈值无输入（按键/触屏）自动关闭背光，输入即唤醒
//
// 后续阶段（阶段四功耗深度优化）：
//  - CPU 降频（需 sdkconfig 开 CONFIG_PM_ENABLE + esp_pm_configure）
//  - 深度休眠（M5PM1 timerSet 定时唤醒 + getWakeSource，深睡恢复走 esp_restart）

namespace framework {

namespace {

constexpr const char* _tag                = "PowerManager";
constexpr uint32_t _sample_period_ms      = 1000;       // 监测周期
constexpr uint32_t _screen_off_idle_ms    = 60 * 1000;  // 闲置关屏阈值
constexpr uint8_t _low_battery_threshold  = 15;         // 低电量阈值（%）

std::mutex _state_mutex;
uint32_t _last_activity_ms   = 0;
bool _screen_off             = false;
bool _keep_awake             = false;  // 前台 App 请求保持常亮（AI 对话等）
uint8_t _saved_backlight     = 0;
bool _was_charging           = false;
bool _low_battery_notified   = false;
PowerManager::EventCallback _event_callback = nullptr;
TaskHandle_t _task_handle    = nullptr;

void notify(PowerEvent::Type type, uint8_t batteryLevel)
{
    if (_event_callback) {
        _event_callback(PowerEvent{type, batteryLevel});
    }
}

bool has_input_activity()
{
    return GetHAL().btnA.wasPressed() || GetHAL().btnB.wasPressed() || GetHAL().btnPwr.wasPressed() ||
           GetHAL().getTouchPoint().num > 0;
}

void mark_activity(uint32_t now)
{
    std::lock_guard<std::mutex> lock(_state_mutex);
    _last_activity_ms = now;
    if (_screen_off) {
        _screen_off = false;
        GetHAL().setBackLightBrightness(_saved_backlight, false);
        notify(PowerEvent::AfterWake, GetHAL().getBatteryLevel());
        mclog::tagInfo(_tag, "screen wake");
    }
}

void power_task(void* arg)
{
    mclog::tagInfo(_tag, "power manager task started");
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(_sample_period_ms));
        uint32_t now = GetHAL().millis();

        // 电量与充电事件
        uint8_t level   = GetHAL().getBatteryLevel();
        bool charging   = GetHAL().isBatteryCharging();
        if (charging != _was_charging) {
            _was_charging = charging;
            notify(charging ? PowerEvent::ChargingStarted : PowerEvent::ChargingStopped, level);
        }
        if (!charging && level <= _low_battery_threshold && !_low_battery_notified) {
            _low_battery_notified = true;
            notify(PowerEvent::LowBattery, level);
            mclog::tagWarn(_tag, "low battery: {}%", level);
        }
        if (level > _low_battery_threshold) {
            _low_battery_notified = false;
        }

        // 输入活动检测：重置闲置计时 / 唤醒屏幕
        if (has_input_activity()) {
            mark_activity(now);
            continue;
        }

        // 闲置关屏（keep_awake 时跳过：AI 对话等前台语音 App 常亮）
        std::lock_guard<std::mutex> lock(_state_mutex);
        if (!_keep_awake && !_screen_off && now - _last_activity_ms > _screen_off_idle_ms) {
            _saved_backlight = GetHAL().getBackLightBrightness();
            GetHAL().setBackLightBrightness(0, false);
            _screen_off = true;
            notify(PowerEvent::BeforeSleep, level);
            mclog::tagInfo(_tag, "screen off (idle)");
        }
    }
}

}  // namespace

PowerManager& PowerManager::get()
{
    static PowerManager _instance;
    return _instance;
}

void PowerManager::start()
{
    if (_task_handle != nullptr) {
        return;
    }
    std::lock_guard<std::mutex> lock(_state_mutex);
    _last_activity_ms = GetHAL().millis();
    if (xTaskCreate(power_task, "power_manager", 4096, nullptr, 2, &_task_handle) != pdPASS) {
        _task_handle = nullptr;
        mclog::tagError(_tag, "failed to create task");
    }
}

void PowerManager::stop()
{
    if (_task_handle != nullptr) {
        vTaskDelete(_task_handle);
        _task_handle = nullptr;
    }
}

void PowerManager::onUserActivity()
{
    mark_activity(GetHAL().millis());
}

void PowerManager::setKeepAwake(bool keep)
{
    std::lock_guard<std::mutex> lock(_state_mutex);
    _keep_awake = keep;
    if (keep) {
        _last_activity_ms = GetHAL().millis();
    }
    mclog::tagInfo(_tag, "keep awake: {}", keep);
}

uint8_t PowerManager::getBatteryLevel() const
{
    return GetHAL().getBatteryLevel();
}

bool PowerManager::isCharging() const
{
    return GetHAL().isBatteryCharging();
}

void PowerManager::setEventCallback(EventCallback cb)
{
    _event_callback = std::move(cb);
}

}  // namespace framework
