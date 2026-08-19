# 源码摸底报告

> 本文档补录自 2026-08-19 源码调研，对应开发早期形成的《源码摸底报告》5.1/5.2/5.3 三节
> （原报告未入仓库，代码注释多处引用「见《源码摸底报告》5.x」）。
> 内容以**当前仓库实际代码**为准（dev 分支 a77d6f2 后），标注了各能力实现位置。

## 5.1 音频子系统

### 硬件链路

- **Codec**：ES8311 低功耗编解码器（I2C 控制，地址 `ES8311_CODEC_DEFAULT_ADDR`；含 PA 引脚控制，当前 `GPIO_NUM_NC` 未接）
- **I2S 总线**：`I2S_NUM_0`，引脚 MCLK=18 / BCLK=17 / DIN(录音)=16 / LRCK=15 / DOUT(播放)=21（`hal_audio.cpp`）
- **声学器件**：MEMS 麦克风（录音）+ 外放扬声器（播放）
- **工作模式**：`ESP_CODEC_DEV_WORK_MODE_BOTH`（录放双工），44.1kHz / 16bit / 单声道

### 软件现状（hal_audio.cpp，基于 esp_codec_dev 组件）

| 能力 | 现状 | 位置 |
|---|---|---|
| 录音 | `audioRecord(std::vector<int16_t>&, durationMs, gain)` 阻塞采集，默认 IN gain 30dB | `hal.h:191` |
| 播放 | `audioPlay(data, async=true)` 默认异步；async 打断机制 = 新请求通知替代强抢锁 | `hal.h:192` |
| 占用查询 | `getAudioBusy()` 供 audio_mutex 联动 | `hal.h:195` |
| 频谱 | 512 FFT / 256 hop，20 频段，共享读 | `hal.h:197`、AudioCodec `_spectrum_init` |
| 后台任务 | `audio_task` 4KB 栈、优先级 5，常驻处理播放/频谱 | `hal_audio.cpp:40` |
| 静音垫 | 0.1s 静音缓冲（打断播放防爆音） | `hal_audio.cpp:37` |

### 互斥语义（audio_mutex.h，对应文档 3.1.1）

- **录音通道 = 排他**：`audioRecord` 阻塞读与频谱读取同源，录音中禁止其他模块占用
- **播放通道 = 排他但可被打断**：现有 `audioPlay` 的 async 打断机制配合"新请求通知替代强抢锁"
- **频谱读取 = 录音通道的共享读**：录音空闲允许，录音中拒绝（返回旧帧）
- 实现：FreeRTOS 互斥量（参照 hal_display.cpp 的 xGuiSemaphore 模式），持有者退出（app onClose）时强制释放防泄漏

### 语音编码链路（上层应用各自实现，未统一到中间层）

- **VoiceCube（BLE 语音输入）**：44.1kHz → 线性插值重采样 16kHz（`resample_44100_to_16000`，步进 2.75625）→ Opus 60ms 帧（960 samples）→ NimBLE GATT 传输（`app_voicecube.cpp`）
- **小智 AI 对话（AppAiChat）**：16kHz 录音 + Opus 编码 + websocket 上行（`app_ai_chat`）
- **Opus 版本**：libopus v1.5.2，本地组件 `components/opus`（vendoring 进 ESP-IDF 的实例，见 PITFALLS.md）

### 已知坑（编译/运行验证过）

- Opus 编码器**强制内部 RAM** 且需启动早期预分配（PSRAM 密集读写 cache 卡死 → WDT 复位 reason=7；内部 RAM 碎片化时最大连续块仅 20KB）
- 录音任务栈放 **PSRAM**（24KB），内部 RAM 栈不足且需留给编码器
- NimBLE host 栈 4KB→**8KB**（订阅回调栈溢出崩溃）
- WDT/Brownout 已对齐原版：INT_WDT 关、Task WDT 30s、Brownout 电平 5

## 5.2 WiFi 子系统

### 现状（对应 wifi_manager.h 注释）

- **官方工程无任何 STA 代码**：仅 badge 专用 `config_ap`（AP + captive portal + 手机传图，`hal/utils/config_ap`）
- WiFi 栈初始化（nvs_flash / netif / event loop / esp_wifi）在 `config_ap::ensure_wifi_stack_ready` 内部唯一化（static mutex + bool，幂等）——已抽取接管
- `esp-wifi-connect` 3.1.3 曾声明于 idf_component.yml 未接入（STA 连接未复用）

### 统一管理组件（framework/wifi_manager，阶段二实现）

| 能力 | 说明 |
|---|---|
| `init()` | 唯一化栈初始化（幂等，逻辑与 config_ap 一致）；`cfg.nvs_enable=false`，凭据由组件自行持久化 |
| `startAp()` | AP 模式 + 静态 IP + DHCP，ssid 为空自动生成 `M5StopWatch-XXXXXX` |
| `connectSta()` | STA 连接，事件组（connected/failed BIT）等待结果，凭据存 NVS ns `"wifi"` |
| `connectSavedSta()` | 用 NVS 凭据连接（配网后自动连 / AI 对话激活时） |
| 事件回调 | Connected / Disconnected / IpObtained / ApStarted / ApStopped |

### 配网 UI（framework/wifi_config + app_setup worker，2026-08-18 新增）

- 设置 → WiFi：AP 热点（M5StopWatch-XXXX）+ 网页配网（192.168.4.1 填写账号密码），凭据存 NVS
- 与 badge config_ap 并存：共用同一栈初始化，互不干扰

### 已知坑（实测）

- **WiFi 任务栈必须内部 RAM 8KB**：esp_wifi 调用 + NVS 写期间 cache 禁用，PSRAM 栈会断言崩溃
- **AP 启动必须异步化**：LVGL 持锁上下文不能阻塞
- **懒加载坑**：WifiManager 懒加载后 lwIP 未初始化 → AI 对话打开时需**无条件先连 WiFi**（否则 Invalid mbox 崩溃）
- **TLS 证书验证**：RTC 时间回退（2012）导致握手失败 → 激活前需 SNTP 校时（`esp_netif_sntp`）
- WiFi 懒加载本身省 ~40KB 内存

## 5.3 电源子系统

### 硬件能力

- **电池**：450mAh 锂电池，Type-C 充电
- **电源管理 IC**：M5PM1 多级电源管理——支持 `timerSet`（RTC 定时唤醒）与 `getWakeSource`（唤醒源查询），分级休眠硬件基础已具备
- **RTC**：RX8130CE 硬件 RTC（断网守时，hal_rtc）

### 电量采样现状（hal_pmic.cpp）

- 独立线程 `bat_reading_task`：1Hz 周期 + 7:1 滑动滤波（`_bat_filter_weight_old=7 / new=1`）
- 电量映射：3300mV（0%）~ 4200mV（100%）线性，互斥量保护电平缓存
- 充电检测：`isBatteryCharging()`
- **结论：采样层已完整，可直接复用并入电源管理**

### 电源管理组件（framework/power_manager，阶段二实现）

当前覆盖（**分级休眠第一级**）：

- 后台 FreeRTOS 任务：优先级 2（低于 UI）、1s 周期（`_sample_period_ms`）
- 电量/充电状态监测与事件回调（ChargingStarted/Stopped、LowBattery ≤15%，消费 hal_pmic 采样结果）
- **闲置关屏**：60s 无输入（按键/触屏）自动背光归零，输入即唤醒（`AfterWake` 事件）——当前已实现的最低档省电

未覆盖（后续阶段）：

- **CPU 降频**：全工程无 `esp_pm` 调用，sdkconfig 无 `CONFIG_PM_ENABLE`（需补 + `esp_pm_configure`）
- **深度休眠**：无 `esp_sleep` 调用；M5PM1 `timerSet` 定时唤醒、`getWakeSource` 唤醒源查询未接
- **深睡前置操作**（已确认需做）：`ioe_speaker_enable(false)` + `stopLvglUpdate()` + codec close；深睡恢复走 `esp_restart()`

### 其他

- 背光：`setBackLightBrightness`（可持久化到 settings）
- 功耗优化入口已规划：阶段五（DEV_PLAN.md 阶段五：power_manager 深度演进）

## 结论与建议

1. **音频**：hal_audio 原始 PCM 通道 + audio_mutex 互斥语义已成型；重采样/编码散落在各应用，若新增语音应用建议抽公共 `audio_codec` 中间层（16k 重采样 + Opus 帧封装）
2. **WiFi**：统一入口（wifi_manager）与配网 UI（wifi_config）已闭环；STA 凭据 NVS 持久化与 AP/STA 切换是后续 AI 对话联网的基础
3. **电源**：监测层完备，执行层（降频/深睡）待阶段四收尾与阶段五实现；深睡是续航达标（对比原厂 <15% 差距）的关键路径
