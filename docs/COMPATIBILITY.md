# VoiceCube 协议兼容性

> 核对时间：2026-08-17，对照 [Nealcn/VoiceCube](https://github.com/Nealcn/VoiceCube) 仓库 main 分支最新代码，与本仓库 `desktop/voicestick/` 逐文件 diff 结论。

## 兼容性矩阵

| 组合（桌面端 × 固件） | 结果 | 说明 |
|------|------|------|
| **本仓库桌面端** + **本仓库固件** | ✅ 完整 | 语音识别 + 屏上预览 + 确认粘贴 + 触摸板鼠标 |
| **本仓库桌面端** + **VoiceCube 固件** | ✅ 基本可用 | 协议层兼容；本桌面端用音频帧 start 标志触发 ASR 会话（不依赖 button_down/up），VoiceCube 固件发送的 button_down/up 会被忽略（无害）。识别文本仍会进剪贴板，用户手动 Ctrl+V 即可（等效参考端流程）。预览/确认粘贴步骤降级为手动 |
| **VoiceCube 桌面端** + **本仓库固件** | ❌ 语音链路不工作 | VoiceCube 桌面端靠 `button_down` / `button_up` 状态事件触发/结束 ASR 会话，本仓库固件**不发送**这两个事件（只发音频帧 + paste_request + mouse），导致 ASR 会话永不启动 |

## 协议层：逐字节一致

以下内容与 VoiceCube 桌面端完全一致（diff 零差异）：

| 项 | 值 |
|----|----|
| BLE Service UUID | `8f2f0b84-6e6f-4b23-88f7-3a3ceafc5100` |
| audio_tx（notify） | `8f2f0b84-6e6f-4b23-88f7-3a3ceafc5101` |
| state_tx（notify） | `8f2f0b84-6e6f-4b23-88f7-3a3ceafc5102` |
| control_rx（write） | `8f2f0b84-6e6f-4b23-88f7-3a3ceafc5103` |
| OTA_RX / OTA_STATE | `...5104` / `...5105`（保留） |
| 音频帧 | 16B 头（小端）：`version=1, type=0x01, header_len, reserved, session_id<u32>, seq<u32>, flags<u8>, reserved<u8>, payload_len<u16>`；flags bit0=start, bit1=end；payload = Opus 60ms 帧 |
| ASR 客户端 | `asr_client.py` + `asr_protocol.py` 与参考端字节级相同（火山引擎 openspeech 流式 WebSocket） |
| 音频封装 | `ogg_opus_muxer.py` 相同 |
| 保活 | `{"event":"ping"}` 每 20 秒（参考端为 20s，本仓库同样实现） |
| ui_state 命令 | `{"event":"ui_state","state":"ready"/"error"/"pending_confirmation","text":...}` |

## 本仓库扩展事件（向后兼容）

以下为本仓库新增、VoiceCube 参考端没有的事件。参考端对未知事件仅记日志，不崩溃：

| 事件 | 通道 | 方向 | 说明 |
|------|------|------|------|
| `{"event":"mouse","dx":..,"dy":..,"btn":..,"action":"move/down/up"}` | state_tx | 设备 → 桌面端 | 触摸板鼠标事件（本工程扩展，桌面端 SendInput 注入） |
| `{"event":"asr","text":..}` | control_rx | 桌面端 → 设备 | ASR 结果下行预览 |
| `{"event":"paste_request"}` | state_tx | 设备 → 桌面端 | 用户点「确认」→ 请求桌面端 Ctrl+V |
| `{"event":"paste_result","ok":..}` | control_rx | 桌面端 → 设备 | 粘贴结果回执 |

## 交互流程差异（重要）

| 环节 | VoiceCube（参考） | 本仓库 |
|------|-------------------|--------|
| 会话触发 | `button_down` / `button_up`（带 session_id / duration_ms） | 音频帧 start/end flag |
| 粘贴方式 | 桌面端自动复制到剪贴板，用户手动 Ctrl+V（`paste_on_final`） | 设备屏上预览 → 用户点「确认」→ `paste_request` → 桌面端自动 Ctrl+V → `paste_result` 回执 |
| 结果展示 | 无屏幕（LED 状态：pending_confirmation / ready / error） | 屏上大字预览 + 确认/取消按钮 |
| 桌面端形态 | PyQt5 GUI（悬浮球/设置面板/字幕）+ LLM 翻译润色 | CLI 无 GUI（参考端功能子集 + 鼠标注入扩展） |

## 结论

1. **「本仓库桌面端兼容 VoiceCube」成立**：协议层（UUID / 帧格式 / ASR 接口 / 保活）逐字节一致，本仓库桌面端可直接对接 VoiceCube 固件与硬件，且无 PyQt5 依赖、可纯 CLI 运行。
2. **反向不成立**：VoiceCube 官方桌面端连接本仓库固件时，因缺 `button_down` / `button_up` 状态事件，语音识别链路不工作。
3. 若需**双向兼容**，本仓库固件侧补发 `button_down`/`button_up` 事件即可（桌面端对未知事件容忍，无破坏性）。
