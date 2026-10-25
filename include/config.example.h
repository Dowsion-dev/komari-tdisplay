#pragma once
// 复制本文件为 config.h 并填写。config.h 不要提交到公开仓库。

// ---- WiFi（ESP32 只支持 2.4GHz）----
// 可以留空：开机后进入屏幕配网，用手机选择 WiFi 并输入密码
#define WIFI_SSID      ""
#define WIFI_PASSWORD  ""

// ---- Komari ----
// 面板地址，不要以 / 结尾。http:// 和 https:// 都可以
#define KOMARI_URL     "https://status.example.com"
// API Key：在 Komari 后台设置里生成（请求头 Authorization: Bearer <key>）。
// 公开站点可以留空；如果开了「私有站点」或想显示隐藏节点，必须填
#define KOMARI_API_KEY ""

// Komari 后台可以配置多个延迟检测任务。留空则取每台机器的第一个任务，
// 也可以填任务名（例如 "电信"）固定显示某一个
#define PING_TASK_NAME ""

// ---- 显示 ----
// 时区（POSIX 格式）。中国大陆 = "CST-8"
#define TIMEZONE        "CST-8"
// 1 或 3：画面上下颠倒就换另一个
#define SCREEN_ROTATION 1
// 数据刷新间隔（毫秒）
#define REFRESH_MS      1000
