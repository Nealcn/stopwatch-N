/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "ai_network.h"

#include <esp_network.h>
#include <esp_random.h>
#include <esp_log.h>
#include <cstdio>
#include <cstring>
#include <utils/settings/settings.h>

#define TAG "ai_network"

namespace framework {
namespace aichat {

NetworkInterface* GetAiNetwork()
{
    // 静态实例：与 stackchan wifi_board.cc 的 `static EspNetwork network;` 同模式
    static EspNetwork network;
    return &network;
}

std::string GetOrCreateClientId()
{
    Settings settings("websocket", true);
    std::string id = settings.GetString("client_id");
    if (!id.empty()) {
        return id;
    }

    // UUID v4：16 字节硬件随机数 + 版本/变体位
    uint8_t uuid[16];
    esp_fill_random(uuid, sizeof(uuid));
    uuid[6] = (uuid[6] & 0x0F) | 0x40;  // version 4
    uuid[8] = (uuid[8] & 0x3F) | 0x80;  // variant 1

    char buf[37];
    snprintf(buf, sizeof(buf),
             "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
             uuid[0], uuid[1], uuid[2], uuid[3], uuid[4], uuid[5], uuid[6], uuid[7],
             uuid[8], uuid[9], uuid[10], uuid[11], uuid[12], uuid[13], uuid[14], uuid[15]);
    id = buf;
    settings.SetString("client_id", id);
    ESP_LOGI(TAG, "Generated client id: %s", id.c_str());
    return id;
}

}  // namespace aichat
}  // namespace framework
