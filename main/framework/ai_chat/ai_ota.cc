/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 *
 * @brief 小智激活/配置获取（精简移植自 Stackchan-Newstep ota.cc）
 */
#include "ai_ota.h"

#include "ai_network.h"
#include <hal/hal.h>
#include <utils/settings/settings.h>

#include <http.h>
#include <ArduinoJson.h>
#include <esp_log.h>
#include <esp_system.h>
#include <esp_chip_info.h>
#include <esp_app_desc.h>
#include <sys/time.h>
#include <time.h>
#include <memory>
#include <utility>

#define TAG "ai_ota"

namespace framework {
namespace aichat {

const char* AiOta::kOtaUrl = "https://api.tenclass.net/xiaozhi/ota/";

esp_err_t AiOta::CheckVersion()
{
    auto network = GetAiNetwork();
    auto http    = std::unique_ptr<Http>(network->CreateHttp(0));
    if (http == nullptr) {
        ESP_LOGE(TAG, "Failed to create http client");
        return ESP_ERR_NO_MEM;
    }

    http->SetHeader("Activation-Version", "1");
    http->SetHeader("Device-Id", GetHAL().getFactoryMacString(":").c_str());
    http->SetHeader("Client-Id", GetOrCreateClientId().c_str());
    http->SetHeader("User-Agent", "M5StopWatch/0.5");
    http->SetHeader("Accept-Language", "zh-CN");
    http->SetHeader("Content-Type", "application/json");

    std::string data = BuildSystemInfoJson();
    http->SetContent(std::move(data));

    ESP_LOGI(TAG, "Checking version at %s", kOtaUrl);
    if (!http->Open("POST", kOtaUrl)) {
        int last_error = http->GetLastError();
        ESP_LOGE(TAG, "Failed to open HTTP connection, code=0x%x", last_error);
        return ESP_FAIL;
    }

    int status_code = http->GetStatusCode();
    if (status_code != 200) {
        ESP_LOGE(TAG, "Failed to check version, status code: %d", status_code);
        return ESP_ERR_INVALID_RESPONSE;
    }

    data = http->ReadAll();
    http->Close();

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, data);
    if (err) {
        ESP_LOGE(TAG, "Failed to parse JSON response: %s", err.c_str());
        return ESP_ERR_INVALID_RESPONSE;
    }

    // --- activation ---
    has_activation_code_ = false;
    activation_code_.clear();
    activation_message_.clear();
    if (doc["activation"].is<JsonObject>()) {
        activation_message_ = doc["activation"]["message"] | "";
        activation_code_    = doc["activation"]["code"] | "";
        has_activation_code_ = !activation_code_.empty();
    }

    // --- websocket 配置 → NVS ns "websocket" ---
    if (doc["websocket"].is<JsonObject>()) {
        Settings settings("websocket", true);
        JsonObject ws = doc["websocket"].as<JsonObject>();
        for (JsonPair kv : ws) {
            if (kv.value().is<const char*>()) {
                settings.SetString(kv.key().c_str(), kv.value().as<const char*>());
            } else if (kv.value().is<int>()) {
                settings.SetInt(kv.key().c_str(), kv.value().as<int>());
            }
        }
        ESP_LOGI(TAG, "websocket config saved");
    } else {
        ESP_LOGW(TAG, "No websocket section found in response");
    }

    // --- mqtt 配置 → NVS ns "mqtt"（服务器主通道已切换为 MQTT） ---
    if (doc["mqtt"].is<JsonObject>()) {
        Settings settings("mqtt", true);
        JsonObject mq = doc["mqtt"].as<JsonObject>();
        for (JsonPair kv : mq) {
            if (kv.value().is<const char*>()) {
                settings.SetString(kv.key().c_str(), kv.value().as<const char*>());
            } else if (kv.value().is<int>()) {
                settings.SetInt(kv.key().c_str(), kv.value().as<int>());
            }
        }
        ESP_LOGI(TAG, "mqtt config saved");
    } else {
        ESP_LOGW(TAG, "No mqtt section found in response");
    }

    // --- server_time 校时 ---
    if (doc["server_time"].is<JsonObject>()) {
        JsonObject st = doc["server_time"].as<JsonObject>();
        if (st["timestamp"].is<double>()) {
            double ts = st["timestamp"].as<double>();
            if (st["timezone_offset"].is<int>()) {
                ts += st["timezone_offset"].as<int>() * 60 * 1000;
            }
            struct timeval tv;
            tv.tv_sec  = (time_t)(ts / 1000);
            tv.tv_usec = (suseconds_t)((long long)ts % 1000) * 1000;
            settimeofday(&tv, nullptr);
            ESP_LOGI(TAG, "Server time applied");
        }
    }

    return ESP_OK;
}

std::string AiOta::BuildSystemInfoJson()
{
    JsonDocument doc;
    doc["version"]  = 2;
    doc["language"] = "zh-CN";

    const esp_app_desc_t* app_desc = esp_app_get_description();
    doc["mac_address"]     = GetHAL().getFactoryMacString(":");
    doc["uuid"]            = GetOrCreateClientId();
    doc["chip_model_name"] = "esp32s3";

    JsonObject chip_info = doc["chip_info"].to<JsonObject>();
    esp_chip_info_t chip;
    esp_chip_info(&chip);
    chip_info["model"]    = chip.model;
    chip_info["cores"]    = chip.cores;
    chip_info["revision"] = chip.revision;
    chip_info["features"] = chip.features;

    JsonObject app = doc["application"].to<JsonObject>();
    app["name"]         = app_desc->project_name;
    app["version"]      = app_desc->version;
    app["compile_time"] = std::string(app_desc->date) + "T" + app_desc->time + "Z";
    app["idf_version"]  = app_desc->idf_ver;

    // board 字段：服务器按 board 管理设备，格式不识别时可能只回激活码
    // （见 AI_CHAT_PLAN.md 风险 #1，必要时对齐官方 m5stack-core-s3 字段）
    JsonObject board = doc["board"].to<JsonObject>();
    board["type"] = "m5stack-stopwatch";
    board["name"] = "M5StopWatch";

    std::string json;
    serializeJson(doc, json);
    return json;
}

}  // namespace aichat
}  // namespace framework
