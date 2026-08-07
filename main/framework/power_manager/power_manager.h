/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <cstdint>
#include <functional>

/**
 * @brief 电源后台常驻线程（文档 3.1.1：电量监测、闲置降频、分级休眠、自动唤醒）
 *
 * 现状（见《源码摸底报告》5.3）：
 *  - 电量采样已有独立线程（hal_pmic.cpp bat_reading_task，1Hz + 滑动滤波），直接复用并入
 *  - 全工程无 esp_pm / esp_sleep 调用；sdkconfig 无 CONFIG_PM_ENABLE（需补）
 *  - M5PM1 硬件支持 timerSet（RTC 定时唤醒）与 getWakeSource（唤醒源查询）
 *  - 深睡前置操作：ioe_speaker_enable(false) + stopLvglUpdate() + codec close
 *
 * 实现计划（阶段二）：
 *  - FreeRTOS task（优先级 1-3，不抢占 UI），消费 getBatteryLevel/isBatteryCharging
 *  - 闲置计时：主循环经 onIdleTick() 上报，超阈值分级降频/休眠
 *  - 唤醒：M5PM1 timerSet + getWakeSource；深睡恢复走 esp_restart()
 */
namespace framework {

struct PowerEvent {
    enum Type : uint8_t { LowBattery, ChargingStarted, ChargingStopped, BeforeSleep, AfterWake };
    Type type;
    uint8_t batteryLevel;
};

class PowerManager {
public:
    using EventCallback = std::function<void(const PowerEvent&)>;

    static PowerManager& get();

    /* 启动后台线程（并入现有电量采样，幂等） */
    void start();
    void stop();

    /* 主循环节拍：上报一次用户活动，重置闲置计时 */
    void onUserActivity();

    /* 直接状态查询（转发 HAL） */
    uint8_t getBatteryLevel() const;
    bool isCharging() const;

    void setEventCallback(EventCallback cb);

private:
    PowerManager()          = default;
    ~PowerManager()         = default;
    PowerManager(const PowerManager&)            = delete;
    PowerManager& operator=(const PowerManager&) = delete;
};

}  // namespace framework
