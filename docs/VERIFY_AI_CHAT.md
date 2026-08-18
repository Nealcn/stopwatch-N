# 小智 AI 对话（阶段三 P1）编译机验证清单

> 对应提交批次：`b7ef5e2`（P0 依赖）→ `8bb1611`（P1 框架层）→ `5c5f1ec`（P1 应用层）→ `e2d4cd4`（工具）。
> 代码在源码工作区编写，本机无 ESP-IDF；以下步骤在**编译机**执行。

## 0. 验证结果（2026-08-18 编译机实测 ✅）

- `idf.py build` 通过（esp-ml307 v3.6.5 组件从注册表拉取成功，含 78__uart-uhci 依赖）；产物 0x3a7890（26% 分区空闲）
- 修复 4 处（已提交）：
  1. `chat_ui.cpp` — `LV_OBJ_FLAG_NONE` 在 LVGL v9.5 不存在 → 改用 add/clear_flag(HIDDEN)
  2. `ai_ota.cc` — 缺 `esp_chip_info.h` include；删未使用变量
  3. `tools/gen_icon_ai_chat.py` — RGB565 值直接写入 uint8_t 数组被截断（0x7e7f→0x7f，图标颜色全错）→ 拆双字节输出（已逐像素校验）
  4. `tools/ws_mock_server.py` — opuslib.encode 不能接收 list → struct.pack 转 bytes（否则 TTS 音频帧下发崩溃）
- `ws_mock_server.py` 全链路自测通过：激活 HTTP / hello 握手 / listen→tts 流（start/sentence_start/Opus 帧/stop）/ BinaryProtocol2 头 / abort
- P2（avatar 表情 + 摇晃互动）已实现并编译通过，见 [AI_CHAT_PLAN.md](AI_CHAT_PLAN.md) §12

## 1. 拉取与依赖

```bash
git fetch mine && git checkout dev && git pull
python3 ./fetch_repos.py        # 新增拉取 78/esp-ml307（WebSocket/HTTP/TLS 网络层）
idf.py build
```

**首次编译重点确认（esp-ml307 v3.6.x API 头文件）**：
| 文件 | 依赖的头文件 | 说明 |
|---|---|---|
| main/framework/ai_chat/ai_network.h | `<network_interface.h>` | NetworkInterface 抽象 |
| main/framework/ai_chat/ai_network.cc | `<esp_network.h>` | EspNetwork 实现 |
| main/framework/ai_chat/ai_websocket_protocol.h | `<web_socket.h>` | WebSocket 类 |
| main/framework/ai_chat/ai_ota.cc | `<http.h>` | Http 类 |

> 若头文件路径有差异仅需调整 include；API 签名（`SetHeader(key,value)`、`Send(data,len,binary)`、
> `Open(method,url)`、`GetStatusCode()`、`ReadAll()`）以编译机实际拉取的组件为准。
> 已知符号：`CreateWebSocket(1)` 返回 `WebSocket*`、`CreateHttp(0)` 返回 `Http*`（均非空检查已做）。

**其它注意**：
- NimBLE 与 esp-ml307 共存无冲突预期（官方支持场景），编译通过即可
- `-ffunction-sections` 默认开启，esp-ml307 的 modem AT 代码会被裁掉
- 若 `CONFIG_ESP_TLS_USE_BUNDLE` 等三项未生效，检查 sdkconfig 缓存（`idf.py menuconfig` 确认或删 build/）

## 2. P0 冒烟：EspNetwork HTTP 通路

临时在 `main/apps/app_ai_chat/app_ai_chat.cpp` 的 `onOpen()` 加一段（验证后移除）：

```cpp
// TODO(verify): P0 冒烟，验证后删除
#include <framework/ai_chat/ai_network.h>
#include <http.h>
static void smoke() {
    auto http = std::unique_ptr<Http>(framework::aichat::GetAiNetwork()->CreateHttp(0));
    http->SetHeader("Accept-Language", "zh-CN");
    if (http->Open("GET", "https://api.tenclass.net/xiaozhi/ota/")) {
        mclog::tagInfo("SMOKE", "HTTP status: {}", http->GetStatusCode());
    } else {
        mclog::tagError("SMOKE", "HTTP open failed, code=0x{:x}", http->GetLastError());
    }
}
```

期望：串口日志 `HTTP status: 200`（证明 WiFi 栈 + TLS bundle + EspNetwork 全链路打通）。

## 3. P1 协议链路：ws_mock_server.py（可在编译机或有 Python 的电脑上跑）

```bash
pip install websockets          # 必需
pip install opuslib             # 可选：下发真实 Opus 音频帧（需系统 libopus）
python tools/ws_mock_server.py --port 8080 --activation-port 8081 --fake-activated
```

设备侧临时把 `ai_ota.cc` 的 `kOtaUrl` 改为 `http://<本机IP>:8081/ota/`（或直接把
`Settings("websocket")` 的 url/token/version 写进 NVS），烧录后进入 AI 对话应用：

| 验证项 | 期望 |
|---|---|
| 进 App 自动激活（--fake-activated） | mock 日志收到 HTTP POST；NVS 写入 websocket 配置 |
| 触摸轻点 / 按 A | 设备发 listen start(auto) |
| 按住 B 说话松开 | 设备发 listen start(manual) → 音频帧（16B 头 v2）→ listen stop |
| mock 下发 tts 流程 | 设备状态 Listening→Speaking，屏显句子，喇叭出声（opuslib 帧） |
| 播报中触摸 | 设备发 abort，播放立即停 |
| 激活码流程（不加 --fake-activated） | 全屏显示 6 位码，轮询直到配置下发 |

## 4. 真机验证（xiaozhi.me 正式激活）

1. 配网（NVS `wifi` 已有凭据即自动连；未配网需先在设置中配置）
2. 进「AI对话」→ 自动激活 → 全屏激活码 → xiaozhi.me 网页端绑定
3. 一问一答：触摸 → 说话 → 松开/自动停 → TTS 播放
4. 打断：播报中触摸/按键
5. 断网：拔网线 → 回 Idle 提示；重按重连
6. 串口日志确认：Speaking 期间录音任务已停（半双工生效）

## 5. 已知限制（P1 验收口径）

- **无 AEC**：半双工（Speaking 不录音）；auto 模式靠服务器 VAD，manual 靠松键；喇叭音量大时麦克风串扰可能误触发服务器 VAD
- **无离线唤醒词**：触摸/按键唤醒（P 系列决策）
- **无连续对话**：每轮问答后回 Idle，需再次触发（无设备端 VAD）
- **MCP 工具**：hello 中 `mcp:false`（P2 落地 ai_mcp 后置 true）
- **配网界面**：P1 仅复用现有 wifi_manager 凭据（P3 规划配网页）

## 6. 验证后反馈

编译/验证问题请按模块反馈：`ai_network`（头文件路径/API 签名）、`ai_websocket_protocol`（WS 行为）、
`ai_ota`（激活响应结构，特别留意 xiaozhi.me 是否识别 `board.type=m5stack-stopwatch`）、
`chat_engine`（状态机/任务）、`ai_opus`（编解码）。
