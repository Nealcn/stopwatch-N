# 小智 AI 语音对话移植计划（规划中）

> 文档状态：**计划草案（未实施）**。来源：[Nealcn/Stackchan-Newstep](https://github.com/Nealcn/Stackchan-Newstep)（小智 AI 聊天机器人，xiaozhi 协议 v2）核心能力移植评估。
> 排除项（硬件不支持）：红外遥控、SD 卡/拍照、摄像头、舵机/云台、LED 灯环、4G、声纹、ESP-SR 离线唤醒词。

## 1. 目标与已确认决策

把「云端 AI 语音对话」移植进 M5StopWatch（ESP32-S3 圆形屏手表），作为新应用 `AppAiChat` 接入 mooncake 框架。

| 决策项 | 结论 |
|---|---|
| 唤醒方式 | **触摸/按键唤醒**（无 ESP-SR 离线唤醒词） |
| 服务端 | **xiaozhi.me 官方**（websocket 协议 v2） |
| 附带功能 | 表情 Avatar、设备管理/MCP（音量/亮度/电量/重启）、触控/摇晃互动 |
| 不做 | 早安问候、红外、SD/拍照、摄像头、舵机、LED 灯环、4G、声纹、OTA 固件升级（P3 可选） |

## 2. 关键技术事实（已核实）

### 目标硬件能力
- ESP32-S3 240MHz、16MB Flash（双 OTA 各 4.94MB + FAT 4M storage）、**8MB OPI PSRAM**
- ES8311 codec（I2S0 全双工，**固定 44.1kHz/16bit/mono**）、CO5300 AMOLED 圆屏 466x466（LVGL v9.5 + M5GFX）、CST820 触摸、BMI270 IMU、RX8130 RTC、M5PM1 PMIC、M5IOE1、震动马达、按键 A=GPIO2 / B=GPIO1
- **无 AEC/VAD**（喇叭与麦克风同体 → 半双工策略）

### 决定性发现：网络层组件来源
xiaozhi 的 WebSocket/HTTP/TLS 网络层来自 **`78/esp-ml307` 组件**的 `NetworkInterface` 抽象（非 esp-wifi-connect）：
- `class NetworkInterface`：`CreateHttp/CreateTcp/CreateSsl/CreateUdp/CreateMqtt/CreateWebSocket`
- `class WebSocket`：`SetHeader/Connect(uri)/Send(data,len,binary,fin)/OnData/OnDisconnected/Ping/Close`
- `class EspNetwork`：原生 ESP-IDF socket 实现，TLS 用 esp_tls + `esp_crt_bundle_attach`；要求 IDF ≥ 5.5.2（目标 5.5.4 满足）

→ 目标工程加一行依赖 `78/esp-ml307: ~3.6.5`，`websocket_protocol.cc` 与 `ota.cc` 即可近原样移植（协议细节零漂移）。

### xiaozhi 协议要点（stackchan `main/protocols/`）
- **握手**：客户端 hello `{"type":"hello","version":2,"features":{"mcp":true},"transport":"websocket","audio_params":{"format":"opus","sample_rate":16000,"channels":1,"frame_duration":60}}`（不带 aec）；服务端回 hello（transport/session_id/audio_params，可改采样率帧长）
- **上行 JSON**：`listen detect/start/stop`、`abort`、`mcp`；`ListeningModeManualStop` 已存在（hold-to-talk 用）
- **下行 JSON**：`tts start/sentence_start/stop`（文本上屏）、`stt`、`llm`（emotion 字段）、`mcp`、`system reboot`、`alert`
- **音频帧** `BinaryProtocol2`（大端 16B 头）：`version u16=2 | type u16=0(OPUS) | reserved u32 | timestamp u32(ms) | payload_size u32 | payload`
- **激活**：POST `https://api.tenclass.net/xiaozhi/ota/`（头：`Activation-Version:1`、`Device-Id: MAC`、`Client-Id: UUID`、`User-Agent`、`Accept-Language: zh-CN`）→ 返回 websocket url/token 存 NVS，或 `activation.code` 6 位激活码人工绑定

## 3. 架构与文件组织（混合式：协议层移植 + 编排层按 mooncake 重写）

全部新文件（`main/CMakeLists.txt` GLOB_RECURSE 自动收编，无需改构建）：

```
main/framework/ai_chat/              ← 无 LVGL 依赖的可移植核心
  ai_network.h/.cc                   ← EspNetwork 单例持有 + 生命周期（懒创建）
  ai_protocol.h/.cc                  ← 移植 protocols/protocol.{h,cc}（事件消息 + BinaryProtocol2，删 MQTT/UDP）
  ai_websocket_protocol.h/.cc        ← 移植 websocket_protocol.{h,cc}（WebSocket 改为注入 NetworkInterface）
  ai_ota.h/.cc                       ← 精简移植 ota.cc：CheckVersion + 激活码轮询；删 Upgrade
  ai_opus.h/.cc                      ← 公共 opus wrapper：opus_encoder.c 上移 + 新增 decoder wrapper
  ai_resample.h/.cc                  ← resample_linear(in, in_rate, out_rate) 泛化重采样
  ai_mcp.h/.cc                       ← 精简 JSON-RPC 2.0（initialize/tools/list/tools/call）
main/apps/app_ai_chat/
  app_ai_chat.h/.cpp                 ← AppAbility（onCreate/onOpen/onRunning/onClose）
  chat_engine.h/.cpp                 ← 状态机 + 事件循环 + 任务编排（借鉴 application.cc 核心）
  chat_ui.h/.cpp                     ← 文本气泡/状态提示/激活码全屏 UI
  avatar_view.h/.cpp                 ← 表情 Avatar（lv_canvas 绘制）
tools/ws_mock_server.py              ← 编译机验证用 xiaozhi 模拟服务器
```

**依赖变更**：
- `main/idf_component.yml`：+ `78/esp-ml307: ~3.6.5`
- `sdkconfig.defaults`：+ `CONFIG_ESP_TLS_USE_BUNDLE=y`、`CONFIG_MBEDTLS_DYNAMIC_BUFFER=y`、`CONFIG_MBEDTLS_SSL_KEEP_PEER_CERTIFICATE=n`
- `main/apps/apps.h` + `main/main.cpp`：注册 `AppAiChat`（VoiceCube 之后）
- AppVoiceCube 的 `opus_encoder.c` **上移**为 `ai_opus.cc`（保留 `audio_encoder_*` 符号兼容，仅改 include 路径，不复制）

## 4. 音频链路设计

### 上行（44.1k → 16k → Opus 60ms → WS）
仿 `vc_record` 任务：`audioRecord(100ms 块) → resample_linear(44.1k→16k) → 攒 960 样本 → ai_opus encode → BinaryProtocol2 打包（timestamp=esp_timer_get_time()/1000）→ ws.Send()`。
- 不加发送队列（同步发送背压即简化）；编码参数复用现有 wrapper（28kbps/complexity3/VBR，服务器可解码）

### 下行（WS → 解码 → 重采样 → 播放）
`OnData(binary) → 解析 → 解码队列 → audio_decoder_decode → resample 到 44.1k → 应用缓冲`。播放节流（`audioPlay` 是整体替换+notify 语义，**不能逐帧调用**）：
1. 累积 ~300ms 起播 → 2. `while (getAudioBusy()) delay(10)` 等通道空闲 → 3. 取 ≤200ms 块 `audioPlay(chunk, true)` 循环 → 4. abort = 清缓冲 + `audioPlay(空)` 打断 + decoder reset
- 解码器按服务器 `audio_params.sample_rate` 配置（默认 16k/60ms，服务器可改 → 重建 decoder + 重采样器）

### 无 AEC 半双工（已知限制）
- Speaking 不录音（tts start 即停录音任务）、Listening 不播放（服务器 tts start 前不下发音频，天然错峰）
- 录音任务 `AudioMutex::acquireRecord()` 全程持有、播放任务 `acquirePlay()` 会话级持有（挡住 FFT 与其它 App）

## 5. 对话状态机（chat_engine）

状态：`Idle / Activating / Connecting / Listening / Speaking`

- **事件模型**：`EventGroupHandle_t`（USER_START/USER_STOP/USER_ABORT/NET_DISCONNECTED/SCHEDULE/STATE_CHANGED）+ `Schedule(std::function)` 队列（mutex+deque）；协议/网络回调只置位或入队，状态与 UI 全部收敛到 `onRunning` 主循环（规避 LVGL 竞态）
- **控制消息**：Listening 进 `listen start`（**hold-to-talk 侧键 = mode manual**，松开发 `listen stop`；**触摸单点 = mode auto**，服务器 VAD 判停）；打断发 `abort`；session_id 存自 server hello
- **断线/超时**（120s 无下行）→ 关通道回 Idle，不自动重连（省电），下次触发重连；`chat_net` 每 30s `ws->Ping()` 保活

## 6. 激活与配置

1. 进入 App 且 `Settings("websocket").GetString("url")` 为空 → `Activating` 状态
2. 4KB 任务 POST OTA URL → websocket 段写 `Settings("websocket", rw)` 完成激活；`activation.code` → 全屏显示 6 位码 + 提示，10s 轮询（上限 10 次）
3. Client-Id UUID 首次生成（esp_random）持久化，重启不变
4. **配网**：P1 复用 `wifi_manager.connectSta`（NVS "wifi" 凭据即连）；未配网 App 内提示；配网界面列为 P3（config_ap 扩展——badge 现有配网页是图片上传用途，不适合直接复用）

## 7. 表情 Avatar（P2）

`lv_canvas`（PSRAM 240x240 ARGB8888 ≈230KB），参数化绘制，动画由 `onRunning` 帧循环驱动（LvglLockGuard 内）。

| 表情 | 触发 |
|---|---|
| neutral | 默认/Idle |
| happy | `llm.emotion=="happy"`、摇晃互动、tts sentence_start |
| sad / thinking / angry / surprised | 对应 `llm.emotion` |
| talking（嘴型 ~100ms 正弦开合） | Speaking 状态（tts start 起） |

映射优先级：`llm.emotion` > 状态机状态 > neutral；`alert` → 红脸/感叹。

## 8. MCP 设备端工具（P2）

`{"type":"mcp","payload":{jsonrpc}}` 下行 → `ai_mcp` 解析分派，响应经 `SendMcpMessage` 回传。4 个工具（全有现成 HAL）：

| 工具 | 实现 |
|---|---|
| `self.get_device_status` | 组装 JSON（音量/亮度/电量/网络） |
| `self.audio_speaker.set_volume` | `GetHAL().setSpeakerVolume(v, true)` |
| `self.screen.set_brightness` | `GetHAL().setBackLightBrightness(v, true)` |
| `self.reboot` | `GetHAL().reboot()`（经 Schedule 延迟 1s） |

不做：set_theme（无主题系统）、upgrade_firmware（P3 可选）、camera/snapshot。

## 9. 触控/摇晃互动（P2）

- **触摸**：复用 `framework::TouchPad`（轻点 ButtonUp）→ Idle 进 Listening(auto) / Listening 停止 / Speaking 打断
- **按键**：`btnB.isHolding()` = hold-to-talk(manual)；`btnA.wasPressed()` = toggle
- **摇晃**：仿 app_dice（`|ax|+|ay|+|az| > 1.8f`，10s 冷却）→ 语料表随机一条（10~20 条中文短句）+ happy 表情 + `vibrate(80,60)`；对话中注入 `{"type":"listen","state":"detect","text":"<语料>"}`（借鉴 WakeWordInvoke，不走 abort 路径）

## 10. 任务与线程模型

| 任务 | 栈 | 优先级 | 职责 | 生命周期 |
|---|---|---|---|---|
| chat_net | 6KB | 2 | WS 连接/心跳/OnData 分发/超时 | onOpen 创建，onClose 删除 |
| chat_rec | 8KB | 3 | 录音→重采样→编码→发送 | Listening 进出 |
| chat_play | 6KB | 3 | 解码→重采样→节流播放 | Speaking 进出 |
| activation | 4KB | 2 | 激活轮询（一次性） | Activating 时 |

- LVGL 仅在 onOpen/onRunning/onClose（LvglLockGuard）触碰；任务只置事件位/入队
- 音频帧队列：`std::deque + mutex + xTaskNotifyGive`（仿 hal_alarm AlarmController 模式）

## 11. 分区与内存

- **分区表不动**：双 OTA 4.9MB 足够（增量 ≈180KB：esp-ml307 网络层 60-80KB + ai_chat 60KB + opus decoder 30KB + CA bundle 10-15KB）；无 model 分区需求（无 ESP-SR）
- SRAM 预算 ≈105KB 内部（opus decoder 56KB 先 INTERNAL 后 PSRAM fallback），S3 可承受

## 12. 里程碑与验证

### P0 — 骨架 + 网络冒烟（编译机）
- idf_component.yml + sdkconfig 三项；注册空 App；EspNetwork 冒烟（`CreateHttp()->Open("GET", ota_url)` 打 HTTP 状态码）
- **验证**：`idf.py build` 通过（含 NimBLE 共存无编译冲突）；真机串口日志 HTTP 200

### P1 — 核心对话链路（编译机 + 真机）
- ai_opus（encoder 上移 + decoder）、ai_resample、ai_protocol、ai_websocket_protocol、ai_ota、chat_engine（4 任务）、最小 chat_ui、hold-to-talk + 触摸 toggle、激活码 UI
- **编译机**：`tools/ws_mock_server.py` 校验 hello 字段 / server hello / listen JSON / BinaryProtocol2 音频帧 / tts 下行 + 预生成 Opus 帧验证解码
- **真机**：xiaozhi.me 激活 → 语音一问一答；侧键打断；播放中触摸打断；拔网线回 Idle 重按重连；Speaking 期间录音任务已停（串口日志）

### P2 — 表情 + MCP + 互动
- avatar_view（7 表情 + 嘴型动画）、文本气泡、ai_mcp 四工具、摇晃互动 + 语料表、llm.emotion 映射
- **验证**：真机对话观察表情联动与嘴型；xiaozhi.me 控制台 MCP 调用（"把音量调到 50"）；摇晃触发语料 + 10s 冷却生效

### P3 — 可选
- 配网界面（config_ap 扩展 WiFi 配网页）；OTA 固件升级（需服务器侧放固件）；官方仅返回 mqtt 配置时的降级提示（或补 mqtt_protocol 移植，成本低）

## 13. 风险清单

| # | 风险 | 应对 |
|---|---|---|
| 1 | xiaozhi.me 激活/注册限制（新硬件按 MAC+board 管理，board 字段格式不识别可能不返回配置） | P1 先打日志确认响应结构，必要时对齐官方 m5stack-core-s3 字段 |
| 2 | 官方可能优先返回 mqtt 配置（实测仅有 mqtt 段） | 记已知限制，或 P3 补 mqtt_protocol 移植 |
| 3 | 无 AEC 回声（喇叭与麦克风同体） | 半双工缓解；auto 靠服务器 VAD、manual 靠松键；测试期限制最大音量 |
| 4 | 线性重采样无抗混叠（44.1k↔16k） | 语音可接受；服务器改 24k/48k 时失真略增 |
| 5 | TLS：esp_crt_bundle 需含 api.tenclass.net 证书链 | 默认主流 CA 包包含；自定义证书则换 `esp_crt_bundle_set()` |
| 6 | NimBLE 与 WiFi 共存 RF 竞争 | 语音会话期间停 BLE 广播（复用 VoiceCube stopAdvertising 模式） |
| 7 | audioPlay 打断粒度 512 样本（11.6ms） | 块间隙 <30ms；若咔哒声则播放块加大至 300ms |
| 8 | settings nvs_commit 潜在 abort（stackchan BUGFIX.md 记录过） | 实施时核对目标 settings.cc 的 commit 错误处理 |
| 9 | esp-ml307 引入 modem 代码（编译期存在） | gc-sections 裁掉（目标工程默认开启，P0 验证） |

## 14. 移植蓝本（源文件）

| 来源 | 文件 | 用途 |
|---|---|---|
| stackchan | `main/protocols/websocket_protocol.cc` + `protocol.cc` | 协议层（BinaryProtocol2/hello/头字段） |
| stackchan | `main/application.cc` | 状态机事件流/tts 分发/abort/激活逻辑 |
| stackchan | `main/ota.cc` | 激活流程 + websocket 配置落 NVS |
| 目标仓库 | `main/apps/app_voicecube/app_voicecube.cpp` | 录音/重采样/编码任务范式 + opus wrapper 上移源 |
| 目标仓库 | `main/framework/power_manager` / `main/hal/hal_alarm.cpp` | 后台任务范式（单例+task+mutex / 懒创建+notify） |
| 目标仓库 | `main/hal/hal_audio.cpp` | audioRecord/audioPlay/getAudioBusy 语义 |
