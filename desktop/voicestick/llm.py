"""DeepSeek LLM 客户端 — 悬浮球 润色/翻译 按钮（OpenAI 兼容 Chat Completions API）"""
import asyncio
import logging
from typing import Optional, List, Dict

import aiohttp

logger = logging.getLogger(__name__)

DEFAULT_MODEL = "deepseek-chat"
TIMEOUT = 60.0


class LLMError(Exception):
    """LLM 调用失败（网络/鉴权/服务器错误）"""


class DeepSeekClient:
    """最小 OpenAI 兼容客户端：POST {base_url}/chat/completions，返回首条消息文本"""

    def __init__(self, api_key: str, base_url: str = "https://api.deepseek.com"):
        self._api_key = api_key
        self._base_url = base_url.rstrip("/")

    @property
    def configured(self) -> bool:
        return bool(self._api_key)

    async def chat(
        self,
        messages: List[Dict[str, str]],
        temperature: float = 0.7,
        max_tokens: int = 1024,
    ) -> str:
        """发送对话并返回助手回复文本。出错抛 LLMError。"""
        if not self._api_key:
            raise LLMError("未配置 DeepSeek API Key（托盘右键 设置… 填写）")
        url = f"{self._base_url}/chat/completions"
        payload = {
            "model": DEFAULT_MODEL,
            "messages": messages,
            "temperature": temperature,
            "max_tokens": max_tokens,
            "stream": False,
        }
        headers = {
            "Authorization": f"Bearer {self._api_key}",
            "Content-Type": "application/json",
        }
        try:
            async with aiohttp.ClientSession() as session:
                async with session.post(url, json=payload, headers=headers,
                                        timeout=aiohttp.ClientTimeout(total=TIMEOUT)) as resp:
                    if resp.status == 401:
                        raise LLMError("DeepSeek API Key 无效（401），请在 设置… 中检查")
                    if resp.status == 402:
                        raise LLMError("DeepSeek 账户余额不足（402）")
                    if resp.status != 200:
                        body = (await resp.text())[:200]
                        raise LLMError(f"DeepSeek 服务器错误（{resp.status}）: {body}")
                    data = await resp.json()
        except asyncio.TimeoutError:
            raise LLMError("DeepSeek 请求超时，请稍后重试")
        except aiohttp.ClientConnectorError as e:
            raise LLMError(f"无法连接 DeepSeek 服务器: {e}")
        try:
            text = data["choices"][0]["message"]["content"].strip()
        except (KeyError, IndexError, TypeError):
            raise LLMError("DeepSeek 响应格式异常")
        return text


# 提示词模板
def polish_messages(text: str) -> List[Dict[str, str]]:
    return [
        {"role": "system",
         "content": "你是文字润色助手。把用户给出的语音转写文本润色得更通顺、简洁、符合书面表达，"
                    "保留原意，不增删事实内容。只输出润色后的文本，不要任何解释、前缀或引号。"},
        {"role": "user", "content": text},
    ]


def translate_messages(text: str, target: str = "英文") -> List[Dict[str, str]]:
    return [
        {"role": "system",
         "content": f"你是翻译助手。把用户输入翻译成{target}，只输出译文，不要任何解释、前缀或引号。"},
        {"role": "user", "content": text},
    ]
