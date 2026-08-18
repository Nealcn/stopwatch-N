/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <cstdint>
#include <functional>

/**
 * @brief 全局 WiFi 统一管理（文档 3.1.1：所有语音引擎共用一套 WiFi 账号）
 *
 * 现状（见《源码摸底报告》5.2）：
 *  - 官方工程无任何 STA 代码，仅 badge 专用 config_ap（AP + captive portal）
 *  - WiFi 栈初始化（nvs/netif/event/wifi）在 config_ap 内部唯一化，需抽取接管
 *  - esp-wifi-connect 3.1.3 已声明于 idf_component.yml 未接入，STA 连接可复用
 *
 * 实现计划（阶段二）：
 *  - init()：唯一化 WiFi 栈初始化（从 config_ap::ensure_wifi_stack_ready 抽取）
 *  - startAp()：AP 起停 + DHCP + captive portal DNS 脚手架（复用 config_ap）
 *  - connectSta()：esp-wifi-connect 或自实现事件组等待；凭据存 NVS Settings（ns "wifi"）
 *  - config_ap 改造为经本组件起 AP，删除自带栈初始化
 */
namespace framework {

enum class WifiMode : uint8_t { None, Ap, Sta };

struct WifiEvent {
    enum Type : uint8_t { Connected, Disconnected, IpObtained, ApStarted, ApStopped };
    Type type;
};

class WifiManager {
public:
    using EventCallback = std::function<void(const WifiEvent&)>;

    static WifiManager& get();

    /* 唯一化 WiFi 栈初始化（app_main 早期调用一次） */
    void init();

    /* AP 模式：启动/停止热点，ssid 为空时自动生成 M5StopWatch-XXXXXX */
    bool startAp(const char* ssid, const char* password);
    bool stopAp();

    /* STA 模式：连接/断开，超时毫秒，凭据持久化到 NVS */
    bool connectSta(const char* ssid, const char* password, uint32_t timeoutMs);
    /* STA 模式：使用 NVS 中保存的凭据连接（配网后自动连 / AI 对话激活时） */
    bool connectSavedSta(uint32_t timeoutMs);
    void disconnect();

    /* 状态与事件 */
    WifiMode getMode() const;
    bool isConnected() const;
    void setEventCallback(EventCallback cb);

private:
    WifiManager()          = default;
    ~WifiManager()         = default;
    WifiManager(const WifiManager&)            = delete;
    WifiManager& operator=(const WifiManager&) = delete;
};

}  // namespace framework
