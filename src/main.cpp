// Komari 探针副屏 —— LILYGO T-Display-S3
// 数据：Komari JSON-RPC (/api/rpc2) common:getNodes + common:getNodesLatestStatus
// 布局：Sparkline TUI 风格，320x170 横屏
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <TFT_eSPI.h>
#include <time.h>
#include <vector>
#include <algorithm>

#include "config.h"
#include "fonts/Nunito12.h"
#include "fonts/Nunito14.h"
#include "fonts/Nunito26.h"

#ifndef PING_TASK_NAME
#define PING_TASK_NAME ""
#endif
#ifndef SCREEN_ROTATION
#define SCREEN_ROTATION 1
#endif
#ifndef REFRESH_MS
#define REFRESH_MS 1000
#endif

// ---------------- 硬件 ----------------
static const int PIN_POWER_ON = 15;
static const int PIN_BL = 38;
static const int PIN_BTN_PREV = 14;  // KEY 键，长按切换亮度
static const int PIN_BTN_NEXT = 0;   // BOOT 键

static const int W = 320;
static const int H = 170;

// ---------------- 配色 ----------------
static const uint16_t C_BG = TFT_BLACK;
static const uint16_t C_FG = 0xEF7D;     // 主文字
static const uint16_t C_DIM = 0x8C51;    // 标签
static const uint16_t C_FAINT = 0x3186;  // 分隔线 / 进度条底纹
static const uint16_t C_BAR = 0xD69A;    // 进度条 / 柱状图
static const uint16_t C_PAGE = 0x52AA;   // 页码
static const uint16_t C_OK = 0x47E9;
static const uint16_t C_WARN = 0xFD20;
static const uint16_t C_BAD = 0xF9A6;

// ---------------- 数据 ----------------
static const int HIST = 120;

struct Node {
  String uuid, name, cpu;
  int weight = 0;
  time_t expire = 0;  // 0 = 未设置

  bool hasStatus = false;
  bool online = false;
  float cpuPct = 0, load1 = 0, load5 = 0, load15 = 0;
  int64_t ram = 0, ramTotal = 0, disk = 0, diskTotal = 0;
  int64_t netIn = 0, netOut = 0, totalUp = 0, totalDown = 0, uptime = 0;
  int pingMs = -1;
  float loss = -1;

  uint32_t histIn[HIST] = {0};
  uint32_t histOut[HIST] = {0};
  int histHead = 0, histCount = 0;
};

static std::vector<Node> nodes;
static SemaphoreHandle_t dataMutex;
static volatile int current = 0;
static String lastError;
static uint32_t lastOkMs = 0;
static String savedUuid;

static TFT_eSPI tft;
static TFT_eSprite spr(&tft);
static Preferences prefs;

static const uint8_t BRIGHTNESS[] = {255, 140, 60, 20};
static int brightnessIdx = 0;

// ---------------- 工具函数 ----------------
static int64_t daysFromCivil(int y, unsigned m, unsigned d) {
  y -= m <= 2;
  const int64_t era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = (unsigned)(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + (int64_t)doe - 719468;
}

// 解析 "2026-12-01T00:00:00Z" / "2026-12-01T00:00:00.123+08:00"
static time_t parseIso8601(const char *s) {
  if (!s || !*s) return 0;
  int Y, M, D, h = 0, mi = 0, sec = 0;
  if (sscanf(s, "%d-%d-%dT%d:%d:%d", &Y, &M, &D, &h, &mi, &sec) < 3) return 0;
  if (Y <= 1970) return 0;
  int64_t t = daysFromCivil(Y, M, D) * 86400 + h * 3600 + mi * 60 + sec;
  const char *tz = strchr(s, 'T');
  if (tz) {
    tz += 9;  // 跳过 "Thh:mm:ss"
    while (*tz == '.' || isdigit((unsigned char)*tz)) tz++;
    if (*tz == '+' || *tz == '-') {
      int oh = 0, om = 0;
      sscanf(tz + 1, "%d:%d", &oh, &om);
      int off = oh * 3600 + om * 60;
      t -= (*tz == '+') ? off : -off;
    }
  }
  return (time_t)t;
}

// 380M / 4.1G / 16.8G
static String fmtBytes(double v) {
  static const char units[] = {'B', 'K', 'M', 'G', 'T', 'P'};
  int u = 0;
  while (v >= 1000 && u < 5) {
    v /= 1024;
    u++;
  }
  char buf[16];
  if (u == 0) snprintf(buf, sizeof buf, "%dB", (int)v);
  else if (v < 100) snprintf(buf, sizeof buf, "%.1f%c", v, units[u]);
  else snprintf(buf, sizeof buf, "%.0f%c", v, units[u]);
  return buf;
}

static String fmtDuration(int64_t s) {
  char buf[16];
  if (s >= 86400) snprintf(buf, sizeof buf, "%lldd", (long long)(s / 86400));
  else if (s >= 3600) snprintf(buf, sizeof buf, "%lldh", (long long)(s / 3600));
  else snprintf(buf, sizeof buf, "%lldm", (long long)(s / 60));
  return buf;
}

// ---------------- 网络 ----------------
static WiFiClientSecure tlsClient;
static WiFiClient plainClient;
static HTTPClient http;

// 返回 true 时 doc["result"] 可用；否则 lastError 写入原因
static bool rpcCall(const char *method, JsonDocument &doc, String &lastError) {
  if (WiFi.status() != WL_CONNECTED) {
    lastError = "WiFi disconnected";
    return false;
  }
  String url = String(KOMARI_URL) + "/api/rpc2";
  bool tls = url.startsWith("https://");
  http.setReuse(true);
  http.setConnectTimeout(5000);
  http.setTimeout(6000);
  bool ok = tls ? http.begin(tlsClient, url) : http.begin(plainClient, url);
  if (!ok) {
    lastError = "bad KOMARI_URL";
    return false;
  }
  http.addHeader("Content-Type", "application/json");
  if (strlen(KOMARI_API_KEY) > 0) http.addHeader("Authorization", String("Bearer ") + KOMARI_API_KEY);

  String body = String("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"") + method + "\",\"params\":{}}";
  int code = http.POST(body);
  if (code != 200) {
    lastError = code < 0 ? String("HTTP ") + http.errorToString(code) : String("HTTP ") + code;
    http.end();
    return false;
  }
  String payload = http.getString();
  http.end();

  DeserializationError err = deserializeJson(doc, payload);
  if (err) {
    lastError = String("JSON ") + err.c_str();
    return false;
  }
  if (!doc["error"].isNull()) {
    lastError = String("RPC ") + (const char *)(doc["error"]["message"] | "error");
    return false;
  }
  if (!doc["result"].is<JsonObject>()) {
    lastError = "RPC empty result";
    return false;
  }
  return true;
}

static bool fetchNodes(String &err) {
  JsonDocument doc;
  if (!rpcCall("common:getNodes", doc, err)) return false;

  std::vector<Node> fresh;
  for (JsonPair kv : doc["result"].as<JsonObject>()) {
    JsonObject o = kv.value().as<JsonObject>();
    Node n;
    n.uuid = kv.key().c_str();
    n.name = (const char *)(o["name"] | "");
    n.cpu = (const char *)(o["cpu_name"] | "");
    n.weight = o["weight"] | 0;
    n.expire = parseIso8601(o["expired_at"] | "");
    n.cpu.trim();
    fresh.push_back(n);
  }
  std::sort(fresh.begin(), fresh.end(), [](const Node &a, const Node &b) {
    if (a.weight != b.weight) return a.weight < b.weight;
    return a.name < b.name;
  });

  xSemaphoreTake(dataMutex, portMAX_DELAY);
  String curUuid = (current < (int)nodes.size()) ? nodes[current].uuid : savedUuid;
  // 保留已有的状态与流量历史
  for (Node &n : fresh) {
    for (const Node &old : nodes) {
      if (old.uuid != n.uuid) continue;
      String name = n.name, cpu = n.cpu;
      int weight = n.weight;
      time_t expire = n.expire;
      n = old;
      n.name = name; n.cpu = cpu; n.weight = weight; n.expire = expire;
      break;
    }
  }
  nodes.swap(fresh);
  current = 0;
  for (int i = 0; i < (int)nodes.size(); i++)
    if (nodes[i].uuid == curUuid) current = i;
  xSemaphoreGive(dataMutex);
  return true;
}

static bool fetchStatus(String &err) {
  JsonDocument doc;
  if (!rpcCall("common:getNodesLatestStatus", doc, err)) return false;
  JsonObject res = doc["result"].as<JsonObject>();

  xSemaphoreTake(dataMutex, portMAX_DELAY);
  for (Node &n : nodes) {
    JsonObject s = res[n.uuid];
    if (s.isNull()) {
      n.online = false;
      continue;
    }
    n.hasStatus = true;
    n.online = s["online"] | false;
    n.cpuPct = s["cpu"] | 0.0f;
    n.load1 = s["load"] | 0.0f;
    n.load5 = s["load5"] | 0.0f;
    n.load15 = s["load15"] | 0.0f;
    n.ram = s["ram"] | 0LL;
    n.ramTotal = s["ram_total"] | 0LL;
    n.disk = s["disk"] | 0LL;
    n.diskTotal = s["disk_total"] | 0LL;
    n.netIn = n.online ? (s["net_in"] | 0LL) : 0;
    n.netOut = n.online ? (s["net_out"] | 0LL) : 0;
    n.totalUp = s["net_total_up"] | 0LL;
    n.totalDown = s["net_total_down"] | 0LL;
    n.uptime = s["uptime"] | 0LL;

    n.pingMs = -1;
    n.loss = -1;
    for (JsonPair p : s["ping"].as<JsonObject>()) {
      const char *tname = p.value()["name"] | "";
      if (strlen(PING_TASK_NAME) > 0 && strcmp(tname, PING_TASK_NAME) != 0) continue;
      n.pingMs = p.value()["latest"] | -1;
      n.loss = p.value()["loss"] | -1.0f;
      break;
    }

    n.histIn[n.histHead] = (uint32_t)std::min<int64_t>(n.netIn, UINT32_MAX);
    n.histOut[n.histHead] = (uint32_t)std::min<int64_t>(n.netOut, UINT32_MAX);
    n.histHead = (n.histHead + 1) % HIST;
    if (n.histCount < HIST) n.histCount++;
  }
  xSemaphoreGive(dataMutex);
  return true;
}

static void netTask(void *) {
  uint32_t lastNodes = 0;
  for (;;) {
    uint32_t start = millis();
    bool ok = true;
    String err;
    if (nodes.empty() || millis() - lastNodes > 5 * 60 * 1000) {
      ok = fetchNodes(err);
      if (ok) lastNodes = millis();
    }
    if (ok && !nodes.empty()) ok = fetchStatus(err);
    // lastError 在渲染线程里也会读，统一在锁内更新
    xSemaphoreTake(dataMutex, portMAX_DELAY);
    if (ok) lastOkMs = millis();
    lastError = ok ? String() : err;
    xSemaphoreGive(dataMutex);
    uint32_t spent = millis() - start;
    vTaskDelay(pdMS_TO_TICKS(spent < REFRESH_MS ? REFRESH_MS - spent : 50));
  }
}

// ---------------- 绘制 ----------------
// 抗锯齿字体 Nunito（tools/make_vlw.py 生成）；F_TINY 是内置 8px 点阵字，只用于红色错误条
enum Font { F_TINY, F_SMALL, F_MAIN, F_BIG };

static void useFont(Font f) {
  static int loaded = -1;
  if (loaded == f) return;
  if (loaded > F_TINY) spr.unloadFont();
  if (f == F_SMALL) spr.loadFont(Nunito12);
  else if (f == F_MAIN) spr.loadFont(Nunito14);
  else if (f == F_BIG) spr.loadFont(Nunito26);
  else spr.setTextFont(1);
  loaded = f;
}

static int textW(const String &s, Font f = F_MAIN) {
  useFont(f);
  return spr.textWidth(s);
}

// y 对 TL/TC/TR 是文字框顶部（保持和原 16px 字体同样的视觉位置），对 *_BASELINE 是基线
static void text(const String &s, int x, int y, uint16_t color, uint8_t datum = TL_DATUM, Font f = F_MAIN) {
  useFont(f);
  spr.setTextColor(color, C_BG);  // 背景色用于抗锯齿混色
  spr.setTextDatum(datum);
  if (datum <= TR_DATUM) y += (f == F_SMALL) ? 3 : (f == F_MAIN) ? 2 : 0;
  spr.drawString(s, x, y);
}

// 优先用 14px 字体，放不下退回 12px
static void textFit(const String &s, int x, int y, int maxW, uint16_t color) {
  if (textW(s) <= maxW) text(s, x, y, color);
  else text(s, x, y, color, TL_DATUM, F_SMALL);
}

static void arrow(int x, int y, bool down, uint16_t color) {
  if (down) {
    spr.drawFastVLine(x + 3, y + 2, 5, color);
    spr.fillTriangle(x, y + 6, x + 6, y + 6, x + 3, y + 11, color);
  } else {
    spr.fillTriangle(x + 3, y + 2, x, y + 7, x + 6, y + 7, color);
    spr.drawFastVLine(x + 3, y + 7, 5, color);
  }
}

static uint16_t usageColor(float pct) {
  if (pct >= 90) return C_BAD;
  if (pct >= 75) return C_WARN;
  return C_BAR;
}

static void bar(int x, int y, int w, int h, float pct) {
  for (int i = x; i < x + w; i += 2)
    for (int j = y; j < y + h; j += 2) spr.drawPixel(i, j, C_FAINT);
  int fw = (int)(w * constrain(pct, 0.0f, 100.0f) / 100.0f + 0.5f);
  if (pct > 0 && fw < 2) fw = 2;
  spr.fillRect(x, y, fw, h, usageColor(pct));
}

static void column(int x, int w, const char *label, float pct, const String &detail) {
  text(label, x, 56, C_DIM);
  char buf[12];
  snprintf(buf, sizeof buf, "%.1f%%", pct);
  text(buf, x + w, 56, C_FG, TR_DATUM);
  int bx = x + textW(label) + 6;
  int bw = x + w - textW(buf) - 6 - bx;
  bar(bx, 61, bw, 7, pct);
  textFit(detail, x, 76, w, C_FG);
}

static void sparkline(const Node &n, int x, int y, int w, int h) {
  const int step = 3;  // 2px 柱 + 1px 间隔
  int count = std::min(n.histCount, w / step);
  uint32_t peak = 1024;
  for (int i = 0; i < count; i++) {
    int idx = (n.histHead - 1 - i + HIST) % HIST;
    peak = std::max(peak, n.histIn[idx] + n.histOut[idx]);
  }
  spr.drawFastHLine(x, y + h, w, C_FAINT);
  for (int i = 0; i < count; i++) {
    int idx = (n.histHead - 1 - i + HIST) % HIST;
    int bx = x + w - (i + 1) * step;
    int hin = (int)((uint64_t)n.histIn[idx] * h / peak);
    int hout = (int)((uint64_t)n.histOut[idx] * h / peak);
    if (hin + hout == 0) continue;
    spr.fillRect(bx, y + h - hin, 2, hin, C_BAR);
    spr.fillRect(bx, y + h - hin - hout, 2, hout, C_DIM);
  }
}

// 放不下时循环横向滚动；每轮回到开头停 1.5 秒，文字变化（切换机器）时从头开始
struct Marquee {
  String text;
  int offset = 0;
  uint32_t pauseUntil = 0;
};
static Marquee nameMarquee, cpuMarquee;

static void drawMarquee(Marquee &m, const String &s, int x, int y, int w, uint16_t color) {
  if (s != m.text) {
    m.text = s;
    m.offset = 0;
    m.pauseUntil = millis() + 1500;
  }
  int tw = textW(s);
  spr.setViewport(x, y, w, 20);
  if (tw <= w) {
    text(s, 0, 0, color);
  } else {
    const int gap = 40;
    if (millis() > m.pauseUntil) {
      m.offset = (m.offset + 1) % (tw + gap);
      if (m.offset == 0) m.pauseUntil = millis() + 1500;
    }
    text(s, -m.offset, 0, color);
    text(s, -m.offset + tw + gap, 0, color);
  }
  spr.resetViewport();
}

static void drawMessage(const String &title, const String &sub) {
  spr.fillSprite(C_BG);
  text(title, W / 2, H / 2 - 12, C_FG, MC_DATUM, F_BIG);
  text(sub, W / 2, H / 2 + 16, C_DIM, MC_DATUM);
  spr.pushSprite(0, 0);
}

static void drawDashboard() {
  xSemaphoreTake(dataMutex, portMAX_DELAY);
  if (nodes.empty()) {
    String msg = lastError.isEmpty() ? String("loading nodes...") : lastError;
    xSemaphoreGive(dataMutex);
    drawMessage("Komari", msg);
    return;
  }
  if (current >= (int)nodes.size()) current = 0;
  const Node n = nodes[current];  // 拷贝一份再画，避免长时间持锁
  const int total = nodes.size();
  const int idx = current;
  const String err = lastError;
  xSemaphoreGive(dataMutex);

  spr.fillSprite(C_BG);
  const int L = 8, R = W - 8;

  // ---- 第一行：名称 / 序号 + 时钟 ----
  struct tm tmv;
  char clock[12] = "--:--:--";
  if (getLocalTime(&tmv, 0)) strftime(clock, sizeof clock, "%H:%M:%S", &tmv);
  text(clock, R, 6, C_FG, TR_DATUM);
  int clockX = R - textW(clock) - 8;
  char page[12];
  snprintf(page, sizeof page, "%d/%d", idx + 1, total);
  text(page, clockX, 7, C_PAGE, TR_DATUM, F_SMALL);
  int titleW = clockX - textW(page, F_SMALL) - 10 - L;
  drawMarquee(nameMarquee, n.name, L, 6, titleW, C_FG);

  // ---- 第二行：CPU 型号（滚动）/ 在线 / 到期 ----
  time_t now = time(nullptr);
  String exp = "expire --";  // 仅在时间还没同步时出现
  uint16_t expColor = C_DIM;
  if (n.expire == 0) {
    exp = "expire ∞";  // 没填到期时间，视为长期有效
  } else if (now > 1700000000) {
    long days = (long)((n.expire - now) / 86400);
    if (n.expire < now) { exp = "expired"; expColor = C_BAD; }
    else if (days > 3650) exp = "expire \u221e";  // 长期有效
    else { exp = "expire " + String(days) + "d"; if (days <= 7) expColor = C_WARN; }
  }
  text(exp, R, 26, expColor, TR_DATUM);
  // 按最长可能的文字预留固定槽位，方块和 online 文字位置不随内容变化
  static const int boxX = R - textW("expire 0000d") - 12 - textW("online 0000d") - 12;
  spr.fillRect(boxX, 31, 7, 7, n.online ? C_OK : C_BAD);  // 7x7 实心方块：绿 = 在线，红 = 离线
  String up = n.online ? "online " + fmtDuration(n.uptime) : String("offline");
  text(up, boxX + 12, 26, C_DIM);
  drawMarquee(cpuMarquee, n.cpu, L, 26, boxX - 10 - L, C_DIM);

  spr.drawFastHLine(L, 47, R - L, C_FAINT);

  // ---- 中部三栏：CPU / RAM / DISK ----
  const int colW = (R - L - 2 * 13) / 3;
  const int c1 = L, c2 = L + colW + 13, c3 = L + 2 * (colW + 13);
  char load[24];
  snprintf(load, sizeof load, "%.2f %.2f %.2f", n.load1, n.load5, n.load15);
  float ramPct = n.ramTotal ? 100.0f * n.ram / n.ramTotal : 0;
  float diskPct = n.diskTotal ? 100.0f * n.disk / n.diskTotal : 0;
  column(c1, colW, "CPU", n.cpuPct, load);
  column(c2, colW, "RAM", ramPct, fmtBytes(n.ram) + " / " + fmtBytes(n.ramTotal));
  column(c3, colW, "DISK", diskPct, fmtBytes(n.disk) + " / " + fmtBytes(n.diskTotal));
  spr.drawFastVLine(c2 - 7, 54, 40, C_FAINT);
  spr.drawFastVLine(c3 - 7, 54, 40, C_FAINT);

  spr.drawFastHLine(L, 99, R - L, C_FAINT);

  // ---- 底部：网络 + 柱状图 / PING / LOSS ----
  const int panelX = c3 - 3;  // 竖线落在 panelX - 4，与 DISK 左侧分割线 (c3 - 7) 对齐
  text("NET", L, 104, C_DIM);
  int sx = L + 38;
  arrow(sx, 104, true, C_FG);
  text(fmtBytes(n.netIn), sx + 10, 104, C_FG);
  sx += 76;
  arrow(sx, 104, false, C_FG);
  text(fmtBytes(n.netOut), sx + 10, 104, C_FG);

  sparkline(n, L, 123, panelX - 10 - L, 24);

  int tx = L;
  arrow(tx, 151, true, C_DIM);
  text(fmtBytes(n.totalDown), tx + 10, 151, C_DIM);
  tx += 76;
  arrow(tx, 151, false, C_DIM);
  text(fmtBytes(n.totalUp), tx + 10, 151, C_DIM);

  spr.drawFastVLine(panelX - 4, 104, 62, C_FAINT);
  text("PING", panelX + 4, 112, C_DIM);
  String ping = n.pingMs >= 0 ? String(n.pingMs) + "ms" : String("--");
  uint16_t pingColor = n.pingMs < 0 ? C_DIM : n.pingMs > 300 ? C_WARN : C_FG;
  text(ping, R, 112, pingColor, TR_DATUM);
  spr.drawFastHLine(panelX + 4, 133, R - panelX - 4, C_FAINT);
  text("LOSS", panelX + 4, 142, C_DIM);
  String loss = n.loss >= 0 ? String((int)(n.loss + 0.5f)) + "%" : String("--");
  uint16_t lossColor = n.loss < 0 ? C_DIM : n.loss >= 20 ? C_BAD : n.loss >= 5 ? C_WARN : C_FG;
  text(loss, R, 142, lossColor, TR_DATUM);

  // 超过 10 秒没拿到新数据时，在底部提示错误原因
  if (!err.isEmpty() && millis() - lastOkMs > 10000) {
    spr.fillRect(0, H - 14, W, 14, C_BAD);
    useFont(F_TINY);
    spr.setTextColor(TFT_WHITE);
    spr.setTextDatum(TL_DATUM);
    spr.drawString("! " + err, L, H - 11);
  }

  spr.pushSprite(0, 0);
}

// ---------------- 按键 ----------------
struct Button {
  int pin;
  explicit Button(int p) : pin(p) {}
  bool down = false;
  uint32_t since = 0;
  bool longFired = false;
};
static Button btnPrev{PIN_BTN_PREV}, btnNext{PIN_BTN_NEXT};

static void setBrightness(int i) {
  brightnessIdx = i % sizeof(BRIGHTNESS);
  ledcWrite(0, BRIGHTNESS[brightnessIdx]);
}

static void switchNode(int delta) {
  xSemaphoreTake(dataMutex, portMAX_DELAY);
  if (!nodes.empty()) {
    current = (current + delta + nodes.size()) % nodes.size();
    prefs.putString("uuid", nodes[current].uuid);
  }
  xSemaphoreGive(dataMutex);
}

// 返回 1 = 短按，2 = 长按
static int pollButton(Button &b) {
  bool pressed = digitalRead(b.pin) == LOW;
  uint32_t now = millis();
  int ev = 0;
  if (pressed && !b.down) {
    b.down = true;
    b.since = now;
    b.longFired = false;
  } else if (pressed && b.down && !b.longFired && now - b.since > 700) {
    b.longFired = true;
    ev = 2;
  } else if (!pressed && b.down) {
    b.down = false;
    if (!b.longFired && now - b.since > 30) ev = 1;
  }
  return ev;
}

// ---------------- 入口 ----------------
void setup() {
  pinMode(PIN_POWER_ON, OUTPUT);
  digitalWrite(PIN_POWER_ON, HIGH);
  pinMode(PIN_BTN_PREV, INPUT_PULLUP);
  pinMode(PIN_BTN_NEXT, INPUT_PULLUP);
  Serial.begin(115200);

  prefs.begin("komari", false);
  savedUuid = prefs.getString("uuid", "");

  tft.init();
  tft.setRotation(SCREEN_ROTATION);
  tft.fillScreen(C_BG);
  ledcSetup(0, 5000, 8);
  ledcAttachPin(PIN_BL, 0);
  setBrightness(prefs.getUChar("bl", 0));

  spr.setColorDepth(16);
  if (!spr.createSprite(W, H)) {
    spr.setColorDepth(8);
    spr.createSprite(W, H);
  }
  spr.setTextWrap(false, false);  // 抗锯齿字体默认超宽自动换行，会让滚动文字折成两行

  dataMutex = xSemaphoreCreateMutex();
  tlsClient.setInsecure();  // 不校验证书；只读监控数据，够用

  drawMessage("WiFi", String("connecting ") + WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED) {
    delay(200);
    if (millis() - t0 > 30000) {
      drawMessage("WiFi", "connect failed, retrying...");
      WiFi.disconnect();
      WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
      t0 = millis();
    }
  }
  configTzTime(TIMEZONE, "ntp.aliyun.com", "pool.ntp.org", "time.cloudflare.com");
  drawMessage("Komari", WiFi.localIP().toString());

  xTaskCreatePinnedToCore(netTask, "net", 16384, nullptr, 1, nullptr, 0);
}

void loop() {
  int ep = pollButton(btnPrev);
  int en = pollButton(btnNext);
  if (ep == 1) switchNode(-1);
  if (en == 1) switchNode(+1);
  if (ep == 2) {  // 长按 KEY 切换亮度
    setBrightness(brightnessIdx + 1);
    prefs.putUChar("bl", brightnessIdx);
  }

  static uint32_t lastFrame = 0;
  if (millis() - lastFrame >= 33) {  // ~30fps，CPU 型号滚动够顺滑
    lastFrame = millis();
    drawDashboard();
  }
  delay(5);
}
