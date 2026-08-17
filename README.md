# M5StopWatch-UserDemo

M5Stack StopWatch（ESP32-S3 圆形屏手表）融合固件 —— 官方硬件评测 Demo 的功能扩展版。

> 本仓库是 [m5stack/M5StopWatch-UserDemo](https://github.com/m5stack/M5StopWatch-UserDemo)（main 分支，v0.5）的 fork，在官方 9 个应用的基础上扩展了 **Framework 框架层**、**趣味应用**（番茄钟/骰子）和 **VoiceCube 桌面模式**（语音输入棒 + 触摸板鼠标）。

## 功能总览

### 设备端应用（12 个已启用，launcher 环形菜单）

| 应用 | 来源 | 功能 |
|------|------|------|
| AppLauncher | 官方 | 环形主菜单 + 引导页 |
| AppStopwatch | 官方 | 秒表（开始/暂停/LAP 分段） |
| AppWatchFace | 官方 | 表盘（经典/大数字/数字流动/简洁 4 款） |
| AppAlarmClock | 官方 | 闹钟（列表/添加/到点震动响铃） |
| AppSetup | 官方 | 设置（日期时间/背光/音量/设备信息） |
| AppImu | 官方 | IMU 传感器数据（BMI270 立方体姿态） |
| AppFft | 官方 | 麦克风 FFT 环形频谱 |
| AppBadge | 官方 | 徽章（AP 配网 + 手机上传图片） |
| AppLuckyWheel | 官方 | 幸运大转盘（触屏转动） |
| **AppPomodoro** | 本仓库新增 | **番茄钟**：25 分钟专注 + 5 分钟休息多轮循环，到点震动 + 语音播报 |
| **AppDice** | 本仓库新增 | **骰子模拟器**：摇晃设备掷骰（IMU 采样随机数），圆屏渲染 1–6 点 |
| **AppVoiceCube** | 本仓库新增 | **VoiceCube 桌面模式**：语音输入棒 + 触摸板鼠标（见下） |

另有 `AppTemplate`（应用开发模板，默认未安装）。

### VoiceCube 桌面模式（阶段二）

设备与桌面端配合，把 StopWatch 变成**语音输入棒 + 无线触摸板**：

1. 连接电脑（BLE，设备名 `VS-XXXX`）
2. 按住侧键说话 → 松开 → 云端 ASR 识别
3. 识别文字在屏上大字预览（不自动粘贴）
4. 触摸屏当触摸板：滑动 = 移动光标、轻点 = 左键、长按 = 右键
5. 光标到位 → 点「确认」→ 桌面端自动 Ctrl+V 粘贴；「取消」→ 丢弃

录音链路：`hal_audio 44.1kHz → 线性重采样 16kHz → Opus 60ms 帧 → BLE`。BLE 协议（UUID/帧格式）与 [Nealcn/VoiceCube](https://github.com/Nealcn/VoiceCube) **完全兼容**，详见 [docs/COMPATIBILITY.md](docs/COMPATIBILITY.md)。

## 架构

```
main/
├── main.cpp              入口：framework 启动 + 12 个应用注册
├── apps/                 应用层（mooncake::AppAbility）
│   ├── app_*/            AppLauncher / AppPomodoro / AppDice / AppVoiceCube ...
│   └── common/           公共件（status_bar / key_manager / audio / loading_page ...）
├── framework/            框架层（本仓库实现）
│   ├── audio_mutex/      全局音频通道互斥锁（录音/播放/频谱共享读）
│   ├── power_manager/    电源后台常驻线程（电量监测/闲置降频/分级休眠）
│   ├── wifi_manager/     全局 WiFi 统一管理（AP/STA，AP 配网页脚手架）
│   ├── ble_voice/        VoiceCube BLE 服务（NimBLE GATT，协议兼容）
│   └── touch_pad/        触摸板手势识别（滑动/轻点/长按）
└── hal/                  硬件抽象层
    ├── drivers/          cst820 触摸、rx8130 RTC
    ├── utils/            button / config_ap(配网) / settings(NVS) / wear_levelling
    └── hal_*.cpp         display / audio / button / rtc / fs / imu / ioe / pmic / alarm / badge

desktop/                  桌面端程序（Python CLI，无 PyQt5 依赖）
└── voicestick/           BLE 客户端 / ASR 客户端 / 协调器 / 鼠标注入（SendInput）
```

组件依赖（`components/`，版本由 [repos.json](repos.json) 锁定）：mooncake v2.3.3、smooth_ui_toolkit v2.12.1、LVGL v9.5.0、M5GFX 0.2.19、ArduinoJson v7.4.3、M5IOE1、M5PM1、BMI270_BMM150_Sensor、mooncake_log。

## 开发路线图（简版）

| 阶段 | 内容 | 状态 |
|------|------|------|
| 阶段一 W1 | 源码摸底：framework 三组件骨架（audio_mutex/power_manager/wifi_manager）+ 番茄钟/骰子骨架 | ✅ 完成 |
| 阶段一 W2 | 三组件真实现 + launcher 调度增强 + 番茄钟/骰子完整应用 | ✅ 完成 |
| 阶段二 | VoiceCube 桌面模式：ble_voice + touch_pad + AppVoiceCube + 桌面端程序 | ✅ 完成 |
| 阶段三 | 小智 AI 语音对话移植（AppAiChat：触摸/按键唤醒 + xiaozhi.me 云端对话 + 表情 Avatar + 设备管理 MCP + 触控/摇晃互动，见 [AI_CHAT_PLAN.md](docs/AI_CHAT_PLAN.md)） | ⏳ 规划中 |
| 阶段四 | 配网 UI、番茄钟自定义时长、骰子阈值标定等收尾（见[待办清单](docs/DEV_PLAN.md#待办清单)） | ⏳ 规划中 |
| 阶段五 | 功耗深度优化（休眠/自动唤醒，power_manager 深度演进） | ⏳ 规划中 |

详细历史与待办见 [docs/DEV_PLAN.md](docs/DEV_PLAN.md)。

## 兼容性

- **VoiceCube 协议**：BLE UUID、音频帧格式、ASR 接口与 Nealcn/VoiceCube 桌面端逐字节兼容；本工程新增 mouse / asr / paste_request 扩展事件（向后兼容）。完整矩阵见 [docs/COMPATIBILITY.md](docs/COMPATIBILITY.md)。
- **工具链**：ESP-IDF **v5.5.4**（与官方工程锁定版本一致），其余版本见 [BUILD.md](BUILD.md)。

## 构建与烧录

完整指南（Windows，含依赖拉取/工具链安装/验证清单）见 **[BUILD.md](BUILD.md)**。

```bash
# 拉取依赖组件（repos.json 锁定版本，幂等）
python3 ./fetch_repos.py

# 编译与烧录（需 ESP-IDF v5.5.4 环境）
idf.py build
idf.py -p COMx flash
```

桌面端运行：

```bash
cd desktop
pip install bleak aiohttp
# 编辑 ~/.voicestick/config.json 填入 asr_api_key 后可启用语音识别（鼠标功能无需）
python main.py
```

## 文档索引

| 文档 | 内容 |
|------|------|
| [README.md](README.md) | 本页：项目总览 |
| [BUILD.md](BUILD.md) | Windows 构建指南（编译机） |
| [docs/DEV_PLAN.md](docs/DEV_PLAN.md) | 开发计划：阶段历史、路线图、待办清单 |
| [docs/COMPATIBILITY.md](docs/COMPATIBILITY.md) | VoiceCube 协议兼容性矩阵 |
| [docs/AI_CHAT_PLAN.md](docs/AI_CHAT_PLAN.md) | 阶段三规划：小智 AI 语音对话移植计划 |

## 致谢

- 上游基线：M5Stack [M5StopWatch-UserDemo](https://github.com/m5stack/M5StopWatch-UserDemo)（v0.5）
- 协议参考：[Nealcn/VoiceCube](https://github.com/Nealcn/VoiceCube)（BLE 语音协议、桌面端 bleak 客户端）

License: MIT
