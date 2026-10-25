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

## 下载固件

不想自己编译的话，直接到 [Releases](https://github.com/Dowsion-dev/komari-tdisplay/releases) 下载。每个版本有两个固件文件：

| 文件 | 包含内容 | 烧写地址 | 已保存的配置 | 什么时候用 |
|---|---|---|---|---|
| `komari-tdisplay-s3-<版本>-full.bin` | 启动程序 + 分区表 + OTA 引导数据 + 应用 | `0x0` | **会被清空** | 第一次烧录；之前刷过别的固件；设备启动异常需要完整重刷；想恢复出厂 |
| `komari-tdisplay-s3-<版本>-app.bin` | 只有应用 | `0x10000` | 保留 | 已经在用本固件，只想升级到新版本 |

> ⚠️ 两个文件的烧写地址不同，不要弄混：`full.bin` 烧到 `0x10000` 或 `app.bin` 烧到 `0x0`，设备都无法启动。重新按正确地址烧一次 `full.bin` 就能恢复。

**full.bin 为什么会清空配置**：`full.bin` 是把几个分区合并成的一个连续文件，从 `0x0` 一直覆盖到应用结尾。中间 `0x9000–0xDFFF` 是 NVS 存储区，合并时被填成了空白，所以烧写后，配网保存的 WiFi 和 Komari 设置、亮度、上次查看的机器都会被抹掉，开机重新进入配网。`app.bin` 只写应用分区，NVS 不受影响，升级后直接用原来的设置联网。

**其他文件**：`SHA256SUMS.txt` 是校验值，下载后可以运行 `shasum -a 256 -c SHA256SUMS.txt`（Linux 用 `sha256sum -c SHA256SUMS.txt`）确认文件完整。

Release 里的是通用固件，不含任何个人配置，开机后用[屏幕配网](#配网)设置 WiFi 和 Komari 面板。烧录方法见[烧录](#5-烧录)和[导出单文件固件](#7-导出单文件固件可选)两节。

## 功能

- 每秒拉一次 Komari 数据，时钟实时走；机器名和 CPU 型号放不下时自动横向滚动
- 文字使用抗锯齿圆体 Nunito（SIL OFL 开源授权），数字等宽不跳动；改字体或字号：编辑 `tools/make_vlw.py` 后运行 `python3 tools/make_vlw.py`（需要 Pillow）
- 在线状态用实心方块表示：绿色在线，红色离线
- 柱状图是最近约 65 秒的网速（亮色为下行，暗色为上行），所有机器都在后台记录，切换后马上有图
- **KEY 键（GPIO14）**：上一台；**BOOT 键（GPIO0）**：下一台；**长按 KEY**：切换 4 档亮度；**长按 BOOT 3 秒**：进入配网（默认充电口在右侧摆放）
- **屏幕配网**：手机扫码连上设备热点，在网页里选择附近的 WiFi、输入密码，也可以改 Komari 地址和 API Key，不用重新编译
- 记住上次看的机器和亮度，重启后恢复
- 节点按 Komari 后台的权重排序；隐藏节点只有填了 API Key 才会显示
- CPU/RAM/DISK ≥75% 显示橙色，≥90% 显示红色；快到期（≤7 天）、丢包高时也会变色
- 连不上面板时屏幕底部会显示红条，写明原因（HTTP 401、WiFi 断开等）

## 配网

以下情况设备会进入配网模式：

- 第一次开机，还没有配置 WiFi
- 20 秒内连不上已保存的 WiFi（比如密码改了、换了路由器）
- 运行中**长按 BOOT 键 3 秒**，或开机连接 WiFi 时长按 BOOT 键 3 秒

配网步骤：

1. 屏幕左边会显示一个二维码，用手机相机扫码，连上设备热点 `Komari-Setup-XXXX`（开放热点，没有密码）。也可以在手机 WiFi 设置里手动连
2. 连上后手机一般会自动弹出配网页面；没弹出的话，用浏览器打开 `http://192.168.4.1`
3. 从列表里选择附近的 WiFi（按信号强弱排序，🔒 表示需要密码），输入密码。隐藏网络选「其他网络（手动输入）」
4. 需要的话，展开「Komari 面板设置」修改面板地址和 API Key
5. 点「保存并重启」，设备会自动连接新的 WiFi

补充说明：

- 如果没换 WiFi、只想改面板地址，密码可以留空，会沿用已保存的密码；API Key 留空同样表示不修改
- 配网页面保存的设置优先于 `config.h`，所以 `config.h` 里的 WiFi 和 Komari 配置可以留空，同一个固件可以给不同的人用
- 配网模式下按 **KEY** 键会退出配网，重新连接已保存的 WiFi；5 分钟没有手机连上热点，也会自动重启重试，防止路由器临时断电后一直卡在配网
- 只支持 2.4GHz WiFi

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
| `WIFI_SSID` / `WIFI_PASSWORD` | 2.4GHz WiFi 的名称和密码。可以留空，开机后用屏幕配网设置 |
| `KOMARI_URL` | Komari 面板地址，例如 `https://status.example.com`，结尾不要带 `/`。也可以在配网页面里填 |
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

> ⚠️ 合并后的 bin 包含你在 `config.h` 里填的 WiFi 密码和 API Key，不要公开分享。`release/` 已在 `.gitignore` 里。想做一个能公开分享的通用固件，把 `config.h` 里的 WiFi、Komari 地址和 API Key 都留空再编译，拿到固件的人用屏幕配网填写自己的设置。

烧写这个 bin 有三种方式：

- **命令行**：
  ```bash
  pio pkg exec -p tool-esptoolpy -- esptool.py --chip esp32s3 write_flash 0x0 release/komari-tdisplay-s3.bin
  ```
- **网页**：用 Chrome 或 Edge 打开 [ESP Tool](https://espressif.github.io/esptool-js/)，点 Connect 选择开发板，地址填 `0x0`，选择 bin 文件后点 Program
- **Windows**：乐鑫的 Flash Download Tool，芯片选 ESP32-S3，地址填 `0x0`

### 8. 发布新版本（维护者）

仓库里的 GitHub Actions（`.github/workflows/build.yml`）会自动编译：

- 推送到 `main`（改了源码或配置）和提交 PR 时，只编译并上传产物，可以在 Actions 运行详情的 Artifacts 里下载
- 推送 `v` 开头的标签时，编译后自动创建 Release，附带 `-full.bin`、`-app.bin` 和 `SHA256SUMS.txt`

```bash
git tag v1.0.0
git push origin v1.0.0
```

CI 编译时用的是 `config.example.h`，所以发布出去的固件不会包含你本地 `config.h` 里的任何配置。

### 9. 修改字体（可选）

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
