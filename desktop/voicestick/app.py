"""StopWatch 桌面端主应用 — 系统托盘 + 悬浮球（参考原 VoiceCube 的 UI 结构）

架构：PyQt5 主线程（托盘 + 悬浮球）+ 后台线程跑 asyncio（BLE/ASR/协调器）。
状态回调经 pyqtSignal 桥接跨线程更新 UI。
"""
import asyncio
import logging
import threading
from typing import Optional

from PyQt5.QtWidgets import (
    QApplication, QSystemTrayIcon, QMenu, QMessageBox, QAction,
)
from PyQt5.QtCore import Qt, QObject, pyqtSignal
from PyQt5.QtGui import QIcon, QPixmap, QPainter, QColor, QPen

from .config import AppConfig
from .ble import BleClient
from .asr_client import AsrClient
from .coordinator import Coordinator
from .ui.floatball import FloatingBallWindow

logger = logging.getLogger(__name__)


class _UI_Bridge(QObject):
    """跨线程 UI 桥：后台 asyncio 线程 → 主线程（Qt 信号线程安全）"""
    status = pyqtSignal(str)
    partial_text = pyqtSignal(str)
    final_text = pyqtSignal(str)
    device_connected = pyqtSignal(str)
    device_disconnected = pyqtSignal()


class VoiceStickApp:
    def __init__(self, qapp: QApplication):
        self._qapp = qapp
        self._config = AppConfig.load()

        self._ble = BleClient()
        self._asr = AsrClient(self._config.asr_server_url, self._config.asr_api_key)
        self._coordinator = Coordinator(self._ble, self._asr)

        self._bridge = _UI_Bridge()
        self._bridge.status.connect(self._on_status)
        self._bridge.partial_text.connect(self._on_partial_text)
        self._bridge.final_text.connect(self._on_final_text)
        self._bridge.device_connected.connect(self._on_ble_connected)
        self._bridge.device_disconnected.connect(self._on_ble_disconnected)

        # UI
        self._tray: Optional[QSystemTrayIcon] = None
        self._tray_menu: Optional[QMenu] = None
        self._floatball = FloatingBallWindow()
        self._floatball.position_changed.connect(self._save_floatball_pos)

        # 状态
        self._loop: Optional[asyncio.AbstractEventLoop] = None
        self._loop_thread: Optional[threading.Thread] = None
        self._reconnect_task = None

        # 协调器回调（后台线程触发，经桥转发主线程）
        self._coordinator.on_status = self._bridge.status.emit
        self._coordinator.on_partial_text = self._bridge.partial_text.emit
        self._coordinator.on_final_text = self._bridge.final_text.emit
        self._coordinator.on_device_connected = self._bridge.device_connected.emit
        self._coordinator.on_device_disconnected = self._bridge.device_disconnected.emit
        self._coordinator.clipboard_callback = QApplication.clipboard().setText

    # ---- 生命周期 ----

    def start(self):
        self._loop = asyncio.new_event_loop()
        asyncio.set_event_loop(self._loop)

        self._setup_tray()
        self._floatball.load_pos(self._config.floatball_x, self._config.floatball_y)
        self._floatball.show()
        self._coordinator.on_status("启动中…")

        # asyncio 事件循环运行在后台线程
        self._loop_thread = threading.Thread(target=self._run_loop, daemon=True)
        self._loop_thread.start()

        # 异步初始化 + 自动连接
        asyncio.run_coroutine_threadsafe(self._init_async(), self._loop)

    def _run_loop(self):
        asyncio.set_event_loop(self._loop)
        self._loop.run_forever()

    async def _init_async(self):
        await self._coordinator.start()
        asyncio.create_task(self._auto_reconnect())

    def shutdown(self):
        if self._loop:
            self._loop.call_soon_threadsafe(self._loop.stop)
            if self._loop_thread:
                self._loop_thread.join(timeout=3)
            self._loop.close()

    # ---- 系统托盘 ----

    def _setup_tray(self):
        self._tray_menu = QMenu()

        status_action = self._tray_menu.addAction("状态: 启动中")
        status_action.setEnabled(False)
        self._status_action = status_action

        self._tray_menu.addSeparator()

        scan_action = self._tray_menu.addAction("重新扫描连接")
        scan_action.triggered.connect(self._manual_scan)

        about_action = self._tray_menu.addAction("关于")
        about_action.triggered.connect(self._show_about)

        self._tray_menu.addSeparator()

        quit_action = self._tray_menu.addAction("退出")
        quit_action.triggered.connect(self._quit)

        icon = self._make_icon()
        self._tray = QSystemTrayIcon(icon, self._qapp.activeWindow())
        self._tray.setContextMenu(self._tray_menu)
        self._tray.setToolTip("语音输入 — 连接中…")
        self._tray.activated.connect(self._on_tray_activated)
        self._tray.show()

    @staticmethod
    def _make_icon() -> QIcon:
        """绘制麦克风托盘图标（蓝色背景 + 白色符号）"""
        pm = QPixmap(22, 22)
        pm.fill(QColor(0x20, 0x80, 0xC0))
        p = QPainter(pm)
        p.setRenderHint(QPainter.Antialiasing)
        p.setPen(QPen(QColor(255, 255, 255), 2))
        p.drawEllipse(8, 3, 6, 6)
        p.drawRect(7, 10, 8, 6)
        p.drawLine(11, 16, 11, 19)
        p.drawLine(7, 19, 15, 19)
        p.end()
        return QIcon(pm)

    def _on_tray_activated(self, reason):
        if reason == QSystemTrayIcon.DoubleClick:
            self._manual_scan()

    # ---- 操作 ----

    def _manual_scan(self):
        self._coordinator.on_status("扫描设备…")
        self._schedule_reconnect(force=True)

    def _show_about(self):
        QMessageBox.about(
            self._qapp.activeWindow(),
            "关于 语音输入",
            "StopWatch 语音输入桌面端\n\n"
            "按住手表侧键说话，识别文字确认后粘贴到当前窗口。\n"
            "触摸屏 = 鼠标（滑动移动 / 轻点左键 / 长按右键）。\n\n"
            "协议: MIT"
        )

    def _quit(self):
        self._tray.hide()
        self.shutdown()
        self._qapp.quit()

    # ---- 回调（主线程） ----

    def _on_status(self, status: str):
        self._status_action.setText(f"状态: {status}")
        self._tray.setToolTip(f"语音输入 — {status}")
        self._floatball.set_status(status)

    def _on_partial_text(self, text: str):
        self._floatball.set_partial_text(text)

    def _on_final_text(self, text: str):
        self._floatball.set_final_text(text)

    def _on_ble_connected(self, device_name: str):
        self._status_action.setText(f"已连接: {device_name}")
        self._tray.setToolTip(f"语音输入 — {device_name}")
        self._floatball.set_connected(True)
        self._config.last_connected_address = self._ble._last_address or ""
        self._config.last_connected_name = self._ble._device_name or device_name
        self._config.save()

    def _on_ble_disconnected(self):
        self._status_action.setText("状态: 已断开（重连中…）")
        self._floatball.set_connected(False)
        self._schedule_reconnect()

    def _save_floatball_pos(self):
        x, y = self._floatball.save_pos()
        self._config.floatball_x = x
        self._config.floatball_y = y
        self._config.save()

    # ---- 自动连接/重连 ----

    def _schedule_reconnect(self, force: bool = False):
        """启动/复用自动重连任务（防止断连风暴产生多个并发循环）"""
        if self._reconnect_task and not self._reconnect_task.done() and not force:
            return
        self._reconnect_task = asyncio.run_coroutine_threadsafe(
            self._auto_reconnect(force), self._loop)

    async def _auto_reconnect(self, force: bool = False):
        """断连后持续自动重连：直连 → 按名字前缀扫描交替，退避 2s→5s→10s"""
        delay = 2.0 if not force else 0.5
        while not self._ble.is_connected:
            if force:
                # 手动扫描：清掉旧的记住地址，只按名字前缀找
                if await self._scan_and_connect(retries=1):
                    return
                force = False
                delay = 2.0
            else:
                addr = self._ble._last_address or self._config.last_connected_address
                name = self._ble._device_name or self._config.last_connected_name
                if addr:
                    try:
                        await self._ble.connect(addr, name, timeout=8.0)
                    except Exception:
                        pass
                if not self._ble.is_connected:
                    if await self._scan_and_connect(retries=0):
                        return
            if self._ble.is_connected:
                return
            await asyncio.sleep(delay)
            delay = min(delay * 2, 10.0)

    async def _scan_and_connect(self, retries=3):
        """扫描并按 device_name_filter 前缀连接设备"""
        if self._ble.is_connected:
            return True
        for attempt in range(retries + 1):
            if self._ble.is_connected:
                return True
            if attempt > 0:
                await asyncio.sleep(2)
            devices = await self._ble.scan(5.0)
            for d in devices:
                name = d.get("name", "") or ""
                if name.startswith(self._config.device_name_filter):
                    logger.info("扫描到设备 %s (%s)，连接中…", name, d["address"])
                    try:
                        await self._ble.connect(d["address"], name, timeout=8.0)
                    except Exception as e:
                        logger.warning("连接失败: %s", e)
                    if self._ble.is_connected:
                        return True
                    break
        return False
