# 编译踩坑记录（2026-08-07，首次在云服务器上编译）

> 环境：腾讯云 VM（2核/1.9G 内存 + 5.9G swap），Linux，ESP-IDF v5.5.4（Docker 镜像 espressif/idf:v5.5.4），网络位于国内。
> 结论：**编译已通过**，产物 `build/StopWatch-UserDemo.bin`。

## 一、网络类（国内拉取 GitHub 依赖）

| 坑 | 现象 | 解法 |
|---|---|---|
| GitHub 间歇性封锁 | git clone/fetch 随机超时、连不上 443 | 多路切换：直连 → `gitclone.com/github.com/` 镜像（`git config --global url."https://gitclone.com/github.com/".insteadOf "https://github.com/"`）→ Gitee 官方镜像 → `ghfast.top` 等 tarball 代理 |
| gitclone 对 tag 支持差 | `-b 1.0.8` 报 "Remote branch not found"（1.0.8 是 tag 不是 branch） | 用代理下精确 tag：`curl -L https://ghfast.top/https://github.com/<owner>/<repo>/archive/refs/tags/<tag>.tar.gz` |
| gitclone 大仓库 502 | lvgl、esp-idf 等大仓库报 502 | 换源：lvgl 走 `gitee.com/mirrors/lvgl`；esp-idf 走 `gitee.com/EspressifSystems/esp-idf` |
| esp-idf 子模块国内拉不动 | 直连/镜像都慢或失败 | Gitee 主仓库 + 乐鑫官方 `esp-gitee-tools/submodule-update.sh` 拉子模块（会改写 .gitmodules 到 gitee） |
| `components/esp_wifi/lib` 被 Gitee 封禁（423） | 官方 gitee 镜像仓库被封（预编译二进制） | 从官方 Docker 镜像提取：`docker pull espressif/idf:v5.5.4` → `docker cp <容器>:/opt/esp/idf/components/esp_wifi/lib <本地>` |
| 组件注册表 403/不存在 | `espressif/opus` 在注册表不存在（403 误导） | 本地引入 xiph/opus v1.5.2 源码 + 自写 IDF 组件包装（见 components/opus/） |
| Docker Hub 镜像源不稳 | docker.1ms.run 拉大镜像中途断、配置层缺失 | 多镜像轮询：`dockerproxy.net` 最终成功 |
| `fetch_repos.py` 顺序缺陷 | lvgl 排在前面失败时，后面组件永远轮不到 | 手动逐个处理剩余组件 |

## 二、编译类

| 坑 | 现象 | 解法 |
|---|---|---|
| `-Werror=all` 漏进主工程 | app 代码警告变错误（LVGL 库的编译策略外溢） | `main/CMakeLists.txt` 加 `target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-error)` |
| `snprintf` 格式警告 | `%u` vs `long unsigned int` | `%02u` → `%02lu`（app_pomodoro.cpp） |
| smooth_ui_toolkit API | `setAlign(align, x, y)` 3 参不存在 | 用 `align(align, x, y)`（pomodoro/voicecube） |
| voicecube 缺常量 | `BOARD_AUDIO_*` 未定义（从 xiaozhi 移植，原在板级头文件） | 补定义：16k / 单声道 / 60ms / 960 samples（编码器输入是 16k 重采样后数据） |
| `idf.py build` 无 `-j` 参数 | `No such option: -j`（IDF 5.x 移除） | `idf.py reconfigure` + `ninja -C build -j1`（限内存时单线程） |
| Docker 镜像入口 | `exec: build: not found` | 镜像 entrypoint 是 shell，要显式 `idf.py build` |
| ninja "manifest still dirty after 100 tries" | 增量构建死循环 | 根因：**VM 系统时钟快了 10 天**（时间戳全在未来）。解法：清理 build 目录全新编译 |
| FreeRTOS API 改名 | `xSemaphoreGetCount` 未声明（IDF 5.5 的 FreeRTOS 移除旧名） | 改 `uxSemaphoreGetCount`（audio_mutex.cpp） |
| NimBLE 设计化初始化顺序 | `designator order for field 'arg'`（C++20 强校验） | `.arg` 挪到 `.flags` 前面（ble_voice.cpp ×3） |
| NimBLE 位域绑定引用 | 位域不能绑 `unsigned char&` | 先存局部变量再传参（ble_voice.cpp SUBSCRIBE 事件） |
| 嵌套枚举未限定 | `Type` was not declared | `TouchPadEvent::Type`（touch_pad.cpp） |
| lwip 宏未声明 | `IP4_ADDR` not declared | 补 `#include <lwip/ip4_addr.h>`（wifi_manager.cpp） |
| 头文件路径缺根 | `<utils/settings/settings.h>` not found | main/CMakeLists.txt 的 INCLUDE_DIRS 加 `hal` |
| **tarball 提取的组件无 .git，`git apply` 会静默失效**（打印 OK 但文件没变） | M5IOE1/M5PM1 的 #error 仍在 | 手工用 patch 工具应用补丁内容；或克隆后验证 `grep -c "#error"` |
| M5IOE1/M5PM1 与 i2c_bus+M5GFX 冲突 #error | 组件头文件硬编码 #error | 仓库自带 patches/M5IOE1.patch、M5PM1.patch 已处理（删 #error + 强制 i2c_bus 模式），确保补丁真正生效 |

## 三、环境类

- **内存不足**：官方要求 8G 起步，VM 只有 1.9G → 加 4G swap（共 5.9G）+ 单线程 `-j1` 编译，可跑但慢（首次全量约 40~60 分钟）。
- **系统时钟**：VM 时钟比真实时间快 10 天（2026-08-17 vs 实际 08-07），影响 ninja 增量判断。已通过清理 build 规避；建议服务器管理员校准 NTP。
- 组件拉取优先顺序建议：**ghfast.top tarball（精确 tag）> gitee 镜像 > gitclone > GitHub 直连**。

## 四、本次代码改动清单（已提交 dev 分支）

1. `main/apps/app_pomodoro/app_pomodoro.cpp`：`%02u`→`%02lu`；`setAlign`→`align`
2. `main/apps/app_voicecube/app_voicecube.cpp`：补 `BOARD_AUDIO_*` 常量；`setAlign`→`align` ×2
3. `main/CMakeLists.txt`：主工程加 `-Wno-error`；INCLUDE_DIRS 加 `hal`（均有注释）
4. `components/opus/`：新增，xiph/opus v1.5.2 本地组件（IDF 包装，float 实现）
5. `main/framework/audio_mutex/audio_mutex.cpp`：`xSemaphoreGetCount`→`uxSemaphoreGetCount` ×2
6. `main/framework/ble_voice/ble_voice.cpp`：`.arg`/`.flags` 初始化顺序 ×3；位域传参修复
7. `main/framework/touch_pad/touch_pad.cpp`：`Type`→`TouchPadEvent::Type`
8. `main/framework/wifi_manager/wifi_manager.cpp`：补 `#include <lwip/ip4_addr.h>`
9. `PITFALLS.md`：新增，本次踩坑全记录
