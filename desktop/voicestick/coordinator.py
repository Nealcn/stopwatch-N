"""协调器 — BLE + ASR + 鼠标/剪贴板注入（简化版，无 PyQt5 依赖）

流程（预览→定位→确认粘贴）：
  1. 设备音频帧 → Opus 原始包 → AsrClient（内部 OggOpus 封装 + 云 ASR）
  2. ASR 最终结果 → 写入剪贴板暂存（不粘贴）+ 下行到设备预览
  3. 设备 mouse 帧 → SendInput 注入鼠标移动/左右键
  4. 设备 paste_request → Ctrl+V 粘贴（剪贴板已有文本）→ 回执 paste_result
"""
import asyncio
import json
import logging

from .protocol import StateEvent, AudioFrame, MouseEvent
from .ble import BleClient
from .asr_client import AsrClient
from . import input_injector

logger = logging.getLogger(__name__)


class Coordinator:
    def __init__(self, ble: BleClient, asr: AsrClient, mouse_gain: float = 1.0):
        self._ble = ble
        self._asr = asr
        self._pending_text = ""          # 剪贴板暂存文本（等待确认粘贴）
        self._session_started = False
        self._audio_queue: asyncio.Queue = asyncio.Queue(maxsize=32)
        # 触摸板 move 微批处理（合并注入 + 桌面端增益补偿）
        self._mouse_batch = input_injector.MouseBatch(gain=mouse_gain)

        # 状态回调（CLI 打印 / GUI 转发）
        self.on_status = None
        # UI 回调（GUI 模式由 app.py 注入；None 时静默）
        self.on_partial_text = None
        self.on_final_text = None
        self.on_device_connected = None
        self.on_device_disconnected = None
        # 剪贴板写入回调（GUI 用 Qt 剪贴板；None 时回退 input_injector）
        self.clipboard_callback = None

        # BLE 回调
        ble.on_audio_frame = self._on_audio_frame
        ble.on_state_event = self._on_state_event
        ble.on_mouse_event = self._on_mouse_event
        ble.on_connected = self._on_ble_connected
        ble.on_disconnected = self._on_ble_disconnected

        # ASR 回调
        asr.on_partial = self._on_asr_partial
        asr.on_final = lambda t: asyncio.create_task(self._on_asr_final(t))
        asr.on_error = lambda m: self._set_status(f"ASR 错误: {m}")

        self._consume_task = None
        self._keepalive_task = None

    def _set_status(self, text: str):
        logger.info("[状态] %s", text)
        if self.on_status:
            self.on_status(text)

    # ---------------- 生命周期 ----------------

    async def start(self):
        await self._asr.start()
        self._consume_task = asyncio.create_task(self._consume_audio())
        self._keepalive_task = asyncio.create_task(self._keepalive())

    async def shutdown(self):
        for t in (self._consume_task, self._keepalive_task):
            if t:
                t.cancel()
        await self._asr.stop()

    # ---------------- BLE 回调 ----------------

    def _on_ble_connected(self, name: str):
        self._set_status(f"已连接 {name}")
        if self.on_device_connected:
            self.on_device_connected(name)

    def _on_ble_disconnected(self):
        self._set_status("连接断开，等待重连…")
        if self.on_device_disconnected:
            self.on_device_disconnected()

    def _on_asr_partial(self, text: str):
        logger.info("[ASR 部分] %s", text)
        if self.on_partial_text:
            self.on_partial_text(text)

    def _on_audio_frame(self, frame: AudioFrame):
        # 首帧：新会话
        if frame.is_start() or not self._session_started:
            self._session_started = True
            self._set_status("录音会话开始")
            asyncio.create_task(self._asr.start_session())
        try:
            self._audio_queue.put_nowait(frame)
        except asyncio.QueueFull:
            logger.warning("音频队列满，丢帧 seq=%d", frame.seq)

    def _on_state_event(self, event: StateEvent):
        if event.event == "paste_request":
            # 设备请求粘贴：剪贴板里的是待贴文本
            self._set_status("收到粘贴请求 → Ctrl+V")
            asyncio.create_task(self._paste())
        elif event.event == "voice":
            logger.info("[设备] %s", event.duration_ms if event.duration_ms else event.button)
        else:
            logger.info("[设备事件] %s", event.event)

    def _on_mouse_event(self, ev: MouseEvent):
        if ev.is_move():
            # 微批处理：5ms 窗口合并注入 + mouse_gain 补偿；点击直发不排队
            self._mouse_batch.move(ev.dx, ev.dy)
        elif ev.action == "down":
            self._mouse_batch.flush()  # 先 flush 残余位移，再点击
            input_injector.mouse_button(ev.btn, True)
        elif ev.action == "up":
            input_injector.mouse_button(ev.btn, False)

    # ---------------- 音频消费（队列 → ASR） ----------------

    async def _consume_audio(self):
        while True:
            frame: AudioFrame = await self._audio_queue.get()
            try:
                await self._asr.send_audio(bytes(frame.payload), is_last=frame.is_end())
            except Exception as e:
                logger.error("发送音频到 ASR 失败: %s", e)
            if frame.is_end():
                self._set_status("录音结束，等待识别…")

    # ---------------- ASR 结果 ----------------

    async def _on_asr_final(self, text: str):
        if not text.strip():
            self._set_status("识别为空")
            self._session_started = False
            return
        self._pending_text = text
        self._session_started = False
        # 1) 剪贴板暂存（不粘贴，等设备确认）
        if self.clipboard_callback:
            self.clipboard_callback(text)
        else:
            input_injector.copy_to_clipboard(text)
        # UI 预览
        if self.on_final_text:
            self.on_final_text(text)
        self._set_status(f"识别完成（已暂存，等待粘贴确认）: {text}")
        # 2) 下行到设备预览
        payload = json.dumps({"event": "asr", "text": text}, ensure_ascii=False).encode("utf-8")
        await self._ble.send_control(payload)

    # ---------------- 粘贴 ----------------

    async def _paste(self):
        ok = False
        if self._pending_text:
            ok = input_injector.paste_text(self._pending_text)
        else:
            logger.warning("粘贴请求但没有待贴文本")
        payload = json.dumps({"event": "paste_result", "ok": ok}).encode("utf-8")
        await self._ble.send_control(payload)
        self._set_status("已粘贴" if ok else "粘贴失败")

    # ---------------- 保活 ----------------

    async def _keepalive(self):
        """每 20 秒发空控制包防止桌面端空闲断连（参照原版）"""
        while True:
            await asyncio.sleep(20)
            try:
                await self._ble.send_control(b"{\"event\":\"ping\"}")
            except Exception:
                pass
