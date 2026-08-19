"""StopWatch 桌面端配置管理（JSON）"""
import json
from pathlib import Path
from dataclasses import dataclass, field, asdict


@dataclass
class AppConfig:
    # ASR（VoiceCube 云 ASR / 火山引擎 OpenSpeech，需用户填写 api_key）
    asr_server_url: str = "wss://openspeech.bytedance.com/api/v3/sauc/bigmodel_async"
    asr_api_key: str = ""
    # 设备过滤（自动连接名字以该前缀开头的设备）
    # 注意：固件广播名为 "VS%02X%02X"（无连字符，见 ble_voice.cpp），此处不含 "-"
    device_name_filter: str = "VS"
    # 记住上次连接地址
    last_connected_address: str = ""
    last_connected_name: str = ""
    # 悬浮球位置（GUI 模式）
    floatball_x: int = -1
    floatball_y: int = -1
    # 鼠标增益补偿（设备端已设 3.0 默认；不同机器 Windows 鼠标加速设置不同，
    # 可在 ~/.voicestick/config.json 调此值微调，默认 1.0）
    mouse_gain: float = 1.0
    # DeepSeek LLM（悬浮球 润色/翻译 按钮，OpenAI 兼容 API）
    deepseek_api_key: str = ""
    deepseek_base_url: str = "https://api.deepseek.com"

    CONFIG_PATH = Path.home() / ".voicestick" / "config.json"

    @classmethod
    def load(cls) -> "AppConfig":
        path = cls.CONFIG_PATH
        if not path.exists():
            return cls()
        try:
            data = json.loads(path.read_text(encoding="utf-8"))
            return cls(**{k: v for k, v in data.items() if k in cls.__dataclass_fields__})
        except Exception:
            return cls()

    def save(self):
        path = self.CONFIG_PATH
        path.parent.mkdir(parents=True, exist_ok=True)
        data = {k: v for k, v in asdict(self).items() if k != "CONFIG_PATH"}
        path.write_text(json.dumps(data, ensure_ascii=False, indent=2), encoding="utf-8")
