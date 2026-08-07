#!/usr/bin/env python3
"""StopWatch 桌面端（VoiceCube 模式）— 语音识别 + 触摸板鼠标注入

用法：
  1. 安装依赖：pip install bleak aiohttp
  2. 配置 ASR：编辑 ~/.voicestick/config.json 填入 asr_api_key
  3. 运行：python main.py

流程：扫描 VS-* 设备 → 连接 → 语音识别（预览→确认粘贴）+ 触摸板鼠标
"""
import asyncio
import logging
import sys

if sys.platform == "win32":
    asyncio.set_event_loop_policy(asyncio.WindowsSelectorEventLoopPolicy())

from voicestick.config import AppConfig
from voicestick.ble import BleClient
from voicestick.asr_client import AsrClient
from voicestick.coordinator import Coordinator


async def _find_device(config: AppConfig, timeout: float = 5.0):
    ble = BleClient()
    devices = await ble.scan(timeout)
    if config.last_connected_address:
        for d in devices:
            if d["address"] == config.last_connected_address:
                logging.info("找到上次连接的设备: %s (%s)", d["name"], d["address"])
                return d
    for d in devices:
        if d["name"].startswith(config.device_name_filter):
            logging.info("找到设备: %s (%s)", d["name"], d["address"])
            return d
    return None


async def main():
    logging.basicConfig(
        level=logging.INFO,
        format="%(asctime)s [%(levelname)s] %(name)s: %(message)s",
    )
    config = AppConfig.load()
    if not config.asr_api_key:
        logging.warning("ASR API Key 未配置：编辑 %s 填入 asr_api_key 后才能识别语音", config.CONFIG_PATH)
        logging.warning("（鼠标/触摸板功能不受影响）")

    device = await _find_device(config)
    if device is None:
        logging.error("未找到 VoiceCube 设备（扫描 5 秒），请确认 StopWatch 已进入 VoiceCube 模式")
        return

    ble = BleClient()
    asr = AsrClient(config.asr_server_url, config.asr_api_key)
    coord = Coordinator(ble, asr)

    try:
        await ble.connect(device["address"], device["name"], timeout=15)
        config.last_connected_address = device["address"]
        config.last_connected_name = device["name"]
        config.save()

        await coord.start()
        logging.info("=== 桌面端就绪：按住 StopWatch 侧键说话；触摸屏 = 鼠标 ===")
        while True:
            await asyncio.sleep(1)
    except asyncio.CancelledError:
        pass
    finally:
        await coord.shutdown()
        await ble.disconnect()


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        print("\n已退出")
