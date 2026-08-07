/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "power_manager.h"
#include <mooncake_log.h>

// TODO(阶段二): FreeRTOS 任务 + 并入 bat_reading_task + 降频/分级休眠/定时唤醒挂接
// 当前为骨架桩实现。

namespace framework {

PowerManager& PowerManager::get()
{
    static PowerManager _instance;
    return _instance;
}

void PowerManager::start()
{
    // TODO: xTaskCreate 后台任务（优先级 1-3）
    mclog::tagWarn("PowerManager", "start: stub, not implemented yet");
}

void PowerManager::stop()
{
    // TODO
}

void PowerManager::onUserActivity()
{
    // TODO: 重置闲置计时
}

uint8_t PowerManager::getBatteryLevel() const
{
    return 0;
}

bool PowerManager::isCharging() const
{
    return false;
}

void PowerManager::setEventCallback(EventCallback cb)
{
    // TODO
}

}  // namespace framework
