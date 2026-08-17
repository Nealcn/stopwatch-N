#!/usr/bin/env python3
"""小智（xiaozhi）协议模拟服务器 — 编译机/无真机环境验证 AI 对话链路

用法：
  pip install websockets           # 必需
  pip install opuslib              # 可选：生成真实 Opus 音频帧验证下行解码

  python ws_mock_server.py [--port 8080] [--activation-port 8081]

验证内容（对照 AI_CHAT_PLAN.md P1 编译机验证项）：
  1. 客户端 hello 字段校验（version/transport/audio_params）
  2. 服务器 hello 下发（session_id + audio_params）
  3. listen start/stop/abort JSON 事件接收
  4. BinaryProtocol2 音频帧接收（大端 16B 头解析）
  5. 下行 tts 流程：收到 listen start 后自动下发
     tts start → sentence_start → Opus 音频帧（可选）→ tts stop
  6. 激活码流程（HTTP POST /ota/ 模拟，见下方说明）

注意：
  - 设备端激活 URL 是硬编码的 xiaozhi.me（main/framework/ai_chat/ai_ota.cc kOtaUrl），
    本地全链路测试需临时改为 http://<本机IP>:8081/ota/。
  - 激活端点默认返回 activation.code（模拟"新设备需绑定"）；
    收到正确绑定后（--fake-activated）返回 websocket 配置指向本服务器。
"""
import argparse
import asyncio
import json
import logging
import math
import random
import struct
import time

import websockets

log = logging.getLogger("mock")

BINARY_VERSION = 2
SERVER_SAMPLE_RATE = 16000
SERVER_FRAME_MS = 60


def parse_binary_proto2(data: bytes) -> dict:
    """大端 16B 头：version u16 | type u16 | reserved u32 | timestamp u32 | payload_size u32"""
    if len(data) < 16:
        return None
    # 大端：version, type 为 u16；reserved, timestamp, payload_size 为 u32
    version, type_, reserved, ts, size = struct.unpack(">HHIII", data[0:16])
    payload = data[16:16 + size]
    return {
        "version": version,
        "type": type_,
        "reserved": reserved,
        "timestamp": ts,
        "payload_size": size,
        "payload": payload,
    }


def make_opus_sine_packets(frame_ms=60, sample_rate=16000, count=8, freq=440.0, amp=0.2):
    """用 opuslib 生成 count 帧正弦波 Opus 包（无 opuslib 时返回空列表）"""
    try:
        import opuslib
    except ImportError:
        log.warning("opuslib 未安装，跳过音频下行（仅 JSON 流程）")
        return []

    enc = opuslib.Encoder(sample_rate, 1, opuslib.APPLICATION_AUDIO)
    frame_samples = sample_rate * frame_ms // 1000
    packets = []
    for i in range(count):
        # 每帧加轻微频率抖动，便于确认设备连续解码
        f = freq * (1 + 0.02 * math.sin(i / 2.0))
        pcm = [
            int(amp * 32767 * math.sin(2 * math.pi * f * t / sample_rate))
            for t in range(i * frame_samples, (i + 1) * frame_samples)
        ]
        packets.append(enc.encode(pcm, frame_samples))
    return packets


def make_binary_proto2(payload: bytes, timestamp_ms: int) -> bytes:
    return struct.pack(">HHIII", BINARY_VERSION, 0, 0, timestamp_ms, len(payload)) + payload


class Session:
    """一个设备连接会话"""

    def __init__(self, ws, fake_activated: bool):
        self.ws = ws
        self.fake_activated = fake_activated
        self.session_id = f"mock-{random.randrange(100000, 999999)}"
        self.hello = None
        self.frame_count = 0

    async def send_json(self, obj: dict):
        await self.ws.send(json.dumps(obj, ensure_ascii=False))

    async def handle_hello(self, hello: dict):
        log.info("客户端 hello: %s", json.dumps(hello, ensure_ascii=False))
        assert hello.get("type") == "hello", "hello.type 缺失"
        assert hello.get("version") in (2, 3), f"hello.version 异常: {hello.get('version')}"
        ap = hello.get("audio_params", {})
        assert ap.get("format") == "opus", f"audio_params.format 异常: {ap}"
        assert ap.get("sample_rate") == 16000, f"audio_params.sample_rate 异常: {ap}"
        assert ap.get("frame_duration") == 60, f"audio_params.frame_duration 异常: {ap}"

        # 服务器 hello
        await self.send_json({
            "type": "hello",
            "transport": "websocket",
            "session_id": self.session_id,
            "audio_params": {
                "format": "opus",
                "sample_rate": SERVER_SAMPLE_RATE,
                "frame_duration": SERVER_FRAME_MS,
            },
        })
        log.info("已下发服务器 hello (session=%s)", self.session_id)

    async def handle_json(self, msg: dict):
        mtype = msg.get("type")
        log.info("[JSON] %s", json.dumps(msg, ensure_ascii=False))

        if mtype == "listen" and msg.get("state") == "start":
            # 模拟服务器处理：约 1.5s 后返回 TTS 结果
            log.info("=== 收到聆听开始 (mode=%s)，模拟 TTS 回复 ===", msg.get("mode"))
            asyncio.create_task(self.tts_flow())

        elif mtype == "listen" and msg.get("state") == "detect":
            log.info("=== 收到文本注入: %s ===", msg.get("text"))
            # 也可回一段 TTS 应答
            asyncio.create_task(self.tts_flow(text=f"收到：{msg.get('text')}"))

        elif mtype == "abort":
            log.info("=== 设备打断 ===")

        elif mtype == "mcp":
            log.info("=== MCP 消息（P2 未处理）===")

    async def tts_flow(self, text: str = "你好，我是小智，很高兴为你服务。"):
        await asyncio.sleep(1.5)
        await self.send_json({"type": "tts", "state": "start"})
        await asyncio.sleep(0.2)
        await self.send_json({"type": "tts", "state": "sentence_start", "text": text})
        packets = make_opus_sine_packets()
        for p in packets:
            await self.ws.send(make_binary_proto2(p, int(time.time() * 1000) & 0xFFFFFFFF))
            await asyncio.sleep(0.05)
        await asyncio.sleep(0.5)
        await self.send_json({"type": "tts", "state": "stop"})
        log.info("=== TTS 流程完成 ===")


async def ws_handler(ws, fake_activated: bool):
    session = Session(ws, fake_activated)
    log.info("设备已连接: %s", ws.remote_address)
    try:
        async for raw in ws:
            if isinstance(raw, (bytes, bytearray)):
                parsed = parse_binary_proto2(bytes(raw))
                if parsed is None:
                    log.warning("音频帧头解析失败: %d 字节", len(raw))
                    continue
                session.frame_count += 1
                if session.frame_count <= 3 or session.frame_count % 100 == 0:
                    log.info("[AUDIO] v=%d type=%d ts=%d size=%d (第 %d 帧)",
                             parsed["version"], parsed["type"], parsed["timestamp"],
                             parsed["payload_size"], session.frame_count)
            else:
                try:
                    msg = json.loads(raw)
                except json.JSONDecodeError:
                    log.warning("非法 JSON: %.120s", raw)
                    continue
                if msg.get("type") == "hello":
                    session.hello = msg
                    await session.handle_hello(msg)
                else:
                    await session.handle_json(msg)
    except websockets.exceptions.ConnectionClosed:
        pass
    finally:
        log.info("设备断开 (共收音频帧 %d)", session.frame_count)


# ---------------------------------------------------------------- 激活端点（HTTP）

async def activation_handler(reader, writer, fake_activated: bool):
    request = (await reader.read(65536)).decode("utf-8", "replace")
    first_line = request.splitlines()[0] if request else ""
    log.info("[HTTP] %s", first_line)

    if fake_activated:
        body = json.dumps({
            "websocket": {
                "url": "ws://<本机IP>:8080/",
                "token": "mock-token",
                "version": 2,
            },
            "server_time": {"timestamp": int(time.time() * 1000), "timezone_offset": 480},
        }, ensure_ascii=False)
    else:
        # 模拟"新设备需绑定"：下发激活码，设备端会全屏展示并轮询
        code = "".join(str(random.randrange(0, 10)) for _ in range(6))
        body = json.dumps({"activation": {"code": code, "message": "请在 xiaozhi.me 绑定设备"}})

    resp = (
        "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n"
        f"Content-Length: {len(body.encode())}\r\nConnection: close\r\n\r\n" + body
    )
    writer.write(resp.encode())
    await writer.drain()
    writer.close()


async def main():
    parser = argparse.ArgumentParser(description="小智协议模拟服务器")
    parser.add_argument("--port", type=int, default=8080, help="WebSocket 端口")
    parser.add_argument("--activation-port", type=int, default=8081, help="激活 HTTP 端口")
    parser.add_argument("--fake-activated", action="store_true",
                        help="激活端点直接返回 websocket 配置（模拟已绑定设备）")
    parser.add_argument("--log-level", default="INFO")
    args = parser.parse_args()

    logging.basicConfig(level=getattr(logging, args.log_level.upper()),
                        format="%(asctime)s [%(levelname)s] %(name)s: %(message)s")

    async def handler(ws):
        await ws_handler(ws, args.fake_activated)

    async def activation():
        server = await asyncio.start_server(
            lambda r, w: activation_handler(r, w, args.fake_activated),
            "0.0.0.0", args.activation_port)
        log.info("激活端点: http://0.0.0.0:%d/ota/", args.activation_port)
        async with server:
            await server.serve_forever()

    async with websockets.serve(handler, "0.0.0.0", args.port, max_size=65536):
        log.info("=== 小智协议模拟服务器就绪 ===")
        log.info("WebSocket: ws://0.0.0.0:%d/", args.port)
        log.info("设备侧配置: url=ws://<本机IP>:%d/ token=mock-token version=2", args.port)
        log.info("提示: 设备激活 URL 需临时改为 http://<本机IP>:%d/ota/（ai_ota.cc kOtaUrl）",
                 args.activation_port)
        await asyncio.gather(activation(), asyncio.Future())


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        print("\n已退出")
