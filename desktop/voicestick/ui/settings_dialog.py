"""设置对话框 — ASR 服务器/Key、设备过滤、鼠标增益

托盘菜单「设置…」打开；保存后由 app.py 热生效（ASR 重连、鼠标增益即时，
设备过滤下次重连生效）。
"""
from PyQt5.QtWidgets import (
    QDialog, QFormLayout, QLineEdit, QDoubleSpinBox,
    QDialogButtonBox, QLabel, QVBoxLayout,
)
from PyQt5.QtCore import Qt

from ..config import AppConfig


class SettingsDialog(QDialog):
    def __init__(self, config: AppConfig, parent=None):
        super().__init__(parent)
        self._config = config
        self.setWindowTitle("设置 — 语音输入")
        self.setMinimumWidth(420)

        # ---- 表单 ----
        self._server_url = QLineEdit(config.asr_server_url)
        self._server_url.setPlaceholderText("wss://…（火山引擎 OpenSpeech 大模型流式）")

        self._api_key = QLineEdit(config.asr_api_key)
        self._api_key.setEchoMode(QLineEdit.Password)
        self._api_key.setPlaceholderText("留空则语音识别不可用（鼠标功能不受影响）")

        self._device_filter = QLineEdit(config.device_name_filter)
        self._device_filter.setPlaceholderText("自动连接名字以此前缀开头的设备")

        self._mouse_gain = QDoubleSpinBox()
        self._mouse_gain.setRange(0.1, 10.0)
        self._mouse_gain.setSingleStep(0.1)
        self._mouse_gain.setDecimals(1)
        self._mouse_gain.setValue(config.mouse_gain)

        form = QFormLayout()
        form.addRow("ASR 服务器地址", self._server_url)
        form.addRow("ASR API Key", self._api_key)
        form.addRow("设备名前缀", self._device_filter)
        form.addRow("鼠标增益", self._mouse_gain)

        hint = QLabel(
            "保存后：ASR 与鼠标增益立即生效；设备名前缀在下次自动重连时生效。\n"
            "配置文件：%s" % AppConfig.CONFIG_PATH
        )
        hint.setWordWrap(True)
        hint.setStyleSheet("color:#888;font-size:12px;")
        hint.setAlignment(Qt.AlignLeft | Qt.AlignTop)

        buttons = QDialogButtonBox(
            QDialogButtonBox.Save | QDialogButtonBox.Cancel
        )
        buttons.accepted.connect(self._on_save)
        buttons.rejected.connect(self.reject)

        layout = QVBoxLayout(self)
        layout.addLayout(form)
        layout.addWidget(hint)
        layout.addWidget(buttons)

    def _on_save(self):
        """校验并写入配置（保存成功后对话框关闭）"""
        url = self._server_url.text().strip()
        if url and not (url.startswith("wss://") or url.startswith("ws://")):
            self._server_url.setFocus()
            self._server_url.selectAll()
            return  # 非法 URL 不关闭，等用户修正

        self._config.asr_server_url = url or "wss://openspeech.bytedance.com/api/v3/sauc/bigmodel_async"
        self._config.asr_api_key = self._api_key.text().strip()
        self._config.device_name_filter = self._device_filter.text().strip() or "VS"
        self._config.mouse_gain = round(self._mouse_gain.value(), 1)
        self._config.save()
        self.accept()
