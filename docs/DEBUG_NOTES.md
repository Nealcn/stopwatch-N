# M5StopWatch 调试踩坑记录

> 2026-08 固件/桌面端调试全过程经验。每个坑都包含：现象 → 根因 → 修复/规避。

## 一、编译环境（Windows）

| 坑 | 现象 | 修复 |
|---|---|---|
| Git Bash 的 `MSYSTEM` 变量 | `idf.py` 打印 "MSys/Mingw is no longer supported" 后**直接退出**（不执行任何命令，exit 0） | 用 `cmd //c`（内部 `set MSYSTEM=`）调用，或用 PowerShell |
| `call export.bat >nul` 重定向 | "系统找不到指定的路径"（MSYS 路径转换把 `/d` 等参数改写） | `export.bat` 后**不能加任何重定向** |
| bash `cd` 跨命令持久化 | idf.py 报 "CMakeLists.txt not found in project directory .../fonts"（cwd 漂移） | 每次构建前确认 cwd 在项目根 |
| 新文件（.c/.cpp）不被编译 | 链接报 undefined reference（CMake `GLOB_RECURSE` 配置时缓存） | `idf.py reconfigure` 后再 build |
| `sdkconfig.defaults` 修改不生效 | 配置没变（已有 sdkconfig 不重新合并） | 删 `sdkconfig` 重新生成 |
| ninja 锁残留 | `WriteFile(.ninja_lock): Permission denied`（kill 进程后） | 删 `build/.ninja_lock` 或等释放 |
| monitor 遇 emoji 崩溃 | `UnicodeEncodeError: 'gbk' codec`（设备输出 emoji 表情） | `PYTHONUTF8=1` 前缀运行 monitor |
| 串口被占用 | "Could not open COM8, the port is busy"（残留 python 进程） | `taskkill /F /IM python.exe` 后重试 |

## 二、ESP32-S3 内存与栈（核心知识）

### 1. Opus 编码器必须在内部 RAM
- **现象**：录音编码时 WDT 复位（`[RESET] reason=7`），无 panic 输出
- **根因**：编码器放 PSRAM，密集读写触发 cache 等待卡死 → 硬件看门狗
- **修复**：`MALLOC_CAP_INTERNAL` 强制 + **启动早期预分配**（`audio_encoder_prealloc`，43KB）
- **注意**：运行时内部 RAM 碎片严重（free 50KB 但最大连续块仅 20KB），运行时分配 43KB 必失败

### 2. NVS/flash 操作的任务不能用 PSRAM 栈
- **现象**：`assert failed: spi_flash_disable_interrupts_caches_and_other_cpu (esp_task_stack_is_sane_cache_disabled())`
- **根因**：flash 操作期间禁用 cache，PSRAM 栈（cache 映射）无法访问
- **修复**：WiFi 连接/配网任务必须**内部 RAM 栈**（8KB+）

### 3. 软 double 浮点栈需求巨大
- **现象**：`stack overflow in task chat_rec` + backtrace 含 `__muldf3`
- **根因**：xtensa 无 double FPU，libgcc 软浮点库栈需求大；重采样函数用了 `double`
- **修复**：重采样改 `float`（ESP32-S3 有硬件 FPU，栈需求极小）

### 4. 任务栈大小参考
| 任务 | 栈 | 内存 | 原因 |
|---|---|---|---|
| 录音（audioRecord + Opus 编码叠加） | 48KB | PSRAM | I2S 读 + CELT 编码同任务（参考 Stackchan 分离设计 24KB+6KB） |
| WiFi 连接/配网 | 8KB | 内部 RAM | esp_wifi 调用链深 + NVS 写 |
| AI 对话网络任务 | 24KB | 内部 RAM | TLS 握手 + lwIP DNS（TLS 读 flash 证书不能 PSRAM 栈） |
| WiFi 配网 AP | 8KB | PSRAM | AP 启动异步化（LVGL 持锁上下文不能阻塞） |

### 5. DMA 保留区（CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL）
- **16KB 太小** → WiFi 启动后 M5GFX `getDMABuffer` 分配失败（`heap_alloc_dma` NULL）→ 显示 `memcpy(NULL)` 崩溃
- 32KB 起步，参考 Stackchan 用 96KB（我们内部 RAM 紧张取 48KB 折中）

### 6. WiFi 缓冲配置（对齐 Stackchan-Newstep）
- **RX 减半** → RX 处理（`ppTask`）PC 跳飞（InstrFetchProhibited）
- **TX 减** → `is_wapi_alloc_tx_buf` 崩溃（LoadProhibited 读 0xfffff48e）
- **SPIRAM_TRY_ALLOCATE_WIFI_LWIP=y** → 音频 UDP 发送 pbuf 在 PSRAM，WiFi TX DMA 访问异常
- **稳定组合**：IRAM_OPT=n、RX_IRAM_OPT=n、DYNAMIC_RX_MGMT_BUFFER=y、STATIC_RX 6/DYNAMIC_RX 8、RX_BA_WIN 3、MBEDTLS_EXTERNAL_MEM_ALLOC=y

### 7. I2C 并发崩溃
- **现象**：录音时 `s_i2c_synchronous_transaction` 崩溃（LoadProhibited 读 0x98c）
- **根因**：IMU 摇晃检测（I2C 读）与音频 codec 的 I2C 配置并发（同一总线）
- **修复**：录音/播放期间跳过 IMU 读（checkShake 检查状态）

### 8. 栈溢出的连锁表现（都是栈溢出）
- BREAK 指令 + 栈指针飞到 RTC 区（A1=0x60100000）
- IDLE 任务检测（a5a5a5a5 填充被写穿）
- 堆损坏（TLSF `block_locate_free` 断言）——free list 被栈溢出写穿
- InstrFetchProhibited（PC 跳飞——返回地址被破坏）
- LVGL px_map NULL（draw buffer 分配失败——内存压力）
- **定位工具**：heap poisoning（CONFIG_HEAP_POISONING_COMPREHENSIVE）+ `heap_caps_check_integrity_all(true)` + addr2line

## 三、小智 AI 对话（xiaozhi.me）

### 1. 服务器主通道已切 MQTT
- **websocket 已废弃**：`HTTP 426 Upgrade Required`（nginx 层）
- OTA 响应包含 `mqtt` 段：endpoint（mqtt.xiaozhi.me:8883）/client_id/username/password/publish_topic/subscribe_topic
- **协议**：MQTT 信令（hello/JSON）+ AES-CTR 加密 + UDP 音频（16B 头：type/flags/len/ssrc/ts/seq）
- 订阅主题：`devices/p2p/<mac 下划线>`（服务器下发的 subscribe_topic 可能是字面 "null" 占位）

### 2. 激活流程
- OTA 检查（POST /xiaozhi/ota/）→ 未绑定返回 activation.code → xiaozhi.me 网页绑定 → 之后返回配置
- **激活码不显示**：AiOta 的响应解析被 ArduinoJson is<> 问题卡住（见下）
- 绑定后服务器**仍发 test-token**（占位）——websocket 时代认证失败（已废弃）

### 3. ArduinoJson V7 的坑
- `JsonVariantConst::is<JsonObject>()` 对 const JsonDocument 的对象**异常返回 false**（即使对象存在，重序列化可见）
- **修复**：不要用 is<> 检查，直接读字段 + 空值/值校验

### 4. TLS 证书验证依赖系统时间
- **现象**：`esp-x509-crt-bundle: PK verify failed`（激活 HTTP 连接失败）
- **根因**：RTC 掉电后系统时间回退（1970/2012）→ 证书 "not yet valid"
- **修复**：激活前 SNTP 校时（`esp_netif_sntp`，时间 < 2024 时同步）

### 5. OTA 分区死循环（反复黑屏重启）
- **现象**：`image at 0x510000 has invalid magic` + `No bootable app partitions` + bootloader_reset 循环（百余次）
- **根因**：dual-OTA 下 app 未 mark valid → bootloader 写对侧回退 entry（seq=2）→ 崩溃重启误选空 slot 1
- **修复**：`esp_ota_mark_app_valid_cancel_rollback()` + `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y`
- **恢复**：`esptool write_flash 0xd000 <全FF文件>` 清 ota_data
- **注意**：ota_data 的 entry1（seq=2）残留是 bootloader 固定写入（CRC 对 seq 计算），实际无效（CRC 不匹配）会被忽略

### 6. 其他
- **Clear AI Config**（设置 → Firmware）：只删 url/host/port/token，**保留 client_id**（设备身份，删了会漂移导致服务器拒绝）
- 录音采样率必须与服务器一致（24kHz，官方 stopwatch 板级配置 AUDIO_INPUT/OUTPUT_SAMPLE_RATE=24000）
- AI 对话录音：server_sample_rate_ 从服务器 hello 的 audio_params 获取

### 7. 崩溃重启排查（2026-08-20 代码审查，已修）
崩溃日志按乐鑫 fatal-errors 文档对号入座后，AI 对话高概率崩溃点及修复：
- **esp-mqtt 任务栈 4096→8192**（`managed_components/78__esp-ml307/src/esp/esp_mqtt.cc`）：MQTT over SSL 的
  TLS 握手 + MQTT_EVENT_DATA 的 JSON 解析（deserializeJson/serializeJson）都在 esp-mqtt 任务上下文，
  4KB 必溢出 → 崩溃伪装成堆损坏/跳飞/随机复位
- **UDP 接收任务栈 3072→6144**（`esp_udp.cc`）：回调链 recv(lwIP)+AES-CTR(mbedtls 栈上 context)+
  AudioStreamPacket 构造，持续音频流下 3KB 不足
- **Opus decoder 预分配池被 destroy**（`main/framework/ai_chat/ai_opus.c`）：decoder deinit 与 encoder
  不一致（encoder 归还预分配池复用，decoder 直接 free）→ 第二轮播放内部 RAM 碎片化分配失败。
  已改一致：内部 RAM 归还池，PSRAM fallback 才真正 destroy
- **protocol_ 跨线程无锁**（`chat_engine.cpp`）：摇晃/抚摸注入（App 线程）与 closeChannel（net 线程）
  protocol_.reset() 竞态 use-after-free。已加 mutex_ 互斥（锁内只转移指针，析构放锁外防长持锁）
- ⚠️ **升级 esp-ml307 组件会覆盖上面两个第三方改动**，升级后需重新打
- 待确认：服务器 `{"type":"system","command":"reboot"}` 会触发设备 1s 后重启（chat_engine handleSystem），
  若"莫名重启"发生在说话前/对话中且无 panic 日志，查服务器是否下发过该命令

## 四、官方参考项目

### xiaozhi-esp32 官方（github.com/78/xiaozhi-esp32）
- `main/boards/m5stack/stopwatch/config.h`：M5StopWatch 板级配置（音频 24k、I2C/GPIO/IOE/PMIC 引脚定义）
- `main/boards/m5stack/stopwatch/config.json`：sdkconfig_append（SPIRAM OCT 80M）
- `main/protocols/mqtt_protocol.cc`：MQTT 协议完整参考（AES-CTR + UDP）

### Stackchan-Newstep（D:\StackchanNew\Stackchan-Newstep）
- `sdkconfig.defaults.esp32s3`：DMA 保留区 **96KB**、WiFi 缓冲小（3/6）+ mgmt 动态、MBEDTLS_EXTERNAL_MEM_ALLOC、cache 配置（指令 32KB/数据行 64B）
- `audio_service.cc`：**opus 编码专用任务 24KB**（与 I2S 读分离）
- BUGFIX.md：I2C 总线死锁（vTaskSuspend 停任务导致锁不释放）、esp_wifi_set_ps 错误处理、空指针保护、状态泄漏

## 五、桌面端（desktop/）

- 设备名过滤：固件广播名 `VS%02X%02X`（**无连字符**），配置 `device_name_filter` 不要带 `-`
- `_find_device` 优先按名字匹配（last_connected_address 会残留旧设备导致连错）
- Windows BLE 需 `WindowsSelectorEventLoopPolicy`
- PyQt5 托盘 + 悬浮球跨线程：用 QObject signal 桥接
- 悬浮球 LLM（润色/翻译）走 DeepSeek OpenAI 兼容接口

## 六、通用建议

1. **先看复位原因**（`[RESET] reason=N`，esp_reset_reason）：2=brownout、3=软件复位、7=WDT、11=USB
2. **栈溢出会伪装成各种崩溃**（堆损坏/I2C/显示）——backtrace 里的 `__muldf3`/`memcpy`/`block_locate_free` 都是线索
3. **改一处内存配置牵一发动全身**：内部 RAM 总量固定，预分配/保留区/缓冲/栈互相挤占
4. 崩溃后先清 ota_data 再测（防死循环干扰）
5. 用 `PYTHONUTF8=1` monitor 避免编码崩溃
