# M5 StopWatch 融合固件 — Windows 构建指南

> 编译环境与源码开发环境分离：本目录为**源码与文档工作区**（不含 ESP-IDF），编译在另一台 Windows 电脑上进行。
> 本指南面向编译机。官方工程基线：`m5stack/M5StopWatch-UserDemo`（main 分支）。

## 1. 前置条件

- Windows 10/11，硬盘剩余空间 ≥ 15GB（ESP-IDF 工具链约 8GB）
- 建议内存 16GB（8GB 可编译但较慢）
- 网络可达 GitHub（拉取组件与工具链）

## 2. 安装 ESP-IDF v5.5.4（编译机必装）

ESP-IDF 版本必须为 **v5.5.4**（与官方工程锁定版本一致），二选一：

### 方式 A（推荐）：VS Code 扩展

1. VS Code 安装扩展 **Espressif IDF**
2. `Ctrl+Shift+P` → 运行 **`ESP-IDF: Configure ESP-IDF Extension`**
3. 选择 **v5.5.4**（列表 5.5.x），其余默认（扩展自带 Python 虚拟环境与工具链）

### 方式 B：官方 Windows 安装器

1. 从 <https://dl.espressif.com/dl/esp-idf/> 下载 **esp-idf-tools-setup**（建议 offline 版，约 1GB，安装时无需再下）
2. 安装时选择 ESP-IDF **v5.5.4**，勾选 "Add to PATH" 项保持默认即可
3. 安装完成后桌面上会生成 **"ESP-IDF 5.5 PowerShell"** 快捷方式（编译用这个终端）

## 3. 获取工程源码（二选一）

### 方式 A：使用打包好的完整源码包（推荐，离线可用）

从工作机拷贝 **`M5StopWatch-UserDemo-完整源码.zip`**，解压到编译机（如 `D:\projects\M5StopWatch-UserDemo`）。
包内已含 `components/` 全部 9 个依赖组件，**无需联网拉依赖**。

### 方式 B：联网拉取（组件需要更新时）

```bash
git clone https://github.com/m5stack/M5StopWatch-UserDemo.git
cd M5StopWatch-UserDemo
python3 ./fetch_repos.py        # 从 repos.json 拉取 9 个组件到 components/
```

> `fetch_repos.py` 幂等，网络中断重跑即可。组件版本由 `repos.json` 锁定（mooncake v2.3.3、LVGL v9.5.0、M5GFX 0.2.19 等），**不要手动更新组件到其他版本**。

## 4. 编译

```bash
# 方式 A 安装后：在 VS Code 中按 F1 → "ESP-IDF: Build your project"
# 方式 B 安装后：打开 "ESP-IDF 5.5 PowerShell"，cd 到工程目录
idf.py build
```

首次编译约 10–30 分钟（全量），产物在 `build/M5StopWatch-UserDemo.bin`。增量编译通常 < 2 分钟。

## 5. 烧录与串口监控

```bash
# 设备用 Type-C 线连接编译机，确认串口号（设备管理器→端口 COMx）
idf.py -p COMx flash          # 烧录固件（含分区表与 storage 分区格式化）
idf.py -p COMx monitor        # 打开串口日志（Ctrl+] 退出）
```

- 若设备管理器看不到 COM 口：安装 CP210x 串口驱动（<https://www.silabs.com/developer-tools/usb-to-uart-bridge-vcp-drivers>），或更换数据线（部分线只有充电没有数据）
- 烧录完成后设备自动重启进入环形主菜单

## 6. 烧录后验证清单（阶段一 W1 摸底）

| 应用 | 验证项 |
|---|---|
| 表盘 | 显示正常，时间正确（RTC） |
| 秒表 | 开始/暂停/LAP 分段正常 |
| 闹钟 | 添加多组闹钟，到点震动+响铃 |
| 徽章 | 进入 AP 配网模式，手机浏览器上传图片成功 |
| 频谱 | 麦克风收音，环形频谱跳动 |
| IMU | 立方体随设备转动同步 |
| 转盘 | 触屏点击转动，指针停止正常 |
| 设置 | 背光/音量调节生效，重启后保存 |

## 7. 常见问题

| 现象 | 处理 |
|---|---|
| `fetch_repos.py` 报 `Failed to connect to github.com` | 网络瞬断，重跑即可（幂等） |
| 编译报组件版本冲突 | 确认未手动改动 `repos.json` 与 `components/` 下组件 |
| 烧录时报 `Could not open COMx` | 检查驱动与串口号；拔插 USB 重试 |
| 编译内存不足 | 关闭其他程序；Windows 虚拟内存调整到 16GB |
| 需要完全重来 | 删除 `build/` 目录后重新 `idf.py build` |
