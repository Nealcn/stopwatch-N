/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include <smooth_ui_toolkit.hpp>
#include <uitk/short_namespace.hpp>
#include <mooncake_log.h>
#include <mooncake.h>
#include <esp_system.h>
#include <apps/apps.h>
#include <hal/hal.h>
#include <lv_demos.h>
#include <apps/app_voicecube/opus_encoder.h>
#include <apps/common/audio/audio.h>
#include <framework/power_manager/power_manager.h>
#include <framework/wifi_manager/wifi_manager.h>

using namespace mooncake;
using namespace smooth_ui_toolkit;

extern "C" void app_main(void)
{
    // Setup logger
    mclog::set_level(mclog::level_info);
    mclog::set_time_format(mclog::time_format_unix_milliseconds);

    // 记录复位原因（排查录音瞬间硬件复位的依据）：
    // 0x02=Brownout 欠压, 0x03=SW_RESET, 0x0C=SW_CPU_RESET, 0x12=TG1WDT, 0x15=USB_UART
    printf("[RESET] reason=%d\n", (int)esp_reset_reason());

    // HAL init
    GetHAL().init();

    // 预分配 Opus 编码器内存（内部 RAM）：启动早期 heap 干净、无碎片，
    // 避免语音输入录音时 43KB 连续块分配失败（实测运行时最大连续块仅 ~20KB）
    audio_encoder_prealloc();

    // Setup ui hal
    ui_hal::on_delay([](uint32_t ms) { GetHAL().delay(ms); });
    ui_hal::on_get_tick([]() { return GetHAL().millis(); });

    // Framework init（阶段二）：电源后台线程 + 全局 WiFi 栈
    framework::PowerManager::get().start();
    framework::WifiManager::get().init();

    // Install apps
    GetMooncake().installApp(std::make_unique<AppLauncher>());
    // GetMooncake().installApp(std::make_unique<AppAlarmClock>());  // 闹钟已移除（需求）
    // GetMooncake().installApp(std::make_unique<AppWatchFace>());    // 表盘已移除（需求）
    // GetMooncake().installApp(std::make_unique<AppStopWatch>());    // 秒表已移除（需求）
    GetMooncake().installApp(std::make_unique<AppBadge>());
    GetMooncake().installApp(std::make_unique<AppImu>());
    GetMooncake().installApp(std::make_unique<AppFft>());
    GetMooncake().installApp(std::make_unique<AppLuckyWheel>());
    GetMooncake().installApp(std::make_unique<AppSetup>());

    // 趣味拓展（阶段一）：安装顺序 = 环形菜单顺序
    GetMooncake().installApp(std::make_unique<AppPomodoro>());
    // GetMooncake().installApp(std::make_unique<AppDice>());  // 骰子已移除（需求）

    // VoiceCube 桌面模式（阶段二）：语音输入棒 + 触摸板鼠标
    GetMooncake().installApp(std::make_unique<AppVoiceCube>());
    // 小智 AI 对话（阶段三）：触摸/按键唤醒 + xiaozhi.me 云端语音对话
    GetMooncake().installApp(std::make_unique<AppAiChat>());
    // GetMooncake().installApp(std::make_unique<AppTemplate>());

    // Main loop
    while (1) {
        GetHAL().feedTheDog();
        GetMooncake().update();
    }
}
