#!/usr/bin/env python3
"""StopWatch 桌面端（语音输入模式）— 系统托盘 + 悬浮球 GUI

用法：
  1. 安装依赖：pip install -r requirements.txt
  2. 配置 ASR：编辑 ~/.voicestick/config.json 填入 asr_api_key
  3. 运行：python main.py            # GUI（托盘 + 悬浮球）
           python main.py --cli     # 无界面命令行模式

流程：扫描 VS-* 设备 → 连接 → 语音识别（预览→确认粘贴）+ 触摸板鼠标
"""
import sys
import os
import asyncio
import logging

# Windows BLE (bleak) 需要 Selector 事件循环
if sys.platform == "win32":
    asyncio.set_event_loop_policy(asyncio.WindowsSelectorEventLoopPolicy())

# 确保能找到 voicestick 包
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from voicestick.config import AppConfig


def _cli_main():
    """无界面模式（原 CLI 逻辑）"""
    from voicestick.ble import BleClient
    from voicestick.asr_client import AsrClient
    from voicestick.coordinator import Coordinator

    async def main():
        config = AppConfig.load()
        if not config.asr_api_key:
            logging.warning("ASR API Key 未配置：编辑 %s 填入 asr_api_key 后才能识别语音", config.CONFIG_PATH)
            logging.warning("（鼠标/触摸板功能不受影响）")

        ble = BleClient()
        asr = AsrClient(config.asr_server_url, config.asr_api_key)
        coord = Coordinator(ble, asr)

        # 按名字前缀扫描
        devices = await ble.scan(5.0)
        device = next((d for d in devices if (d.get("name") or "").startswith(config.device_name_filter)), None)
        if device is None:
            logging.error("未找到设备（扫描 5 秒），请确认 StopWatch 已进入语音输入模式")
            return

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

    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        print("\n已退出")


def _gui_main():
    """GUI 模式：系统托盘 + 悬浮球"""
    from PyQt5.QtWidgets import QApplication
    from PyQt5.QtCore import Qt
    from PyQt5.QtGui import QFont

    from voicestick.app import VoiceStickApp

    # 高 DPI 支持
    QApplication.setHighDpiScaleFactorRoundingPolicy(Qt.HighDpiScaleFactorRoundingPolicy.PassThrough)
    QApplication.setAttribute(Qt.AA_EnableHighDpiScaling, True)

    app = QApplication(sys.argv)
    app.setApplicationName("语音输入")
    app.setQuitOnLastWindowClosed(False)  # 托盘常驻

    # 中文字体
    app.setFont(QFont("Microsoft YaHei", 9))

    vs = VoiceStickApp(app)
    vs.start()

    sys.exit(app.exec_())


if __name__ == "__main__":
    logging.basicConfig(
        level=logging.INFO,
        format="%(asctime)s [%(levelname)s] %(name)s: %(message)s",
    )
    if "--cli" in sys.argv:
        _cli_main()
    else:
        _gui_main()
