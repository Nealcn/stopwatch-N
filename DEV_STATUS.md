# M5StopWatch 项目状态（2026-08-17）

## 项目概况

- 平台：M5StopWatch 手表（ESP32-S3，16MB Flash QIO，8MB Octal PSRAM）
- 框架：ESP-IDF v5.5.4 + mooncake v2.3.3 + LVGL v9.5.0 + M5GFX 0.2.19
- 分支：dev

## 当前应用清单（8 个）

| 应用 | 状态 | 说明 |
|---|---|---|
| Launcher | ✅ | 横向滑动循环菜单 |
| 徽章 Badge | ✅ | AP 配网 + 手机传图（依赖 WiFi，保留） |
| IMU 立方体 | ✅ | 姿态显示 |
| 频谱 FFT | ✅ | 麦克风环形频谱 |
| 转盘 LuckyWheel | ✅ | 触屏转盘 |
| 设置 Settings | ✅ | 背光/音量 |
| 番茄倒计时 | ✅ | 专注/休息循环，新图标 |
| 语音输入（原 VoiceCube） | ✅ | BLE 语音识别 + 触摸板鼠标，新图标 + 重设计 UI |

已移除：骰子、闹钟、表盘、秒表（源码保留，注释 [main.cpp](main/main.cpp) 注册行可恢复）

## 本次改动总结（2026-08-17）

### 固件端
1. **中文字体修复**：界面原显示方块字。新增思源黑体子集字体（[lv_font_cn_24.c](main/assets/fonts/lv_font_cn_24.c) / 26，211 字符，由 [gen_cn_fonts.py](gen_cn_fonts.py) 生成），挂到所有字体 fallback（[hal_display.cpp](main/hal/hal_display.cpp)）。**注意**：被挂 fallback 的字体对象必须放 `.dram0.data`（RAM）段，写 flash 只读区会 Cache error panic
2. **新图标**：番茄钟（深红番茄+绿叶）、语音输入（深青蓝麦克风），由 [gen_icons.py](gen_icons.py) 生成 200×200 RGB565（黑底深色调，与现有图标风格一致）
3. **语音输入应用重设计**：
   - 改名"语音输入"（原 VoiceCube）
   - **黑屏修复**：onOpen 持锁期间嵌套加锁（非递归互斥量）→ 死锁。锁规则：`_update_labels` 不加锁，调用方保证持锁；onClick（LVGL 线程）绝不加锁；BLE 回调（NimBLE 线程）必须加锁
   - 新 UI：状态文字 + 麦克风图形（状态色驱动：灰=空闲/红=录音/琥珀=识别/蓝=已粘贴）+ 识别预览大字 + 确认/取消按钮 + 两行录音提示
4. **录音稳定性修复**（参考原 VoiceCube 工程经验）：
   - NimBLE host 栈 4KB→8KB（订阅回调栈溢出崩溃）
   - 录音任务栈 24KB 放 **PSRAM**（内部 RAM 栈不足 + 内部 RAM 需留给编码器）
   - **Opus 编码器强制内部 RAM + 启动早期预分配**（[opus_encoder.c](main/apps/app_voicecube/opus_encoder.c)）：PSRAM 上的编码器密集读写会 cache 卡死 → WDT 复位（reason=7）；内部 RAM 运行时碎片化（free 53KB 但最大连续块 20KB），预分配在 heap 干净时执行
   - WDT/Brownout 配置对齐原版：[sdkconfig.defaults](sdkconfig.defaults) —— INT_WDT 关、Task WDT 30s、Brownout 电平 5、NimBLE 内存 PSRAM、WiFi 缓冲 PSRAM、DMA 保留区 16KB
5. 删除应用：闹钟/表盘/秒表/骰子（注册注释）

### 桌面端（desktop/）
- **新增 GUI**（参考原 VoiceCube，PyQt5）：
  - 系统托盘（麦克风图标，状态/重新扫描/关于/退出）
  - 悬浮球（状态色 + 识别文本预览 + 清除/复制/保存按钮，可拖拽记忆位置）
  - 自动连接/断线重连（按 `VS` 前缀扫描，退避 2s→5s→10s）
  - 文件：`voicestick/app.py`（新增）、`voicestick/ui/floatball.py`（移植裁剪）、`voicestick/coordinator.py`（增加 UI 回调接口）
- 修复：设备名过滤 `VS-`→`VS`（固件广播名 `VSA4AA` 无连字符）；`_find_device` 优先名字匹配（避免连错旧设备）
- 依赖：`pip install -r desktop/requirements.txt`（bleak/aiohttp/PyQt5）
- 运行：`python main.py`（GUI）/ `python main.py --cli`（无界面）

## 构建与烧录

```bash
# 环境（Windows）：ESP-IDF v5.5.4（D:\esp-idf）+ 工具链（D:\Espressif）
# 在 cmd 中（Git Bash 需清 MSYSTEM 变量）：
set MSYSTEM=
set IDF_TOOLS_PATH=D:\Espressif
set IDF_PYTHON_ENV_PATH=D:\Espressif\python_env\idf5.5_py3.10_env
call D:\esp-idf\export.bat
python D:\esp-idf\tools\idf.py build
python D:\esp-idf\tools\idf.py -p COM8 flash
python D:\esp-idf\tools\idf.py -p COM8 monitor
```

注意：`export.bat` 后不能加 `>nul` 重定向（MSYS 转换坑）；`sdkconfig.defaults` 变更后需删除 `sdkconfig` 重新生成。

## 已知问题 / 待办

- [ ] 录音完整链路待最终验证（编码器预分配 + PSRAM 栈固件已烧录，待实测录音+识别+粘贴）
- [ ] 桌面端"设置"界面未做（ASR Key 直接编辑 `~/.voicestick/config.json`）
- [ ] Launcher 菜单为横向滑动（"环形"指循环滚动）；如需圆形排列需重做布局
- [ ] 悬浮球"润色/翻译"按钮未实现（无 LLM 功能）；"保存"写入桌面端运行目录 notes.md

## 内存情况

- 内部 RAM（~300KB 可用）主要占用：WiFi 驱动 ~40KB、LVGL 管理、文件系统、Opus 编码器 43KB（预分配）
- 已优化：NimBLE 内存→PSRAM、WiFi 缓冲→PSRAM、DMA 保留区 32→16KB、main 栈 8→4KB、任务栈→PSRAM
- 固件大小 ~4.33MB / app 分区 4.94MB（剩余 16%）
