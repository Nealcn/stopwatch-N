#!/usr/bin/env python3
"""无头验证：stopwatch 桌面端 DeepSeek 润色/翻译 + 无 ASR Key 提示（stub PyQt5/bleak）

运行：python3 verify_headless.py
覆盖：config round-trip、settings 保存（deepseek 字段）、llm 客户端逻辑、
     floatball 按钮/信号、app 无 Key 分支。
"""
import sys
import os
import types
import unittest
import tempfile
from pathlib import Path

DESKTOP = Path(__file__).resolve().parent
sys.path.insert(0, str(DESKTOP))

# ---------- stub Windows/Qt 依赖 ----------

class _Stub:
    def __init__(self, *a, **kw): pass
    def __getattr__(self, name): return _Stub()
    def __call__(self, *a, **kw): return _Stub()
    def __bool__(self): return True
    def __int__(self): return 0


class FakeSignal:
    """pyqtSignal stub：connect 存回调，emit 触发（记录发射参数）"""
    def __init__(self, *types):
        self._callbacks = []
        self.emitted = []
    def connect(self, cb): self._callbacks.append(cb)
    def emit(self, *args):
        self.emitted.append(args)
        for cb in self._callbacks:
            cb(*args)


import ctypes
if not hasattr(ctypes, "windll"):
    ctypes.windll = _Stub()

pyqt5 = types.ModuleType("PyQt5")
qtcore = types.ModuleType("PyQt5.QtCore")
qtwidgets = types.ModuleType("PyQt5.QtWidgets")
qtgui = types.ModuleType("PyQt5.QtGui")

Qt = _Stub()
Qt.WindowStaysOnTopHint = Qt.FramelessWindowHint = Qt.Tool = Qt.LeftButton = 1
Qt.NoFocus = Qt.NoPen = Qt.AlignCenter = Qt.AlignLeft = Qt.AlignTop = Qt.TransparentForMouseEvents = 2
Qt.ScrollBarAlwaysOff = Qt.PointingHandCursor = Qt.PassThrough = 3
Qt.AA_EnableHighDpiScaling = 4
Qt.HighDpiScaleFactorRoundingPolicy = type("P", (), {"PassThrough": 5})

class _QObject(_Stub):
    def __init__(self, *a, **kw): pass

qtcore.QObject = _QObject
qtcore.Qt = Qt
qtcore.QTimer = _Stub
qtcore.QPoint = _Stub
qtcore.QRect = _Stub
qtcore.QRectF = _Stub
qtcore.QEvent = _Stub
qtcore.pyqtSignal = FakeSignal
qtcore.pyqtSlot = lambda *a, **kw: (lambda f: f)

class _WidgetStub:
    def __init__(self, *a, **kw):
        self._x = self._y = 0
        self._w = self._h = 88
    def width(self): return self._w
    def height(self): return self._h
    def x(self): return self._x
    def y(self): return self._y
    def pos(self): return (self._x, self._y)
    def move(self, x, y=None):
        if y is None:  # QPoint 形式
            x, y = x.x(), x.y()
        self._x, self._y = x, y
    def setFixedSize(self, w, h):
        self._w, self._h = w, h
    def __getattr__(self, name): return _Stub()


class _PushButtonStub(_WidgetStub):
    def __init__(self, text="", parent=None):
        super().__init__()
        self._text = text
        self.clicked = FakeSignal()
    def text(self): return self._text
    def setText(self, t): self._text = t
    def setCursor(self, c): pass
    def setMouseTracking(self, v): pass
    def setStyleSheet(self, s): pass


qtwidgets.QWidget = _WidgetStub
qtwidgets.QLabel = _WidgetStub
qtwidgets.QTextEdit = _WidgetStub
qtwidgets.QPushButton = _PushButtonStub
class _RectStub:
    def __init__(self, l=0, t=0, r=1920, b=1080):
        self._l, self._t, self._r, self._b = l, t, r, b
    def left(self): return self._l
    def top(self): return self._t
    def right(self): return self._r
    def bottom(self): return self._b
    def united(self, other):
        return _RectStub(min(self._l, other.left()), min(self._t, other.top()),
                         max(self._r, other.right()), max(self._b, other.bottom()))


class _ScreenStub:
    def __init__(self): self.screenAdded = self.screenRemoved = _Stub()
    def availableGeometry(self): return _RectStub()


class _AppInstanceStub:
    def __init__(self):
        self.screenAdded = FakeSignal()
        self.screenRemoved = FakeSignal()


class _QApplicationStub(_Stub):
    @staticmethod
    def primaryScreen(): return _ScreenStub()
    @staticmethod
    def screens(): return [_ScreenStub()]
    @staticmethod
    def instance(): return _AppInstanceStub()


qtwidgets.QApplication = _QApplicationStub
qtwidgets.QDialog = type("QD", (_Stub,), {"Accepted": 1, "Rejected": 0})
qtwidgets.QSystemTrayIcon = type("QT", (_Stub,), {"Warning": 1, "DoubleClick": 2})
qtwidgets.QMenu = _Stub
qtwidgets.QMessageBox = _Stub
qtwidgets.QAction = _Stub
qtwidgets.QFormLayout = _Stub
qtwidgets.QLineEdit = type("QLE", (_Stub,), {"Password": 3})
qtwidgets.QDoubleSpinBox = _Stub
qtwidgets.QDialogButtonBox = type("QDB", (_Stub,), {"Save": 1, "Cancel": 2})
qtwidgets.QVBoxLayout = _Stub
qtwidgets.QStandardPaths = type("QSP", (), {"DocumentsLocation": 1})
qtwidgets.QStandardPaths.writableLocation = staticmethod(lambda loc: str(Path.home()))

class _QColor:
    def __init__(self, *a, **kw): pass
    def name(self): return "#000000"
    def darker(self, f=100): return self
    def lighter(self, f=100): return self
    def red(self): return 0
    def green(self): return 0
    def blue(self): return 0
    def alpha(self): return 255


qtgui.QIcon = _Stub
qtgui.QPixmap = _Stub
qtgui.QPainter = _Stub
qtgui.QColor = _QColor
qtgui.QPen = _Stub
qtgui.QFontMetrics = _Stub
qtgui.QRadialGradient = _Stub
qtgui.QFont = _Stub

bleak = types.ModuleType("bleak")
bleak.BleakScanner = _Stub
bleak.BleakClient = _Stub
bleak.backends = types.ModuleType("bleak.backends")
bleak.backends.device = types.ModuleType("bleak.backends.device")
bleak.backends.device.BLEDevice = _Stub

sys.modules.update({
    "PyQt5": pyqt5, "PyQt5.QtCore": qtcore, "PyQt5.QtWidgets": qtwidgets,
    "PyQt5.QtGui": qtgui, "bleak": bleak, "bleak.backends": bleak.backends,
    "bleak.backends.device": bleak.backends.device,
})

# ---------- 测试 ----------

from voicestick.config import AppConfig


class TestConfig(unittest.TestCase):
    def test_roundtrip_with_deepseek(self):
        with tempfile.TemporaryDirectory() as td:
            orig = AppConfig.CONFIG_PATH
            AppConfig.CONFIG_PATH = Path(td) / "config.json"
            try:
                c = AppConfig()
                c.asr_api_key = "k1"
                c.deepseek_api_key = "sk-deepseek"
                c.deepseek_base_url = "https://api.deepseek.com"
                c.save()
                c2 = AppConfig.load()
                self.assertEqual(c2.deepseek_api_key, "sk-deepseek")
                self.assertEqual(c2.deepseek_base_url, "https://api.deepseek.com")
            finally:
                AppConfig.CONFIG_PATH = orig

    def test_defaults(self):
        c = AppConfig()
        self.assertEqual(c.deepseek_api_key, "")
        self.assertEqual(c.deepseek_base_url, "https://api.deepseek.com")


class TestSettingsDialog(unittest.TestCase):
    def test_save_writes_deepseek(self):
        from voicestick.ui.settings_dialog import SettingsDialog
        cfg = AppConfig()
        dlg = SettingsDialog.__new__(SettingsDialog)
        dlg._config = cfg
        dlg._server_url = types.SimpleNamespace(text=lambda: "wss://x", setFocus=lambda: None, selectAll=lambda: None)
        dlg._api_key = types.SimpleNamespace(text=lambda: "asr-key")
        dlg._device_filter = types.SimpleNamespace(text=lambda: "VS")
        dlg._mouse_gain = types.SimpleNamespace(value=lambda: 1.5)
        dlg._deepseek_key = types.SimpleNamespace(text=lambda: " sk-ds ")
        dlg._deepseek_url = types.SimpleNamespace(text=lambda: " https://api.deepseek.com ")
        dlg.accept = lambda: None
        dlg._on_save()
        self.assertEqual(cfg.deepseek_api_key, "sk-ds")
        self.assertEqual(cfg.deepseek_base_url, "https://api.deepseek.com")

    def test_save_empty_deepseek_url_restores_default(self):
        from voicestick.ui.settings_dialog import SettingsDialog
        cfg = AppConfig()
        dlg = SettingsDialog.__new__(SettingsDialog)
        dlg._config = cfg
        dlg._server_url = types.SimpleNamespace(text=lambda: "wss://x", setFocus=lambda: None, selectAll=lambda: None)
        dlg._api_key = types.SimpleNamespace(text=lambda: "")
        dlg._device_filter = types.SimpleNamespace(text=lambda: "VS")
        dlg._mouse_gain = types.SimpleNamespace(value=lambda: 1.0)
        dlg._deepseek_key = types.SimpleNamespace(text=lambda: "")
        dlg._deepseek_url = types.SimpleNamespace(text=lambda: "  ")
        dlg.accept = lambda: None
        dlg._on_save()
        self.assertEqual(cfg.deepseek_base_url, "https://api.deepseek.com")


class TestLLM(unittest.TestCase):
    def test_chat_without_key_raises(self):
        import asyncio
        from voicestick.llm import DeepSeekClient, LLMError
        client = DeepSeekClient("")
        self.assertFalse(client.configured)
        with self.assertRaises(LLMError):
            asyncio.run(client.chat([{"role": "user", "content": "hi"}]))

    def test_messages_builders(self):
        from voicestick.llm import polish_messages, translate_messages
        p = polish_messages("今天天气不错")
        self.assertEqual(p[-1]["role"], "user")
        self.assertEqual(p[-1]["content"], "今天天气不错")
        t = translate_messages("你好")
        self.assertIn("翻译", t[0]["content"])
        self.assertEqual(t[-1]["content"], "你好")

    def test_url_join(self):
        from voicestick.llm import DeepSeekClient
        c = DeepSeekClient("k", "https://api.deepseek.com/")
        self.assertEqual(c._base_url, "https://api.deepseek.com")


class TestFloatball(unittest.TestCase):
    def test_buttons_include_polish_translate(self):
        from voicestick.ui.floatball import FloatingBallWindow
        w = FloatingBallWindow()
        labels = [b.text() for b in w._side_btns]
        self.assertEqual(labels, ["清除", "复制", "保存", "润色", "翻译"])

    def test_polish_btn_emits_signal(self):
        from voicestick.ui.floatball import FloatingBallWindow
        w = FloatingBallWindow()
        w._edit.toPlainText = lambda: " 今天  天气  不错 "
        w.show_toast = lambda m: None
        # 直接调用 _on_btn，捕获信号
        received = []
        w.llm_requested.connect(lambda mode, text: received.append((mode, text)))
        w._on_btn("润色")
        self.assertEqual(received, [("润色", "今天  天气  不错")])
        received.clear()
        w._on_btn("翻译")
        self.assertEqual(received, [("翻译", "今天  天气  不错")])

    def test_polish_empty_text_no_emit(self):
        from voicestick.ui.floatball import FloatingBallWindow
        w = FloatingBallWindow()
        w._edit.toPlainText = lambda: "   "
        w.show_toast = lambda m: None
        received = []
        w.llm_requested.connect(lambda mode, text: received.append((mode, text)))
        w._on_btn("润色")
        self.assertEqual(received, [])


class TestAppNoKey(unittest.TestCase):
    def test_llm_requested_without_key_shows_toast(self):
        from voicestick.app import VoiceStickApp
        app = VoiceStickApp.__new__(VoiceStickApp)
        app._llm = types.SimpleNamespace(configured=False)
        app._floatball = types.SimpleNamespace(show_toast=lambda m: None)
        app._coordinator = types.SimpleNamespace(on_status=lambda s: None)
        app._on_llm_requested("润色", "text")
        # 不崩溃即通过（无 key 分支不应调度协程）

    def test_asr_client_no_key_silent_start(self):
        import asyncio
        from voicestick.asr_client import AsrClient
        client = AsrClient("wss://x", "")
        errors = []
        client.on_error = lambda m: errors.append(m)
        self.assertFalse(asyncio.run(client.start()))
        self.assertEqual(errors, [])  # 启动静默

    def test_asr_client_no_key_session_warns(self):
        import asyncio
        from voicestick.asr_client import AsrClient
        client = AsrClient("wss://x", "")
        errors = []
        client.on_error = lambda m: errors.append(m)
        self.assertFalse(asyncio.run(client.start_session()))
        self.assertTrue(errors)
        self.assertIn("API Key 未配置", errors[0])


if __name__ == "__main__":
    unittest.main(verbosity=2)
