# M5StopWatch-UserDemo

M5Stack StopWatch（ESP32-S3 圆形屏手表）融合固件 —— 官方硬件评测 Demo 的功能扩展版。

> 本仓库是 [m5stack/M5StopWatch-UserDemo](https://github.com/m5stack/M5StopWatch-UserDemo)（main 分支，v0.5）的 fork，扩展了 **Framework 框架层**、**中文字体**、**语音输入应用重设计**（含录音稳定性修复）、**桌面端 GUI** 和 **小智 AI 语音对话**（开发中）。

## 功能总览

### 设备端应用（9 个已启用，launcher 环形菜单）

| 应用 | 来源 | 功能 |
|------|------|------|
| AppLauncher | 官方 | 环形主菜单 + 引导页 |
| AppSetup | 官方 | 设置（日期时间/背光/音量/设备信息） |
| AppImu | 官方 | IMU 传感器数据（BMI270 立方体姿态） |
| AppFft | 官方 | 麦克风 FFT 环形频谱 |
| AppBadge | 官方 | 徽章（AP 配网 + 手机上传图片） |
| AppLuckyWheel | 官方 | 幸运大转盘（触屏转动） |
| **AppPomodoro** | 本仓库新增 | **番茄钟**：25 分钟专注 + 5 分钟休息多轮循环，到点震动 + 语音播报 |
| **AppVoiceCube** | 本仓库新增 | **语音输入**：BLE 语音识别 + 触摸板鼠标（重设计 UI + 录音稳定性修复） |
| **AppAiChat** | 本仓库新增 | **AI 对话**（阶段三开发中）：小智云端语音对话，触摸/按键唤醒 |

已移除（源码保留，[main.cpp](main/main.cpp) 注册行注释可恢复）：闹钟、表盘、秒表、骰子。另有 `AppTemplate`（应用开发模板，默认未安装）。

### 中文字体（2026-08-17 修复）

界面原显示方块字；新增思源黑体子集字体（`lv_font_cn_24/26`，211 字符，由 [gen_cn_fonts.py](gen_cn_fonts.py) 生成），经 hal_display 挂到所有字体 fallback。

### 语音输入（AppVoiceCube，阶段二）

设备与桌面端配合，把 StopWatch 变成**语音输入棒 + 无线触摸板**：

1. 连接电脑（BLE，设备名 `VS-XXXX`）
2. 按住侧键说话 → 松开 → 云端 ASR 识别
3. 识别文字在屏上大字预览（不自动粘贴）
4. 触摸屏当触摸板：滑动 = 移动光标（默认增益 3.0 + 封顶加速度曲线，2026-08-18 标定）、轻点 = 左键、长按 = 右键（可拖拽，抬起结束）
5. 光标到位 → 点「确认」→ 桌面端自动 Ctrl+V 粘贴；「取消」→ 丢弃

录音链路：`hal_audio 44.1kHz → 线性重采样 16kHz → Opus 60ms 帧 → BLE`。BLE 协议（UUID/帧格式）与 [Nealcn/VoiceCube](https://github.com/Nealcn/VoiceCube) **完全兼容**，详见 [docs/COMPATIBILITY.md](docs/COMPATIBILITY.md)。

### AI 对话（AppAiChat，阶段三开发中）

小智（xiaozhi 协议 v2）云端语音对话：触摸/按键唤醒 → 流式 ASR/LLM/TTS 播报，支持打断。架构/里程碑见 [docs/AI_CHAT_PLAN.md](docs/AI_CHAT_PLAN.md)，编译机验证见 [docs/VERIFY_AI_CHAT.md](docs/VERIFY_AI_CHAT.md)。

## 架构

```
main/
├── main.cpp              入口：framework 启动 + 9 个应用注册
├── apps/                 应用层（mooncake::AppAbility）
│   ├── app_*/            AppLauncher / AppPomodoro / AppVoiceCube / AppAiChat ...
│   └── common/           公共件（status_bar / key_manager / audio / loading_page ...）
├── framework/            框架层（本仓库实现）
│   ├── audio_mutex/      全局音频通道互斥锁（录音/播放/频谱共享读）
│   ├── power_manager/    电源后台常驻线程（电量监测/闲置降频/分级休眠）
│   ├── wifi_manager/     全局 WiFi 统一管理（AP/STA，AP 配网页脚手架）
│   ├── ble_voice/        VoiceCube BLE 服务（NimBLE GATT，协议兼容）
│   ├── touch_pad/        触摸板手势识别（滑动/轻点/长按）
│   └── ai_chat/          小智 AI 对话核心（协议/网络/激活/Opus 编解码/重采样）
└── hal/                  硬件抽象层
    ├── drivers/          cst820 触摸、rx8130 RTC
    ├── utils/            button / config_ap(配网) / settings(NVS) / wear_levelling
    └── hal_*.cpp         display / audio / button / rtc / fs / imu / ioe / pmic / alarm / badge

desktop/                  桌面端程序（PyQt5 GUI：托盘 + 悬浮球；--cli 无界面）
└── voicestick/           BLE 客户端 / ASR 客户端 / 协调器 / 鼠标注入（SendInput）
```

组件依赖（`components/`，版本由 [repos.json](repos.json) 锁定）：mooncake v2.3.3、smooth_ui_toolkit v2.12.1、LVGL v9.5.0、M5GFX 0.2.19、ArduinoJson v7.4.3、M5IOE1、M5PM1、BMI270_BMM150_Sensor、mooncake_log。

## 开发路线图（简版）

| 阶段 | 内容 | 状态 |
|------|------|------|
| 阶段一 W1 | 源码摸底：framework 三组件骨架（audio_mutex/power_manager/wifi_manager）+ 番茄钟/骰子骨架 | ✅ 完成 |
| 阶段一 W2 | 三组件真实现 + launcher 调度增强 + 番茄钟/骰子完整应用 | ✅ 完成 |
| 阶段二 | VoiceCube 桌面模式：ble_voice + touch_pad + AppVoiceCube + 桌面端程序 | ✅ 完成 |
| 阶段三 | 小智 AI 语音对话移植（AppAiChat：触摸/按键唤醒 + xiaozhi.me 云端对话，见 [AI_CHAT_PLAN.md](docs/AI_CHAT_PLAN.md)） | 🔧 P1 代码完成，待编译机验证 |
| 阶段四 | 配网 UI、番茄钟自定义时长等收尾（见[待办清单](docs/DEV_PLAN.md#待办清单)） | ⏳ 规划中 |
| 阶段五 | 功耗深度优化（休眠/自动唤醒，power_manager 深度演进） | ⏳ 规划中 |

项目当前状态另见 [DEV_STATUS.md](DEV_STATUS.md)（远端维护）、编译踩坑记录见 [PITFALLS.md](PITFALLS.md)。

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
pip install -r requirements.txt        # bleak / aiohttp / PyQt5
# 编辑 ~/.voicestick/config.json 填入 asr_api_key 后可启用语音识别（鼠标功能无需）
python main.py                         # GUI（托盘 + 悬浮球）
python main.py --cli                   # 无界面模式
```

## 文档索引

| 文档 | 内容 |
|------|------|
| [README.md](README.md) | 本页：项目总览 |
| [BUILD.md](BUILD.md) | Windows 构建指南（编译机） |
| [docs/DEV_PLAN.md](docs/DEV_PLAN.md) | 开发计划：阶段历史、路线图、待办清单 |
| [docs/COMPATIBILITY.md](docs/COMPATIBILITY.md) | VoiceCube 协议兼容性矩阵 |
| [docs/AI_CHAT_PLAN.md](docs/AI_CHAT_PLAN.md) | 阶段三规划：小智 AI 语音对话移植计划 |
| [docs/VERIFY_AI_CHAT.md](docs/VERIFY_AI_CHAT.md) | 阶段三 P1：编译机验证清单 |
| [DEV_STATUS.md](DEV_STATUS.md) | 项目当前状态（远端维护） |
| [PITFALLS.md](PITFALLS.md) | 编译踩坑记录 |

## 致谢

- 上游基线：M5Stack [M5StopWatch-UserDemo](https://github.com/m5stack/M5StopWatch-UserDemo)（v0.5）
- 协议参考：[Nealcn/VoiceCube](https://github.com/Nealcn/VoiceCube)（BLE 语音协议、桌面端 bleak 客户端）

License: MIT
