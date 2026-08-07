/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "wifi_manager.h"
#include <mooncake_log.h>

// TODO(阶段二): 抽取 config_ap 的 WiFi 栈初始化与 AP 脚手架；接入 esp-wifi-connect 或自实现 STA 连接
// 当前为骨架桩实现。

namespace framework {

WifiManager& WifiManager::get()
{
    static WifiManager _instance;
    return _instance;
}

void WifiManager::init()
{
    // TODO: esp_netif_init / esp_event_loop_create_default / esp_wifi_init（从 config_ap.cpp 抽取）
    mclog::tagWarn("WifiManager", "init: stub, not implemented yet");
}

bool WifiManager::startAp(const char* ssid, const char* password)
{
    // TODO: AP 模式 + DHCP + captive portal DNS（复用 config_ap.cpp:152-213 脚手架）
    mclog::tagWarn("WifiManager", "startAp: stub, not implemented yet");
    return false;
}

bool WifiManager::stopAp()
{
    // TODO
    return false;
}

bool WifiManager::connectSta(const char* ssid, const char* password, uint32_t timeoutMs)
{
    // TODO: STA 连接 + 凭据持久化到 NVS（ns "wifi"）
    mclog::tagWarn("WifiManager", "connectSta: stub, not implemented yet");
    return false;
}

void WifiManager::disconnect()
{
    // TODO
}

WifiMode WifiManager::getMode() const
{
    return WifiMode::None;
}

bool WifiManager::isConnected() const
{
    return false;
}

void WifiManager::setEventCallback(EventCallback cb)
{
    // TODO
}

}  // namespace framework
