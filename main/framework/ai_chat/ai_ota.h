/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 *
 * @brief 小智激活/配置获取（精简移植自 Stackchan-Newstep ota.cc）
 * 仅保留 CheckVersion（POST OTA URL → websocket 配置/激活码落 NVS）+ server_time。
 * 固件升级（Upgrade/MarkCurrentVersionValid）留待 P3。
 */
#pragma once

#include <esp_err.h>
#include <string>

namespace framework {
namespace aichat {

class AiOta {
public:
    /** 设备激活/OTA 检查 URL（xiaozhi.me 官方） */
    static const char* kOtaUrl;

    /**
     * @brief POST 系统信息到 OTA URL，解析响应：
     *  - activation.code/message → 需要激活码
     *  - websocket 段 → 写入 NVS ns "websocket"（url/token/version）
     *  - server_time → settimeofday 校时
     * @return ESP_OK / 负错误码 / HTTP 状态码
     */
    esp_err_t CheckVersion();

    bool HasActivationCode() const { return has_activation_code_; }
    const std::string& GetActivationCode() const { return activation_code_; }
    const std::string& GetActivationMessage() const { return activation_message_; }

private:
    std::string BuildSystemInfoJson();

    bool has_activation_code_ = false;
    std::string activation_code_;
    std::string activation_message_;
};

}  // namespace aichat
}  // namespace framework
