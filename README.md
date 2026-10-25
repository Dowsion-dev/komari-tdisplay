# Komari 探针副屏（LILYGO T-Display-S3）

照 NodeSeek 帖子 post-798692 的 Sparkline TUI 布局做的 Komari 探针固件。

```
┌──────────────────────────────────────────────┐
│ DMIT                           1/3  14:30:05 │
│ AMD EPYC 9654    ● online 25d   expire 130d  │
├──────────────┬──────────────┬────────────────┤
│ CPU ▮░░░ 1.3%│ RAM ▮▮░ 9.3% │ DISK ▮░░ 13.7% │
│ 0.21 0.09 0.03│ 380M / 4.1G │ 8.7G / 63.3G   │
├──────────────┴──────────────┴──┬─────────────┤
│ NET  ↓ 7.7K   ↑ 0.4K           │ PING  199ms │
│ ▁▂▁▃▅▂▁▁▂▆▃▂▁▁▂▃▂▁▁▂▃▅▂▁▂▁▃    │─────────────│
│ ↓ 16.8G  ↑ 8.8G                │ LOSS    71% │
└────────────────────────────────┴─────────────┘
```

## 功能

- 每秒拉一次 Komari 数据，时钟实时走；机器名和 CPU 型号放不下时自动横向滚动
- 文字使用抗锯齿圆体 Nunito（SIL OFL 开源授权），数字等宽不跳动；改字体或字号：编辑 `tools/make_vlw.py` 后运行 `python3 tools/make_vlw.py`（需要 Pillow）
- 在线状态用实心方块表示：绿色在线，红色离线
- 柱状图是最近约 65 秒的网速（亮色为下行，暗色为上行），所有机器都在后台记录，切换后马上有图
- **KEY 键（GPIO14）**：上一台；**BOOT 键（GPIO0）**：下一台；**长按 KEY**：切换 4 档亮度（默认充电口在右侧摆放）
- 记住上次看的机器和亮度，重启后恢复
- 节点按 Komari 后台的权重排序；隐藏节点只有填了 API Key 才会显示
- CPU/RAM/DISK ≥75% 显示橙色，≥90% 显示红色；快到期（≤7 天）、丢包高时也会变色
- 连不上面板时屏幕底部会显示红条，写明原因（HTTP 401、WiFi 断开等）

## 数据来源

固件只调用 Komari 的 JSON-RPC 接口 `POST /api/rpc2`：

| 方法 | 用途 | 频率 |
|---|---|---|
| `common:getNodes` | 名称、CPU 型号、系统、到期时间、权重 | 5 分钟 |
| `common:getNodesLatestStatus` | CPU、负载、内存、硬盘、网速、流量、在线时长、延迟和丢包 | 每秒 |

延迟和丢包取自 Komari 的「延迟检测」任务（最近 1 小时统计）。没配任务，或者 Komari 版本较旧时，这两项显示 `--`。

## 编译与烧录

### 1. 准备

- **硬件**：LILYGO T-Display-S3（ESP32-S3R8，16MB Flash，8MB PSRAM），一根能传数据的 USB-C 线（有些线只能充电）
- **驱动**：ESP32-S3 用芯片自带的 USB，macOS、Linux、Windows 10/11 都不用另装驱动
- **网络**：开发板只能连 **2.4GHz** WiFi

### 2. 安装 PlatformIO

任选一种：

| 方式 | 命令 / 操作 |
|---|---|
| macOS（Homebrew） | `brew install platformio` |
| macOS / Linux / Windows（Python 3） | `pip3 install -U platformio`（macOS 自带的是 `pip3`，没有 `pip`） |
| 图形界面 | 安装 [VS Code](https://code.visualstudio.com/)，在扩展里搜索安装 **PlatformIO IDE** |

装好后运行 `pio --version` 能看到版本号就行。第一次编译时，PlatformIO 会自动下载 ESP32 工具链和依赖库（TFT_eSPI、ArduinoJson），大约几百 MB。

### 3. 获取代码并填写配置

```bash
git clone https://github.com/Dowsion-dev/komari-tdisplay.git
cd komari-tdisplay
cp include/config.example.h include/config.h
```

编辑 `include/config.h`：

| 配置项 | 说明 |
|---|---|
| `WIFI_SSID` / `WIFI_PASSWORD` | 2.4GHz WiFi 的名称和密码 |
| `KOMARI_URL` | Komari 面板地址，例如 `https://status.example.com`，结尾不要带 `/` |
| `KOMARI_API_KEY` | 在 Komari 后台设置里生成。公开站点可以留空；开了私有站点或想显示隐藏节点时必须填 |
| `PING_TASK_NAME` | 留空则显示每台机器的第一个延迟检测任务，也可以填任务名固定显示某一个 |
| `TIMEZONE` | POSIX 时区，中国大陆填 `CST-8` |
| `SCREEN_ROTATION` | `1` = 充电口在右，`3` = 充电口在左 |

`config.h` 已在 `.gitignore` 里，不会被提交。

### 4. 编译

```bash
pio run
```

最后看到 `[SUCCESS]` 就是编译成功。产物在 `.pio/build/tdisplay-s3/` 里。

### 5. 烧录

用 USB-C 连接开发板，然后运行：

```bash
pio run -t upload
```

PlatformIO 会自动找到串口。如果插着多个设备，可以手动指定端口：

```bash
pio run -t upload --upload-port /dev/cu.usbmodem21101
```

端口名可以用 `pio device list` 查看：macOS 上类似 `/dev/cu.usbmodemXXXX`，Linux 上类似 `/dev/ttyACM0`，Windows 上类似 `COM5`。

**找不到端口或烧录失败时**，手动进入下载模式：按住 **BOOT** 键，按一下 **RST** 键，松开 BOOT，再重新运行上面的烧录命令。烧完后按一下 RST 重启。

### 6. 查看运行日志

```bash
pio device monitor
```

按 `Ctrl+C` 退出。正常情况下屏幕会依次显示 WiFi 连接、Komari 加载，然后进入面板。

### 7. 导出单文件固件（可选）

想把固件发给别人，或者用网页工具烧录时，可以把启动程序、分区表和固件合成一个 bin，从地址 `0x0` 烧写：

```bash
pio run
mkdir -p release
pio pkg exec -p tool-esptoolpy -- esptool.py --chip esp32s3 merge_bin \
  -o release/komari-tdisplay-s3.bin \
  --flash_mode keep --flash_freq keep --flash_size keep \
  0x0 .pio/build/tdisplay-s3/bootloader.bin \
  0x8000 .pio/build/tdisplay-s3/partitions.bin \
  0xe000 ~/.platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin \
  0x10000 .pio/build/tdisplay-s3/firmware.bin
```

> ⚠️ 合并后的 bin 包含你在 `config.h` 里填的 WiFi 密码和 API Key，不要公开分享。`release/` 已在 `.gitignore` 里。

烧写这个 bin 有三种方式：

- **命令行**：
  ```bash
  pio pkg exec -p tool-esptoolpy -- esptool.py --chip esp32s3 write_flash 0x0 release/komari-tdisplay-s3.bin
  ```
- **网页**：用 Chrome 或 Edge 打开 [ESP Tool](https://espressif.github.io/esptool-js/)，点 Connect 选择开发板，地址填 `0x0`，选择 bin 文件后点 Program
- **Windows**：乐鑫的 Flash Download Tool，芯片选 ESP32-S3，地址填 `0x0`

### 8. 修改字体（可选）

字库由 `tools/make_vlw.py` 从 `tools/fonts/` 里的 TTF 生成。改字号或字重时，编辑脚本开头的 `FONTS` 列表，然后运行：

```bash
pip3 install pillow
python3 tools/make_vlw.py
```

再重新编译烧录即可。

## 常见问题

- **画面上下颠倒**：把 `config.h` 里的 `SCREEN_ROTATION` 在 1 和 3 之间切换（1 = 充电口在右，3 = 充电口在左）
- **底部红条显示 `RPC ...` 或 `HTTP 401/403`**：面板开了私有站点，需要填 `KOMARI_API_KEY`
- **只能连 2.4GHz WiFi**：ESP32-S3 不支持 5GHz
- **一直停在 WiFi 连接界面**：检查 WiFi 名称和密码，确认是 2.4GHz 网络
- **烧录报 `Failed to connect`**：换一根能传数据的线，或者按上面的方法手动进入下载模式
- **HTTPS 证书**：固件不校验证书（`setInsecure`），只读监控数据，一般够用
