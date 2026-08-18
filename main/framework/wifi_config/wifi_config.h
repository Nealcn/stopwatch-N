/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <functional>
#include <string_view>

/**
 * @brief WiFi 配网模块（AP + 网页）：设置应用启动热点，手机连上后
 *        浏览器打开 192.168.4.1 填写 WiFi 账号密码，保存后 STA 连接并持久化。
 *
 * 依赖 WifiManager（AP 静态 IP 192.168.4.1 + STA connectSta 存 NVS）。
 */
namespace framework::wifi_config {

void start(const std::function<void(std::string_view)>& onLog);
void stop();

/** 当前 STA 连接状态（供 UI 轮询显示） */
bool isConnected();
/** 最近一次配网结果描述（如 "已连接 MyWiFi" / "连接失败，请检查密码"） */
const char* lastResult();
/** AP 热点名（M5StopWatch-XXXX，供 UI 显示） */
const char* apSsid();

}  // namespace framework::wifi_config
