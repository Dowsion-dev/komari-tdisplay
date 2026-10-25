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
- 柱状图是最近约 70 秒的网速（亮色为下行，暗色为上行），所有机器都在后台记录，切换后马上有图
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

## 使用步骤

1. 安装 [VS Code](https://code.visualstudio.com/) 和 PlatformIO 插件，或者只装命令行：`pip install platformio`
2. 编辑 `include/config.h`（没有就从 `include/config.example.h` 复制一份），填写 WiFi、Komari 地址和 API Key（可选）
3. 用 USB-C 连接开发板，编译并烧录：

   ```bash
   pio run -t upload
   ```

   如果烧录时找不到端口：按住 BOOT 键，按一下 RST，松开 BOOT，再重新烧录
4. 查看串口日志：`pio device monitor`

## 常见问题

- **画面上下颠倒**：把 `config.h` 里的 `SCREEN_ROTATION` 在 1 和 3 之间切换（1 = 充电口在右，3 = 充电口在左）
- **底部红条显示 `RPC ...` 或 `HTTP 401/403`**：面板开了私有站点，需要填 `KOMARI_API_KEY`
- **只能连 2.4GHz WiFi**：ESP32-S3 不支持 5GHz
- **HTTPS 证书**：固件不校验证书（`setInsecure`），只读监控数据，一般够用
