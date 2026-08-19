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
#include <esp_ota_ops.h>
#include <apps/apps.h>
#include <hal/hal.h>
#include <lv_demos.h>
#include <framework/ai_chat/ai_opus.h>
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

    // 标记当前固件有效（dual-OTA 必需）：bootloader 会写对侧回退 entry，
    // 不标记则崩溃重启被判定"待回滚"→ 切到空 slot 死循环（曾多次黑屏重启）
    esp_ota_mark_app_valid_cancel_rollback();

    // HAL init
    GetHAL().init();

    // 预分配 Opus 编码器内存（内部 RAM）：启动早期 heap 干净、无碎片，
    // 避免录音时 43KB 连续块分配失败（实测运行时最大连续块仅 ~20KB）。
    // 解码器不预分配：省 18KB 内部 RAM 给 WiFi 启动峰值（DMA 缓冲），
    // 解码器运行时动态分配（内部 RAM 优先，PSRAM fallback）
    audio_encoder_prealloc();
    // audio_decoder_prealloc();

    // Setup ui hal
    ui_hal::on_delay([](uint32_t ms) { GetHAL().delay(ms); });
    ui_hal::on_get_tick([]() { return GetHAL().millis(); });

    // Framework init（阶段二）：电源后台线程
    framework::PowerManager::get().start();
    // WiFi 栈懒加载：WifiManager 在首次 startAp/connectSta 时自动初始化，
    // 启动不 init 可省 ~40KB 内部 RAM（语音输入等不需要 WiFi 的功能内存更充足）
    // framework::WifiManager::get().init();

    // Install apps（安装顺序 = 菜单顺序；AI 对话最前、语音输入第二、设置最后）
    GetMooncake().installApp(std::make_unique<AppLauncher>());
    // GetMooncake().installApp(std::make_unique<AppAlarmClock>());  // 闹钟已移除（需求）
    // GetMooncake().installApp(std::make_unique<AppWatchFace>());    // 表盘已移除（需求）
    // GetMooncake().installApp(std::make_unique<AppStopWatch>());    // 秒表已移除（需求）
    // 小智 AI 对话（阶段三）：触摸/按键唤醒 + xiaozhi.me 云端语音对话
    GetMooncake().installApp(std::make_unique<AppAiChat>());
    // VoiceCube 桌面模式（阶段二）：语音输入棒 + 触摸板鼠标
    GetMooncake().installApp(std::make_unique<AppVoiceCube>());
    GetMooncake().installApp(std::make_unique<AppBadge>());
    GetMooncake().installApp(std::make_unique<AppImu>());
    GetMooncake().installApp(std::make_unique<AppFft>());
    GetMooncake().installApp(std::make_unique<AppLuckyWheel>());

    // 趣味拓展（阶段一）：安装顺序 = 环形菜单顺序
    GetMooncake().installApp(std::make_unique<AppPomodoro>());
    // GetMooncake().installApp(std::make_unique<AppDice>());  // 骰子已移除（需求）

    GetMooncake().installApp(std::make_unique<AppSetup>());  // 设置放最后
    // GetMooncake().installApp(std::make_unique<AppTemplate>());

    // Main loop
    while (1) {
        GetHAL().feedTheDog();
        GetMooncake().update();
    }
}
