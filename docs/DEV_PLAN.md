# 开发计划（DEV PLAN）

> 本文档重建自提交历史与源码注释。开发过程中的两份外部文档（《源码摸底报告》与需求文档 3.x 章节）已补录至 `docs/`（见文末[文档索引](#文档索引)）——代码中的「文档 3.1.1 / 3.2 A 级」即引用需求文档对应章节。

## 阶段总览

| 阶段 | 主题 | 状态 | 完成日期 |
|------|------|------|----------|
| 阶段一 W1 | 源码摸底 + 骨架搭建 | ✅ 完成 | 2026-08-07 |
| 阶段一 W2 | 框架三组件真实现 + 趣味应用 | ✅ 完成 | 2026-08-07 |
| 阶段二 | VoiceCube 桌面模式 | ✅ 完成 | 2026-08-07 |
| 阶段三 | 小智 AI 语音对话移植（P1 编译机验证通过 ✅ 2026-08-18；P2 表情/摇晃互动完成 ✅；MCP 用户决定不做） | ✅ 完成 | 2026-08-18 |
| 阶段四 | 收尾优化（番茄钟自定义时长 / 配网 UI / NimBLE 完全关闭） | 🔧 进行中 | — |
| 阶段五 | 功耗深度优化 | ⏳ 规划中 | — |

## 阶段一：源码摸底与框架搭建

### W1 —— 摸底 + 骨架

- 全工程源码摸底，形成《源码摸底报告》5.1（音频）/ 5.2（WiFi）/ 5.3（电源）三份调研结论（已补录 [SOURCE_SURVEY.md](SOURCE_SURVEY.md)）
- 搭建 framework 三组件骨架：`audio_mutex` / `power_manager` / `wifi_manager`（仅接口定义 + 实现计划注释）
- 番茄钟 / 骰子应用骨架（`AppPomodoro` / `AppDice`，功能未完成）
- 添加 [BUILD.md](BUILD.md)：Windows 构建指南（源码工作区与编译机分工），含阶段一 W1 烧录后验证清单

### W2 —— 真实现

- **framework 三组件真实现**：
  - `audio_mutex`：基于 FreeRTOS 互斥量，录音/播放通道排他 + 频谱共享读，持有者退出强制释放
  - `power_manager`：后台常驻线程，电量监测（复用 hal_pmic 1Hz 滑动滤波采样）、闲置计时、分级降频/休眠
  - `wifi_manager`：唯一化 WiFi 栈初始化（从 config_ap 抽取接管）、AP/STA 模式、事件回调、NVS 凭据存储脚手架
- **launcher 调度增强**：应用安装顺序即环形菜单顺序，统一经 `apps.h` 注册
- **番茄钟完整应用**：25/5 循环、到点震动 + 语音播报、按键/触屏交互（`key_manager`）
- **骰子完整应用**：IMU 摇晃检测掷骰、圆形屏 1–6 点渲染

## 阶段二：VoiceCube 桌面模式

- **`ble_voice`**：NimBLE GATT peripheral 真实现，协议与 Nealcn/VoiceCube 完全兼容（UUID/16B 帧头/Opus payload），改编自 VoiceCube firmware `ble_service.c`
- **`touch_pad`**：触摸板手势识别（滑动=移动/轻点=左键/长按=右键，带加速度增益与死区）
- **`AppVoiceCube`**：语音录音链路（44.1kHz → 16kHz 重采样 → Opus 60ms 帧 → BLE）、屏上大字预览 + 确认/取消粘贴、触摸板鼠标事件组包
- **桌面端程序**（`desktop/`）：Python CLI（无 PyQt5 依赖），bleak BLE 客户端 + 火山引擎流式 ASR + SendInput 鼠标/剪贴板注入；协议模块与 VoiceCube 桌面端逐字节兼容

## 阶段三：小智 AI 语音对话移植（已完成）

把 [Nealcn/Stackchan-Newstep](https://github.com/Nealcn/Stackchan-Newstep)（小智 AI 聊天机器人，xiaozhi 协议 v2）的核心能力移植进本工程，新增 `AppAiChat` 应用：

- 云端 AI 语音对话（触摸/按键唤醒，xiaozhi.me 官方服务器，websocket 协议）
- 表情 Avatar（P2：240x240 lv_canvas，7 表情 + 嘴型动画）、摇晃互动（P2：16 条语料 + 10s 冷却 + emotion 映射）
- 不做：MCP 设备工具（用户决定，设置 App 已有同能力）、红外、SD/拍照、摄像头、舵机、LED 灯环、4G、声纹、ESP-SR 唤醒词

详细设计（架构/音频链路/状态机/激活流程/风险清单）见 **[AI_CHAT_PLAN.md](AI_CHAT_PLAN.md)**；
编译机验证记录见 **[VERIFY_AI_CHAT.md](VERIFY_AI_CHAT.md)**（2026-08-18：编译通过，修复 4 处）。

## 阶段四：收尾优化（进行中）

- 番茄钟自定义时长（当前固定 25/5，与系统设置联动）
- WiFi 配网 UI：`wifi_manager.startAp` 已实现（AP + DHCP），配网页 / captive portal 待挂接
- `ble_voice` NimBLE 栈完全关闭（当前仅停广告，需真机验证后补充）
- ~~骰子摇晃阈值实测校准~~（功能已取消）

## 阶段五：功耗深度优化（规划）

- `power_manager` 深度演进：M5PM1 `timerSet` RTC 定时唤醒 + `getWakeSource` 唤醒源查询、分级休眠阈值调优、深睡恢复流程（`esp_restart()`）

## 待办清单（代码内 TODO / 注释整理）

| # | 事项 | 位置 | 状态 |
|---|------|------|------|
| 1 | 番茄钟自定义时长（系统设置联动） | `main/apps/app_pomodoro/app_pomodoro.h` | ⏳ |
| 2 | startAp 配网页 / captive portal 挂接 | `main/framework/wifi_manager/wifi_manager.cpp` | ⏳ |
| 3 | ble_voice NimBLE 栈完全关闭（停广告之外） | `main/framework/ble_voice/ble_voice.h` | ⏳（需真机） |
| 4 | touch_pad 灵敏度/标定参数真机调整 | `main/framework/touch_pad/touch_pad.h` | ⏳ |
| 5 | power_manager 分级休眠/自动唤醒深度实现 | `main/framework/power_manager/power_manager.cpp` | ⏳ |
| 6 | 桌面端：无 ASR Key 时交互提示优化（当前仅日志警告） | `desktop/main.py` | ⏳（设置对话框已可填 Key） |

## 文档补录清单

- ✅ 《源码摸底报告》（5.1 音频 / 5.2 WiFi / 5.3 电源调研）→ 已补录 [SOURCE_SURVEY.md](SOURCE_SURVEY.md)（2026-08-19）
- ✅ 需求文档 3.x 章节（3.1.1 基础能力 / 3.2 A 级趣味功能）→ 已随 [M5-StopWatch-多应用融合固件开发项目文档.md](M5-StopWatch-多应用融合固件开发项目文档.md) 第 3 章入仓（2026-08-17）

## 文档索引

| 文档 | 内容 |
|------|------|
| [AI_CHAT_PLAN.md](AI_CHAT_PLAN.md) | 阶段三：小智 AI 语音对话移植计划（架构/音频/状态机/里程碑/风险） |
| [VERIFY_AI_CHAT.md](VERIFY_AI_CHAT.md) | 阶段三 P1：编译机验证清单（依赖确认/冒烟/协议 mock/真机） |
| [SOURCE_SURVEY.md](SOURCE_SURVEY.md) | 源码摸底报告（5.1 音频 / 5.2 WiFi / 5.3 电源调研，2026-08-19 补录） |
| [M5-StopWatch-多应用融合固件开发项目文档.md](M5-StopWatch-多应用融合固件开发项目文档.md) | 需求文档 3.x + 总体架构 + 分阶段计划（外部文档入仓版） |
