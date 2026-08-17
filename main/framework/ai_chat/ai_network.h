/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <network_interface.h>
#include <string>

/**
 * @brief 小智 AI 对话网络层入口（阶段三）
 *
 * WebSocket/HTTP/TLS 网络层来自 78/esp-ml307 组件的 NetworkInterface 抽象
 * （EspNetwork：原生 ESP-IDF socket + esp_tls + esp_crt_bundle_attach），
 * 与 Stackchan-Newstep 的板级实现同源。
 *
 * @note 头文件与 API 签名以编译机拉取的 esp-ml307 v3.6.x 为准，
 *       首次编译时如头文件路径有差异仅需调整 include。
 */
namespace framework {
namespace aichat {

/**
 * @brief 获取全局网络接口（懒创建单例，等价 stackchan WifiBoard::GetNetwork）
 */
NetworkInterface* GetAiNetwork();

/**
 * @brief 设备 Client-Id（UUID v4）：首次生成并持久化到 NVS ns "websocket"，
 *        重启不变（移植 stackchan Board::GenerateUuid + NVS 持久化）
 */
std::string GetOrCreateClientId();

}  // namespace aichat
}  // namespace framework
