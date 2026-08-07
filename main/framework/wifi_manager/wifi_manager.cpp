/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "wifi_manager.h"
#include <mooncake_log.h>
#include <utils/settings/settings.h>

#include <cstring>
#include <mutex>

#include <esp_event.h>
#include <esp_netif.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <nvs_flash.h>

// 全局 WiFi 统一管理（阶段二实现）
//
// - init()：唯一化 WiFi 栈初始化（逻辑与 config_ap::ensure_wifi_stack_ready 一致，幂等）
// - startAp()：AP 模式 + 静态 IP + DHCP（配网页/captive portal 由后续配网 UI 阶段挂接）
// - connectSta()：STA 连接，事件组等待连接结果，凭据持久化到 NVS（ns "wifi"）
// - config_ap（badge 配网）与 config_ap 并存：两者共用同一栈初始化，互不干扰

namespace framework {

namespace {

constexpr const char* _tag               = "WifiManager";
constexpr EventBits_t _wifi_connected_bit = BIT0;
constexpr EventBits_t _wifi_failed_bit    = BIT1;

std::mutex _init_mutex;
bool _stack_initialized = false;
bool _handlers_registered = false;

EventGroupHandle_t _event_group = nullptr;
esp_netif_t* _ap_netif          = nullptr;
esp_netif_t* _sta_netif         = nullptr;

WifiMode _mode      = WifiMode::None;
bool _connected     = false;
WifiManager::EventCallback _event_callback = nullptr;

void notify(WifiEvent::Type type)
{
    if (_event_callback) {
        _event_callback(WifiEvent{type});
    }
}

bool ensure_stack_initialized()
{
    std::lock_guard<std::mutex> lock(_init_mutex);
    if (_stack_initialized) {
        return true;
    }

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    if (ret != ESP_OK) {
        mclog::tagError(_tag, "nvs init failed: {}", esp_err_to_name(ret));
        return false;
    }

    ret = esp_netif_init();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        mclog::tagError(_tag, "netif init failed: {}", esp_err_to_name(ret));
        return false;
    }

    ret = esp_event_loop_create_default();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        mclog::tagError(_tag, "event loop init failed: {}", esp_err_to_name(ret));
        return false;
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    cfg.nvs_enable         = false;  // 不依赖 WiFi NVS，凭据由本组件自行持久化
    ret                    = esp_wifi_init(&cfg);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        mclog::tagError(_tag, "wifi init failed: {}", esp_err_to_name(ret));
        return false;
    }

    _stack_initialized = true;
    return true;
}

void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT) {
        if (event_id == WIFI_EVENT_STA_START) {
            esp_wifi_connect();
        } else if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
            _connected = false;
            notify(WifiEvent::Disconnected);
            if (_event_group != nullptr) {
                xEventGroupSetBits(_event_group, _wifi_failed_bit);
            }
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        _connected = true;
        notify(WifiEvent::IpObtained);
        notify(WifiEvent::Connected);
        if (_event_group != nullptr) {
            xEventGroupSetBits(_event_group, _wifi_connected_bit);
        }
    }
}

void register_event_handlers()
{
    if (_handlers_registered) {
        return;
    }
    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, nullptr, nullptr);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, nullptr, nullptr);
    _handlers_registered = true;
}

void persist_credentials(const char* ssid, const char* password)
{
    Settings settings("wifi", true);
    settings.SetString("ssid", ssid);
    settings.SetString("pwd", password);
}

}  // namespace

WifiManager& WifiManager::get()
{
    static WifiManager _instance;
    return _instance;
}

void WifiManager::init()
{
    ensure_stack_initialized();
    register_event_handlers();
    mclog::tagInfo(_tag, "wifi stack ready");
}

bool WifiManager::startAp(const char* ssid, const char* password)
{
    if (!ensure_stack_initialized()) {
        return false;
    }

    if (_ap_netif == nullptr) {
        _ap_netif = esp_netif_create_default_wifi_ap();
    }
    if (_ap_netif == nullptr) {
        mclog::tagError(_tag, "failed to create AP netif");
        return false;
    }

    // 静态 IP + DHCP（192.168.4.1，与 config_ap 一致）
    esp_netif_ip_info_t ip_info;
    IP4_ADDR(&ip_info.ip, 192, 168, 4, 1);
    IP4_ADDR(&ip_info.gw, 192, 168, 4, 1);
    IP4_ADDR(&ip_info.netmask, 255, 255, 255, 0);
    esp_netif_dhcps_stop(_ap_netif);
    esp_netif_set_ip_info(_ap_netif, &ip_info);
    esp_netif_dhcps_start(_ap_netif);

    wifi_config_t wifi_config = {};
    strncpy(reinterpret_cast<char*>(wifi_config.ap.ssid), ssid, sizeof(wifi_config.ap.ssid) - 1);
    wifi_config.ap.ssid_len       = strlen(ssid);
    wifi_config.ap.max_connection = 4;
    wifi_config.ap.authmode       = (password != nullptr && password[0] != '\0') ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    if (wifi_config.ap.authmode == WIFI_AUTH_WPA2_PSK) {
        strncpy(reinterpret_cast<char*>(wifi_config.ap.password), password, sizeof(wifi_config.ap.password) - 1);
    }

    esp_err_t ret = esp_wifi_stop();
    if (ret != ESP_OK && ret != ESP_ERR_WIFI_NOT_STARTED && ret != ESP_ERR_WIFI_MODE) {
        mclog::tagWarn(_tag, "wifi stop before ap failed: {}", esp_err_to_name(ret));
    }

    ret = esp_wifi_set_mode(WIFI_MODE_AP);
    if (ret != ESP_OK) {
        mclog::tagError(_tag, "set AP mode failed: {}", esp_err_to_name(ret));
        return false;
    }
    ret = esp_wifi_set_config(WIFI_IF_AP, &wifi_config);
    if (ret != ESP_OK) {
        mclog::tagError(_tag, "set AP config failed: {}", esp_err_to_name(ret));
        return false;
    }
    esp_wifi_set_ps(WIFI_PS_NONE);
    ret = esp_wifi_start();
    if (ret != ESP_OK) {
        mclog::tagError(_tag, "start AP failed: {}", esp_err_to_name(ret));
        return false;
    }

    _mode = WifiMode::Ap;
    notify(WifiEvent::ApStarted);
    mclog::tagInfo(_tag, "AP started: {}", ssid);
    return true;
}

bool WifiManager::stopAp()
{
    esp_err_t ret = esp_wifi_stop();
    if (ret != ESP_OK && ret != ESP_ERR_WIFI_NOT_STARTED) {
        mclog::tagWarn(_tag, "stop AP failed: {}", esp_err_to_name(ret));
    }
    _mode = WifiMode::None;
    notify(WifiEvent::ApStopped);
    return true;
}

bool WifiManager::connectSta(const char* ssid, const char* password, uint32_t timeoutMs)
{
    if (!ensure_stack_initialized()) {
        return false;
    }
    if (_event_group == nullptr) {
        _event_group = xEventGroupCreate();
        if (_event_group == nullptr) {
            mclog::tagError(_tag, "failed to create event group");
            return false;
        }
    }
    register_event_handlers();

    if (_sta_netif == nullptr) {
        _sta_netif = esp_netif_create_default_wifi_sta();
        if (_sta_netif == nullptr) {
            mclog::tagError(_tag, "failed to create STA netif");
            return false;
        }
    }

    wifi_config_t wifi_config = {};
    strncpy(reinterpret_cast<char*>(wifi_config.sta.ssid), ssid, sizeof(wifi_config.sta.ssid) - 1);
    if (password != nullptr && password[0] != '\0') {
        strncpy(reinterpret_cast<char*>(wifi_config.sta.password), password, sizeof(wifi_config.sta.password) - 1);
    }

    esp_err_t ret = esp_wifi_stop();
    if (ret != ESP_OK && ret != ESP_ERR_WIFI_NOT_STARTED && ret != ESP_ERR_WIFI_MODE) {
        mclog::tagWarn(_tag, "wifi stop before sta failed: {}", esp_err_to_name(ret));
    }
    ret = esp_wifi_set_mode(WIFI_MODE_STA);
    if (ret != ESP_OK) {
        mclog::tagError(_tag, "set STA mode failed: {}", esp_err_to_name(ret));
        return false;
    }
    ret = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    if (ret != ESP_OK) {
        mclog::tagError(_tag, "set STA config failed: {}", esp_err_to_name(ret));
        return false;
    }
    ret = esp_wifi_start();
    if (ret != ESP_OK) {
        mclog::tagError(_tag, "start STA failed: {}", esp_err_to_name(ret));
        return false;
    }
    _mode = WifiMode::Sta;

    // 等待连接结果（WIFI_EVENT_STA_START 自动触发 esp_wifi_connect）
    EventBits_t bits = xEventGroupWaitBits(_event_group, _wifi_connected_bit | _wifi_failed_bit, pdTRUE, pdFALSE,
                                           pdMS_TO_TICKS(timeoutMs));
    xEventGroupClearBits(_event_group, _wifi_connected_bit | _wifi_failed_bit);

    bool connected = (bits & _wifi_connected_bit) != 0;
    if (connected) {
        persist_credentials(ssid, password);
        mclog::tagInfo(_tag, "STA connected: {}", ssid);
    } else {
        mclog::tagWarn(_tag, "STA connect timeout or failed: {}", ssid);
        _connected = false;
    }
    return connected;
}

void WifiManager::disconnect()
{
    esp_wifi_disconnect();
    esp_wifi_stop();
    _mode     = WifiMode::None;
    _connected = false;
    notify(WifiEvent::Disconnected);
}

WifiMode WifiManager::getMode() const
{
    return _mode;
}

bool WifiManager::isConnected() const
{
    return _connected;
}

void WifiManager::setEventCallback(EventCallback cb)
{
    _event_callback = std::move(cb);
}

}  // namespace framework
