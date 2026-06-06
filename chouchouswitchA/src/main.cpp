/**
 * 开关 A - ESP32-C3 Super Mini
 * 按下按钮时通过 ESP-NOW 向主控发送 "A"，主控可识别是开关A 按下。
 * 开关周围一圈 WS281x 灯环（5050，一圈 12 颗），串口命令可测试灯效，后续主程序控制颜色。
 *
 * 引脚约定（按你最新接线）：
 *   - 中间白色指示灯：GPIO3
 *   - 按钮：GPIO2（按钮另一端接 GND，内部上拉，按下为低电平）
 *   - 灯环数据线：GPIO4（WS281x 灯环，VCC 接 5V/12V，GND 共地）
 */
#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_now.h>
#include <ArduinoJson.h>
#include <ArduinoOTA.h>
#include <FastLED.h>
#include <WebServer.h>
#include <Preferences.h>
#include <ESPmDNS.h>
#include <HTTPClient.h>
#include <esp_sleep.h>

// WiFi 配置（用于 OTA，与主控同一网络；ESP-NOW 不依赖路由器）
// 不再写死 WiFi，改为：优先使用 NVS 里保存的 SSID/密码；没有或连不上则开 AP 让手机配置。
const char* defaultSsid     = "CMCC-tpXT";   // 可作为初始默认值（没有配置且能连上时）
const char* defaultPassword = "rw6s6we7";
const char* hostname = "ESP32-C3-SwitchA";
const char* hostEspNowInfoUrl = "http://ESP32-C3-LED.local/espnow-info";
const bool ENABLE_OFFLINE_MODE = true;  // 离线模式开关：无网时保持 ESP-NOW 可用
String cachedHostIp;

// 开关电路 & 指示灯
#define BUTTON_GPIO        2    // 按钮 GPIO2-GND，内部上拉，按下为 LOW
#define WHITE_LED_GPIO     3    // 中间白色指示灯 GPIO3
// 板载蓝灯 CD2（GPIO8）：用作上电/运行指示灯。LOW=亮，HIGH=灭；进深度睡眠前拉 HIGH 省电
#define BOARD_POWER_LED_GPIO  8
#define DEBOUNCE_STABLE_MS 12   // 稳定 12ms 认一次边沿，过大会漏检快按
#define DEBOUNCE_PRESS_MS  80   // 两次按下至少间隔 80ms，支持快速连按匹配

// WS281x 灯环（5050，一圈 12 颗，可按实际数量改 NUM_LEDS）
#define LED_PIN      4
#define NUM_LEDS     12
#define LED_TYPE     WS2812B
#define COLOR_ORDER  GRB
#define BRIGHTNESS   80

CRGB leds[NUM_LEDS];

// WiFi 运行时状态
Preferences wifiPrefs;
WebServer configServer(80);
String wifiSsid;
String wifiPassword;
bool wifiHasConfig      = false;   // NVS 是否有保存 WiFi
bool wifiConnected      = false;   // 当前是否连上 WiFi
bool wifiApMode         = false;   // 是否处于配网 AP 模式
bool httpServerStarted  = false;   // WebServer 是否已经 begin
bool mdnsReady          = false;   // mDNS 当前是否可用（用于 OTA .local）
unsigned long mdnsLastRetryMs = 0;
#define MDNS_RETRY_INTERVAL_MS 10000UL

// 调试模式（由“模式选择”进入）
bool debugMode = false;
unsigned long pressStartTime = 0;  // 当前这一按的按下时间

// 统一模式选择：长按进入 → 灯环黄 → 短按次数选择（1=调试 2=重置 3=关机 4=伴睡），5 秒无操作确认
bool modeSelectActive = false;
uint8_t modeSelectTapCount = 0;       // 0..13，1/2/3/4 对应四种模式，5+ 为取消，13 为“溢出”显示
bool nightCompanionMode = false;      // 夜间伴睡模式：灯环彩色流水 + 呼吸
unsigned long modeSelectLastTapTime = 0;
#define MODE_SELECT_ENTER_MS    (5 * 1000)    // 开发：长按 5 秒进入模式选择；正式发布改为 (30*1000)
#define MODE_SELECT_IDLE_MS    (5 * 1000)    // 进入选择后 5 秒无操作则执行/取消
#define MODE_SELECT_TAP_MAX    13             // 按到 13 显示 11 蓝 + 1 异色，再按从 1 重计
#define MODE_SELECT_TAP_THRESHOLD_MS 2000   // 低于此时长算“短按”（计为一次点击）

// 省电：未开调试且无主机联系时自动深度睡眠，按按钮唤醒（无需额外开关）
#define MIN_UPTIME_BEFORE_SLEEP_MS  (2 * 60 * 1000)  // 开机至少运行 2 分钟再考虑睡眠
#define IDLE_TIMEOUT_MS             (5 * 60 * 1000)  // 无按键且无主机联系超过 5 分钟则睡眠
#define SLEEP_CHECK_INTERVAL_MS      (30 * 1000)     // 每 30 秒检查一次是否满足睡眠条件
#define COMPANION_SLEEP_AFTER_MS    (20 * 60 * 1000) // 伴睡模式下 20 分钟无操作视为已睡着，进入深度睡眠

// 网页版“串口”日志：开启调试模式后，在 /status 页可查看最近运行输出（不连 USB 也能看）
#define LOG_MAX_LINES  40
#define LOG_MAX_LEN    80
String logBuf[LOG_MAX_LINES];
int logWriteIdx = 0;
int logLineCount = 0;

// 灯环测试模式
enum LedTestMode {
  LED_OFF,
  LED_ALL_WHITE,
  LED_RAINBOW,
  LED_ONE_BY_ONE
};
LedTestMode ledTestMode = LED_OFF;
unsigned long ledLastUpdate = 0;
int ledRainbowHue = 0;
int ledOneIndex = 0;

// 广播 MAC，主控收到后处理（无需配置主控 IP）
static const uint8_t broadcastMac[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
static const char ACK_MSG[] = "ACK_A";
#define COLOR_MSG_PREFIX_LEN 3  // "COL"

unsigned long lastPressTime = 0;
bool lastRawButton = true;     // 上次采样值
bool lastStableButton = true;  // 防抖后的稳定值
unsigned long lastButtonChangeTime = 0;
bool espNowReady = false;
uint8_t espNowChannel = 0;  // 当前 WiFi/ESP-NOW 信道，状态页显示便于与主机、B 对比
wifi_interface_t espNowIfidx = WIFI_IF_STA;
uint8_t espNowPeerChannel = 0;  // 在线=0(当前STA信道)，离线=1(AP信道)
bool hostAlignAlert = false;         // 主机信息连续获取失败时置 true，灯环红闪告警
uint8_t hostAlignFailStreak = 0;     // 连续失败次数
unsigned long hostAlignLastTryMs = 0;
bool hostAlignForceRetry = false;    // 按钮触发一次立即重试
volatile bool otaInProgress = false; // OTA 进行中时跳过阻塞的 HTTP 轮询，加快上传
#define HOST_ALIGN_POLL_MS 15000UL
#define HOST_ALIGN_FAIL_ALERT_COUNT 4
/** 最近一次确认「主机经 ESP-NOW 在线」的时间；用于避免游戏高负载时 HTTP 轮询误判断链红闪 */
static volatile unsigned long s_lastHostEspNowAliveMs = 0;
volatile unsigned long lastAckTime = 0;  // 收到主控 ACK_A，灯环显示“上线”约 2 秒
volatile bool gameColorValid = false;   // 收到主控 COL 后为 true
volatile uint8_t gameColorH = 0, gameColorS = 255, gameColorV = 255;
// COL 第 7 字节：低 bit=本开关是否可按下，高 4bit=主机 GameState（与主机 packMeta 一致）
volatile uint8_t hostColMeta = 0;
// 主控同步：游戏失败全红、倒计时红黄绿
volatile bool gameOverShow = false;
volatile unsigned long gameOverShowTime = 0;
#define GAME_OVER_SHOW_MS 8000         // 失败全红显示时长
volatile bool countdownActive = false;
volatile unsigned long countdownStartTime = 0;
#define COUNTDOWN_DURATION_MS 3000    // 倒计时总时长 3 秒
// 主控同步：本波全部消除胜利，灯环全绿
volatile bool victoryShow = false;
volatile unsigned long victoryShowTime = 0;
#define VICTORY_SHOW_MS 4000          // 胜利全绿显示时长（与主控一致）
// 主控发 RDY：待机彩色流水灯（当前轮次不需要本开关点击）
volatile bool hostReadyDisplay = false;
// 主控发 TA/TB：轮到谁按（本开关为 A）
volatile bool myTurn = false;
volatile unsigned long badPressShowTime = 0;
#define BAD_PRESS_SHOW_MS 400

void setupWiFi();
void loadWifiConfig();
void startConfigAP();
void setupOTA();
void setupEspNow();
void sendCommand(const char* cmd);
void updateLedTest();
void printLedHelp();
void onEspNowRecv(const uint8_t* mac, const uint8_t* data, int len);
void handleConfigRoot();
void handleConfigSave();
void updateWhiteLed();
void handleStatusPage();
void handleLogPage();
void addLog(const String& msg);
void handlePressEvent(unsigned long durationMs, unsigned long nowMs);
void toggleDebugMode();
void alignToHostApIfNeeded();
bool fetchHostEspNowInfo(uint8_t& channel, String& bssid);
bool parseMac(const String& macStr, uint8_t mac[6]);
bool fetchHostEspNowInfoFromUrl(const String& url, uint8_t& channel, String& bssid, String& hostIp);
void saveCachedHostIp(const String& ip);
void rebindNetworkServices();
void pollHostAlignHealth(unsigned long now, bool force);
void ensureMdnsReady(unsigned long now);

void setup() {
  Serial.begin(115200);
  delay(2000);

  addLog("========================================");
  addLog("=== ESP32-C3 开关 A (ESP-NOW + 灯环) ===");
  addLog("========================================");

  pinMode(BUTTON_GPIO, INPUT_PULLUP);
  pinMode(WHITE_LED_GPIO, OUTPUT);
  pinMode(BOARD_POWER_LED_GPIO, OUTPUT);
  digitalWrite(BOARD_POWER_LED_GPIO, LOW);   // 蓝灯 CD2：LOW=亮，表示上电/运行中
  ledcSetup(0, 5000, 8);           // 白灯用 PWM，便于呼吸效果
  ledcAttachPin(WHITE_LED_GPIO, 0);
  ledcWrite(0, 0);
  Serial.print("按钮 GPIO: ");
  Serial.println(BUTTON_GPIO);

  FastLED.addLeds<LED_TYPE, LED_PIN, COLOR_ORDER>(leds, NUM_LEDS);
  FastLED.setBrightness(BRIGHTNESS);
  FastLED.clear();
  FastLED.show();
  Serial.print("灯环: GPIO ");
  Serial.print(LED_PIN);
  Serial.print(", ");
  Serial.print(NUM_LEDS);
  Serial.println(" 颗 WS281x");

  setupWiFi();
  alignToHostApIfNeeded();
  setupOTA();
  setupEspNow();

  Serial.println("按下按钮将发送 \"A\"（主控识别为开关A）");
  printLedHelp();
}

void loadWifiConfig() {
  wifiPrefs.begin("wifi", true);
  wifiSsid = wifiPrefs.getString("ssid", "");
  wifiPassword = wifiPrefs.getString("pass", "");
  cachedHostIp = wifiPrefs.getString("hostip", "");
  wifiPrefs.end();
  wifiHasConfig = wifiSsid.length() > 0;
}

void startConfigAP() {
  wifiApMode = true;
  wifiConnected = false;
  mdnsReady = false;

  // 仅用 WIFI_AP 模式：在 AP+STA 下部分 ESP32 芯片 softAP 密码对 iPhone 会报错，单独 AP 可避免
  WiFi.mode(WIFI_AP);
  const char* apSsid = "SwitchA-Setup";
  const char* apPass = "setup1234";   // 至少 8 位，用 10 位减少兼容性问题
  WiFi.softAP(apSsid, apPass, 1);     // channel 1，提高兼容性
  IPAddress apIP = WiFi.softAPIP();

  addLog("进入 WiFi 配网模式 (AP)");
  addLog("AP SSID: " + String(apSsid) + " 密码: " + String(apPass));
  addLog("AP IP: " + apIP.toString() + " -> 浏览器打开配置");

  configServer.on("/", handleConfigRoot);
  configServer.on("/save", HTTP_POST, handleConfigSave);
  configServer.on("/status", handleStatusPage);
  configServer.on("/log", handleLogPage);
  if (!httpServerStarted) {
    configServer.begin();
    httpServerStarted = true;
  }
}

void setupWiFi() {
  loadWifiConfig();

  // 优先使用已保存配置，其次尝试默认值，最后进入 AP 配网
  String trySsid = wifiHasConfig ? wifiSsid : String(defaultSsid);
  String tryPass = wifiHasConfig ? wifiPassword : String(defaultPassword);
  wifiSsid = trySsid;
  wifiPassword = tryPass;

  if (trySsid.length() == 0) {
    addLog("没有任何 WiFi 配置，直接进入配网模式");
    startConfigAP();
    return;
  }

  Serial.print("\n连接 WiFi: ");
  Serial.println(trySsid);

  WiFi.mode(WIFI_STA);
  WiFi.setHostname(hostname);
  WiFi.begin(trySsid.c_str(), tryPass.c_str());

  unsigned long start = millis();
  wifiConnected = false;
  while (WiFi.status() != WL_CONNECTED && (millis() - start) < 15000) {
    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    wifiConnected = true;
    wifiApMode = false;
    addLog("WiFi 连接成功");
    addLog("本机 IP: " + WiFi.localIP().toString());
    addLog("STA BSSID: " + WiFi.BSSIDstr());

    // 启动 mDNS：后续可通过 ESP32-C3-SwitchA.local 访问 / OTA
    if (!MDNS.begin(hostname)) {
      addLog("mDNS 启动失败");
      mdnsReady = false;
    } else {
      addLog("mDNS: http://" + String(hostname) + ".local");
      mdnsReady = true;
    }

    // 如果是默认值连上的，而且之前没有保存配置，则写入 NVS，方便以后换路由器时还能进 AP
    if (!wifiHasConfig && trySsid == String(defaultSsid)) {
      wifiPrefs.begin("wifi", false);
      wifiPrefs.putString("ssid", trySsid);
      wifiPrefs.putString("pass", tryPass);
      wifiPrefs.end();
      wifiHasConfig = true;
    }

    // 确保调试 /status、/log 页面在 STA 模式下也可访问
    configServer.on("/status", handleStatusPage);
    configServer.on("/log", handleLogPage);
    if (!httpServerStarted) {
      configServer.begin();
      httpServerStarted = true;
    }
  } else {
    addLog("WiFi 连接失败，进入配网 AP 模式");
    startConfigAP();
  }
}

bool parseMac(const String& macStr, uint8_t mac[6]) {
  unsigned int b[6];
  if (sscanf(macStr.c_str(), "%x:%x:%x:%x:%x:%x",
             &b[0], &b[1], &b[2], &b[3], &b[4], &b[5]) != 6) {
    return false;
  }
  for (int i = 0; i < 6; i++) mac[i] = (uint8_t)b[i];
  return true;
}

void saveCachedHostIp(const String& ip) {
  if (ip.length() < 7) return;
  cachedHostIp = ip;
  wifiPrefs.begin("wifi", false);
  wifiPrefs.putString("hostip", ip);
  wifiPrefs.end();
}

bool fetchHostEspNowInfoFromUrl(const String& url, uint8_t& channel, String& bssid, String& hostIp) {
  if (!wifiConnected) return false;
  HTTPClient http;
  http.setTimeout(1500);
  if (!http.begin(url)) return false;
  int code = http.GET();
  if (code != 200) {
    http.end();
    return false;
  }
  String body = http.getString();
  http.end();
  StaticJsonDocument<192> doc;
  if (deserializeJson(doc, body) != DeserializationError::Ok) return false;
  channel = (uint8_t)(doc["channel"] | 0);
  bssid = String((const char*)(doc["bssid"] | ""));
  hostIp = String((const char*)(doc["ip"] | ""));
  return channel > 0 && bssid.length() >= 11;
}

bool fetchHostEspNowInfo(uint8_t& channel, String& bssid) {
  String hostIp;
  if (fetchHostEspNowInfoFromUrl(hostEspNowInfoUrl, channel, bssid, hostIp)) {
    if (hostIp.length() >= 7) saveCachedHostIp(hostIp);
    addLog("主机信息来源: mDNS");
    return true;
  }

  if (cachedHostIp.length() >= 7) {
    String backupUrl = "http://" + cachedHostIp + "/espnow-info";
    if (fetchHostEspNowInfoFromUrl(backupUrl, channel, bssid, hostIp)) {
      if (hostIp.length() >= 7) saveCachedHostIp(hostIp);
      addLog("主机信息来源: 缓存IP " + cachedHostIp);
      return true;
    }
  }
  return false;
}

void rebindNetworkServices() {
  if (!wifiConnected) return;
  MDNS.end();
  if (!MDNS.begin(hostname))
    addLog("mDNS 重建失败"), mdnsReady = false;
  else {
    addLog("mDNS 已重建: http://" + String(hostname) + ".local");
    mdnsReady = true;
  }

  configServer.on("/status", handleStatusPage);
  configServer.on("/log", handleLogPage);
  httpServerStarted = false;  // 切 AP 后强制重新监听，避免 /status 偶发不可达
  configServer.begin();
  httpServerStarted = true;
}

void ensureMdnsReady(unsigned long now) {
  if (wifiApMode || !wifiConnected) return;
  if (mdnsReady) return;
  if (now - mdnsLastRetryMs < MDNS_RETRY_INTERVAL_MS) return;
  mdnsLastRetryMs = now;
  MDNS.end();
  if (MDNS.begin(hostname)) {
    mdnsReady = true;
    addLog("mDNS 自动重试成功: http://" + String(hostname) + ".local");
  } else {
    addLog("mDNS 自动重试失败");
  }
}

void alignToHostApIfNeeded() {
  if (!wifiConnected) return;
  uint8_t hostCh = 0;
  String hostBssid;
  if (!fetchHostEspNowInfo(hostCh, hostBssid)) {
    addLog("未读到主机 espnow-info，保持当前 AP");
    return;
  }

  wifi_second_chan_t second = WIFI_SECOND_CHAN_NONE;
  uint8_t localCh = 0;
  if (esp_wifi_get_channel(&localCh, &second) != ESP_OK) localCh = 0;
  String localBssid = WiFi.BSSIDstr();
  addLog("主机信道/BSSID: " + String((int)hostCh) + " / " + hostBssid);
  addLog("本机信道/BSSID: " + String((int)localCh) + " / " + localBssid);

  if (localBssid.equalsIgnoreCase(hostBssid) && localCh == hostCh) {
    addLog("已与主机同 AP，无需重连");
    return;
  }

  uint8_t hostBssidMac[6];
  if (!parseMac(hostBssid, hostBssidMac)) {
    addLog("主机 BSSID 格式异常，跳过对齐");
    return;
  }

  addLog("检测到不同 AP，尝试对齐到主机 BSSID...");
  WiFi.disconnect(false, true);
  delay(120);
  WiFi.mode(WIFI_STA);
  WiFi.begin(wifiSsid.c_str(), wifiPassword.c_str(), hostCh, hostBssidMac, true);

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - start) < 12000) {
    delay(300);
  }

  if (WiFi.status() == WL_CONNECTED) {
    wifiConnected = true;
    addLog("对齐成功，当前 BSSID: " + WiFi.BSSIDstr());
    rebindNetworkServices();
  } else {
    wifiConnected = false;
    addLog("对齐失败，保持当前连接流程");
    setupWiFi();
  }
}

static void markHostEspNowReachable() {
  s_lastHostEspNowAliveMs = millis();
  hostAlignFailStreak = 0;
  hostAlignAlert = false;
}

void pollHostAlignHealth(unsigned long now, bool force) {
  if (otaInProgress) return;  // OTA 期间不执行阻塞 HTTP，避免拖慢上传
  if (!wifiConnected || wifiApMode) return;
  if (!force && (now - hostAlignLastTryMs) < HOST_ALIGN_POLL_MS) return;
  hostAlignLastTryMs = now;

  uint8_t hostCh = 0;
  String hostBssid;
  if (fetchHostEspNowInfo(hostCh, hostBssid)) {
    hostAlignFailStreak = 0;
    if (hostAlignAlert) {
      hostAlignAlert = false;
      addLog("主机对齐健康恢复正常");
    }
    alignToHostApIfNeeded();
    return;
  }

  // 对战时主机高频 COL/TA/TB 占射频，HTTP 易失败；若 ESP-NOW 仍正常则不计失败
  if ((unsigned long)(now - s_lastHostEspNowAliveMs) < 30000UL) return;

  if (hostAlignFailStreak < 255) hostAlignFailStreak++;
  if (hostAlignFailStreak >= HOST_ALIGN_FAIL_ALERT_COUNT && !hostAlignAlert) {
    hostAlignAlert = true;
    addLog("主机信息连续失败，灯环红色告警；短按按钮可立即重试");
  }
}

void setupOTA() {
  ArduinoOTA.setHostname(hostname);
  ArduinoOTA.setPassword("12345678");
  ArduinoOTA.onStart([]() {
    otaInProgress = true;
    addLog("OTA 更新开始");
    fill_solid(leds, NUM_LEDS, CRGB::Yellow);
    FastLED.show();
  });
  ArduinoOTA.onEnd([]() {
    otaInProgress = false;
    addLog("OTA 更新完成");
    fill_solid(leds, NUM_LEDS, CRGB::Green);
    FastLED.show();
    delay(2000);
  });
  // 进度节流：避免回调过于频繁时刷屏，每 80ms 最多刷新一次灯环
  static unsigned long lastProgressShow = 0;
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    unsigned int pct = total ? (unsigned int)((unsigned long)progress * 100 / total) : 0;
    Serial.printf("进度: %u%%\r", pct);
    if (!total) return;
    // 显示点个数：0 ~ NUM_LEDS，映射 progress/total；传输未结束前最多显示 NUM_LEDS-1，避免“一下就全亮”
    int progressLeds = (int)((unsigned long)progress * NUM_LEDS / total);
    if (progressLeds > NUM_LEDS) progressLeds = NUM_LEDS;
    if (progress < total && progressLeds >= NUM_LEDS) progressLeds = NUM_LEDS - 1;
    for (int i = 0; i < NUM_LEDS; i++) {
      leds[i] = (i < progressLeds) ? CRGB::Blue : CRGB::Black;
    }
    unsigned long now = millis();
    if (now - lastProgressShow >= 80) {
      lastProgressShow = now;
      FastLED.show();
    }
  });
  ArduinoOTA.onError([](ota_error_t error) {
    otaInProgress = false;
    addLog("OTA 错误[" + String((unsigned)error) + "]");
    fill_solid(leds, NUM_LEDS, CRGB::Red);
    FastLED.show();
  });
  ArduinoOTA.begin();
  addLog("OTA 就绪 (密码: 12345678)");
}

void handleConfigRoot() {
  String page = R"(
<!DOCTYPE html>
<html>
  <head>
    <meta charset="utf-8">
    <title>SwitchA WiFi 配置</title>
    <style>
      body { font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif; padding: 16px; }
      input { padding: 6px 8px; width: 260px; max-width: 100%; }
      button { padding: 6px 18px; margin-top: 12px; }
    </style>
  </head>
  <body>
    <h2>SwitchA WiFi 配置</h2>
    <form method="POST" action="/save">
      <div>
        <label>SSID：<br/><input name="ssid" /></label>
      </div>
      <br/>
      <div>
        <label>密码：<br/><input name="pass" type="password" /></label>
      </div>
      <br/>
      <button type="submit">保存并重启</button>
    </form>
    <p>保存后设备会重启并尝试连接新 WiFi。</p>
  </body>
</html>
)";
  configServer.send(200, "text/html", page);
}

void handleConfigSave() {
  String ssid = configServer.arg("ssid");
  String pass = configServer.arg("pass");
  if (ssid.length() == 0) {
    configServer.send(400, "text/plain", "SSID 不能为空");
    return;
  }

  wifiPrefs.begin("wifi", false);
  wifiPrefs.putString("ssid", ssid);
  wifiPrefs.putString("pass", pass);
  wifiPrefs.end();
  wifiHasConfig = true;
  addLog("已保存 WiFi 配置，即将重启: " + ssid);

  String resp = "已保存 WiFi 配置，设备将重启并尝试连接: " + ssid;
  configServer.send(200, "text/plain", resp);

  delay(1500);
  ESP.restart();
}

// 同时输出到串口并写入缓冲，开启调试模式后可在 /status 页查看
void addLog(const String& msg) {
  Serial.println(msg);
  String line = msg;
  if (line.length() > (unsigned)LOG_MAX_LEN) line = line.substring(0, LOG_MAX_LEN);
  logBuf[logWriteIdx] = line;
  logWriteIdx = (logWriteIdx + 1) % LOG_MAX_LINES;
  if (logLineCount < LOG_MAX_LINES) logLineCount++;
}

void updateWhiteLed() {
  static unsigned long lastToggle = 0;
  static bool state = false;
  unsigned long now = millis();

  // 调试模式：白灯呼吸，一眼能看出“进入成功”
  if (debugMode) {
    double t = now / 250.0;
    uint8_t duty = (uint8_t)((sin(t) + 1.0) * 127.5);
    ledcWrite(0, duty);
    return;
  }

  if (wifiConnected) {
    ledcWrite(0, 255);
    return;
  }

  // WiFi 未连接：根据是否处于配网 AP 模式，快/慢闪烁
  unsigned long interval = wifiApMode ? 500 : 150;
  if (now - lastToggle >= interval) {
    lastToggle = now;
    state = !state;
    ledcWrite(0, state ? 255 : 0);
  }
}

// 简单 HTML 转义，避免日志里的 < > & 破坏页面
static String escapeHtml(const String& s) {
  String out;
  out.reserve(s.length() + 10);
  for (unsigned i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '&') out += "&amp;";
    else if (c == '<') out += "&lt;";
    else if (c == '>') out += "&gt;";
    else out += c;
  }
  return out;
}

void handleStatusPage() {
  String ipSta = WiFi.isConnected() ? WiFi.localIP().toString() : String("-");
  String ipAp  = WiFi.softAPIP().toString();

  String page = "<!DOCTYPE html><html><head><meta charset='utf-8'><title>SwitchA 状态</title>";
  page += "<style>body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif;padding:16px;} "
                "code{background:#f5f5f5;padding:2px 4px;border-radius:3px;} "
                ".log{background:#1e1e1e;color:#d4d4d4;padding:8px;font-family:monospace;font-size:12px;white-space:pre-wrap;word-break:break-all;max-height:400px;overflow-y:auto;}</style></head><body>";
  page += "<h2>SwitchA 运行状态</h2>";
  page += "<p><b>主机名</b>：<code>" + String(hostname) + "</code></p>";
  page += "<p><b>调试模式</b>：" + String(debugMode ? "已开启" : "未开启") + "</p>";
  page += "<p><b>WiFi 已连接</b>：" + String(wifiConnected ? "是" : "否") + "</p>";
  page += "<p><b>当前 SSID</b>：<code>" + (wifiSsid.length() ? wifiSsid : String("-")) + "</code></p>";
  page += "<p><b>STA IP</b>：<code>" + ipSta + "</code></p>";
  page += "<p><b>通信模式</b>：" + String(wifiConnected ? "在线模式（STA）" : (ENABLE_OFFLINE_MODE ? "离线模式（AP）" : "未启用")) + "</p>";
  page += "<p><b>AP 模式</b>：" + String(wifiApMode ? "是" : "否") + "，AP IP：<code>" + ipAp + "</code></p>";
  page += "<p><b>ESP-NOW</b>：" + String(espNowReady ? "已就绪" : "未就绪") + "，<b>信道</b>：" + String((int)espNowChannel) + "（当前 STA；同 WiFi 即与主机/B 一致）</p>";
  page += "<p><b>主机对齐健康</b>：" + String(hostAlignAlert ? "异常（红灯告警，短按可立即重试）" : "正常") + "，连续失败：" + String((int)hostAlignFailStreak) + "</p>";
  page += "<p><b>灯环</b>：" + String(NUM_LEDS) + " 颗，亮度 " + String(FastLED.getBrightness()) + "</p>";
  page += "<p><b>提示</b>：长按 5 秒进入模式选择（开发；正式为 30 秒），灯环黄后按 1/2/3/4 下，5 秒无操作确认：1=调试 2=重置WiFi 3=关机 4=夜间伴睡。不按或按 5 下以上则取消。</p>";
  if (nightCompanionMode) {
    page += "<p><b>夜间伴睡</b>：已开启（灯环彩色流水+呼吸）。再次进入模式选择按 4 下可关闭。</p>";
  }

  if (debugMode && logLineCount > 0) {
    page += "<h3>最近运行日志（相当于串口输出）</h3><div id='log' class='log'>";
    for (int i = 0; i < logLineCount; i++) {
      int j = (logWriteIdx - logLineCount + i + LOG_MAX_LINES) % LOG_MAX_LINES;
      page += escapeHtml(logBuf[j]) + "\n";
    }
    page += "</div><p><small>仅保留最近 " + String(LOG_MAX_LINES) + " 条，每 1 秒自动更新（仅刷新日志区，不整页重载）。</small></p>";
    page += "<script>setInterval(function(){ fetch('log').then(function(r){ return r.text(); }).then(function(t){ var e=document.getElementById('log'); if(e) e.innerText=t; }); }, 1000);</script>";
  } else if (!debugMode) {
    page += "<p><small>开启调试模式后，此处会显示最近 " + String(LOG_MAX_LINES) + " 条运行日志，无需连接 USB 串口。</small></p>";
  }

  page += "</body></html>";
  configServer.send(200, "text/html", page);
}

// 调试模式下供页面 AJAX 拉取日志用，仅返回纯文本，每 1 秒由前端请求
void handleLogPage() {
  String text;
  for (int i = 0; i < logLineCount; i++) {
    int j = (logWriteIdx - logLineCount + i + LOG_MAX_LINES) % LOG_MAX_LINES;
    text += logBuf[j] + "\n";
  }
  configServer.send(200, "text/plain; charset=utf-8", text);
}

void onEspNowRecv(const uint8_t* mac, const uint8_t* data, int len) {
  (void)mac;
  if (!data) return;
  if (len >= 5 && memcmp(data, ACK_MSG, 5) == 0) {
    lastAckTime = millis();
    markHostEspNowReachable();
    return;
  }
  // 主控发 RDY：当前轮次不需要本开关，显示彩色流水待机
  if (len >= 3 && data[0] == 'R' && data[1] == 'D' && data[2] == 'Y') {
    markHostEspNowReachable();
    hostReadyDisplay = true;
    myTurn = false;
    return;
  }
  // 主控发 TA/TB：轮次切换
  if (len >= 2 && data[0] == 'T' && (data[1] == 'A' || data[1] == 'B')) {
    markHostEspNowReachable();
    myTurn = (data[1] == 'A');
    hostReadyDisplay = !myTurn;
    return;
  }
  // BAD：按错/不该按
  if (len >= 3 && data[0] == 'B' && data[1] == 'A' && data[2] == 'D') {
    markHostEspNowReachable();
    badPressShowTime = millis();
    return;
  }
  // 主控：COL + H + S + V [+ meta]；7 字节时 meta=低bit可按下 + 高4位游戏状态
  if (len >= 6 && data[0] == 'C' && data[1] == 'O' && data[2] == 'L') {
    markHostEspNowReachable();
    gameColorH = data[3];
    gameColorS = data[4];
    gameColorV = data[5];
    hostColMeta = (len >= 7) ? data[6] : (uint8_t)((1u << 4) | 1u);
    gameColorValid = true;
    hostReadyDisplay = false;
    myTurn = (hostColMeta & 1u) != 0;
    gameOverShow = false;
    victoryShow = false;
    return;
  }
  // 游戏失败：主控发 GOF，灯环同步全红
  if (len >= 3 && data[0] == 'G' && data[1] == 'O' && data[2] == 'F') {
    markHostEspNowReachable();
    gameOverShow = true;
    gameOverShowTime = millis();
    gameColorValid = false;
    hostReadyDisplay = false;
    countdownActive = false;
    return;
  }
  // 倒计时开始：主控发 CD，灯环同步红→黄→绿 3 秒
  if (len >= 2 && data[0] == 'C' && data[1] == 'D') {
    markHostEspNowReachable();
    countdownActive = true;
    countdownStartTime = millis();
    gameColorValid = false;
    hostReadyDisplay = false;
    gameOverShow = false;
    return;
  }
  // 本波全部消除胜利：主控发 WIN，灯环同步全绿
  if (len >= 3 && data[0] == 'W' && data[1] == 'I' && data[2] == 'N') {
    markHostEspNowReachable();
    victoryShow = true;
    victoryShowTime = millis();
    gameColorValid = false;
    hostReadyDisplay = false;
    gameOverShow = false;
    countdownActive = false;
    return;
  }
}

void setupEspNow() {
  // 离线只要求可通讯：无网时走 AP 网卡 + 固定 AP 信道 1
  if (!wifiConnected && !ENABLE_OFFLINE_MODE) {
    addLog("离线模式未启用，跳过 ESP-NOW");
    return;
  }
  espNowIfidx = wifiConnected ? WIFI_IF_STA : WIFI_IF_AP;
  espNowPeerChannel = wifiConnected ? 0 : 1;

  if (esp_now_init() != ESP_OK) {
    addLog("ESP-NOW 初始化失败");
    return;
  }
  esp_now_register_recv_cb(onEspNowRecv);
  // 显示用：当前 STA 信道（与主机、B 同 WiFi 时应一致）
  wifi_second_chan_t second = WIFI_SECOND_CHAN_NONE;
  if (esp_wifi_get_channel(&espNowChannel, &second) != ESP_OK)
    espNowChannel = 0;
  addLog("WiFi 当前信道(显示): " + String((int)espNowChannel) + "，ESP-NOW 使用 channel=0(当前)");
  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, broadcastMac, 6);
  peer.channel = espNowPeerChannel;
  peer.ifidx = espNowIfidx;
  peer.encrypt = false;
  if (esp_now_add_peer(&peer) != ESP_OK) {
    addLog("ESP-NOW 添加广播对端失败");
    return;
  }
  espNowReady = true;
  addLog(wifiConnected ? "ESP-NOW 已就绪（在线）" : "ESP-NOW 已就绪（离线 AP）");
}

void sendCommand(const char* cmd) {
  if (!espNowReady) {
    addLog("ESP-NOW 未就绪，无法发送");
    return;
  }
  size_t len = strlen(cmd);
  if (len == 0 || len > 250) return;
  esp_err_t r = esp_now_send(broadcastMac, (const uint8_t*)cmd, len);
  if (strcmp(cmd, "PA") != 0) {
    addLog("已发送: " + String(cmd) + (r == ESP_OK ? " [OK]" : " [FAIL]"));
  }
}

void printLedHelp() {
  Serial.println("--- 灯环测试（串口命令）---");
  Serial.println("  1 - 全部亮白");
  Serial.println("  2 - 彩虹循环");
  Serial.println("  3 - 逐个点亮");
  Serial.println("  4 - 关灯");
  Serial.println("  brightness 0-255 - 亮度");
  Serial.println("  status - 状态");
  Serial.println("  help / led - 显示本帮助");
}

// 处理一次完整按键事件（按下+松开），durationMs 为稳定按下时长
void handlePressEvent(unsigned long durationMs, unsigned long nowMs) {
  if (durationMs < 50) return;  // 过短视为抖动

  // 已进入模式选择：只认“短按”为点击次数，长按（如 30s 松手）忽略
  if (modeSelectActive) {
    if (durationMs >= 10000) return;  // 30s 进入后的松手，不当作一次点击
    if (durationMs < MODE_SELECT_TAP_THRESHOLD_MS) {
      modeSelectTapCount++;
      if (modeSelectTapCount > MODE_SELECT_TAP_MAX) modeSelectTapCount = 1;  // 13 后再按从 1 重来
      modeSelectLastTapTime = nowMs;
    }
    return;
  }

  // 长按 30s 的松手在 loop 里已进入模式选择，这里不再处理
  if (durationMs >= MODE_SELECT_ENTER_MS) return;

  // 正常使用：短按发送 "A"
  if (durationMs < MODE_SELECT_TAP_THRESHOLD_MS) {
    if (nowMs - lastPressTime >= DEBOUNCE_PRESS_MS) {
      lastPressTime = nowMs;
      if (hostAlignAlert) {
        hostAlignForceRetry = true;
        addLog("按键触发：立即重试主机对齐");
      }
      // 双击：发送 RST（任意时刻都可重开）
      static unsigned long lastTap = 0;
      if (lastTap && (nowMs - lastTap) < 450) {
        sendCommand("RST");
        lastTap = 0;
        return;
      }
      lastTap = nowMs;
      // 简化规则：不再校验 myTurn，按下就把 A 发给主机，让主机根据 expectedButton 判定是否有效
      sendCommand("A");
    }
  }
}

void toggleDebugMode() {
  debugMode = !debugMode;
  addLog("调试模式: " + String(debugMode ? "已开启" : "已关闭"));
}

#define ONLINE_SHOW_MS 2000  // 收到 ACK_A 后灯环显示“上线”的时长（毫秒）

void updateLedTest() {
  unsigned long now = millis();
  if (now - ledLastUpdate < 50) return;
  ledLastUpdate = now;

  // 模式选择：黄灯表示进入选择；按几下亮几颗蓝灯（1–12）；第 13 下为 11 蓝 + 1 异色
  if (modeSelectActive) {
    FastLED.clear();
    if (modeSelectTapCount == 0) {
      fill_solid(leds, NUM_LEDS, CRGB::Yellow);
    } else if (modeSelectTapCount <= 12) {
      for (int i = 0; i < modeSelectTapCount && i < NUM_LEDS; i++)
        leds[i] = CRGB::Blue;
    } else {
      for (int i = 0; i < 11 && i < NUM_LEDS; i++) leds[i] = CRGB::Blue;
      if (NUM_LEDS > 11) leds[11] = CHSV(160, 255, 255);  // 第 12 颗异色（青）
    }
    FastLED.show();
    return;
  }

  // 夜间伴睡模式：彩色流水 + 呼吸（流水用彩虹移动，整体亮度正弦呼吸）
  if (nightCompanionMode) {
    unsigned long t = millis();
    uint8_t hueOffset = (t / 40) % 256;   // 流水速度
    fill_rainbow(leds, NUM_LEDS, hueOffset, 12);
    uint8_t phase = (t / 24) % 256;       // 约 6 秒一周期
    uint8_t breath = 80 + ((175 * (uint16_t)sin8(phase)) >> 8);  // 亮度 80~255 呼吸
    nscale8(leds, NUM_LEDS, breath);
    FastLED.show();
    return;
  }
  if (badPressShowTime != 0 && (now - badPressShowTime) < BAD_PRESS_SHOW_MS) {
    fill_solid(leds, NUM_LEDS, CRGB::Red);
    FastLED.show();
    return;
  }
  // 主机 COL 同步：RUNNING 可按下=队头色呼吸；不可按/IDLE=彩虹锁定；PAUSE=琥珀呼吸
  if (gameColorValid) {
    uint8_t gs = (hostColMeta >> 4) & 0x0Fu;
    bool canPress = (hostColMeta & 1u) != 0;
    if (gs == 1) {
      if (canPress) {
        uint8_t br = beatsin8(28, 90, 255);
        fill_solid(leds, NUM_LEDS, CHSV(gameColorH, gameColorS, scale8(gameColorV, br)));
        FastLED.show();
        return;
      }
      unsigned long t = millis();
      uint8_t hueOffset = (t / 40) % 256;
      fill_rainbow(leds, NUM_LEDS, hueOffset, 12);
      uint8_t phase = (t / 24) % 256;
      uint8_t breath = 80 + ((175 * (uint16_t)sin8(phase)) >> 8);
      nscale8(leds, NUM_LEDS, breath);
      FastLED.show();
      return;
    }
    if (gs == 4) {
      uint8_t br = beatsin8(18, 60, 200);
      fill_solid(leds, NUM_LEDS, CHSV(32, 255, br));
      FastLED.show();
      return;
    }
    if (gs == 0) {
      unsigned long t = millis();
      uint8_t hueOffset = (t / 40) % 256;
      fill_rainbow(leds, NUM_LEDS, hueOffset, 12);
      uint8_t phase = (t / 24) % 256;
      uint8_t breath = 80 + ((175 * (uint16_t)sin8(phase)) >> 8);
      nscale8(leds, NUM_LEDS, breath);
      FastLED.show();
      return;
    }
  }
  // 主控发 RDY：当前不需要本开关点击，显示彩色流水灯
  if (hostReadyDisplay) {
    unsigned long t = millis();
    uint8_t hueOffset = (t / 40) % 256;
    fill_rainbow(leds, NUM_LEDS, hueOffset, 12);
    uint8_t phase = (t / 24) % 256;
    uint8_t breath = 80 + ((175 * (uint16_t)sin8(phase)) >> 8);
    nscale8(leds, NUM_LEDS, breath);
    FastLED.show();
    return;
  }

  // 收到主控 ACK_A 后，灯环显示“上线”（全绿约 2 秒）
  if (lastAckTime != 0 && (now - lastAckTime) < ONLINE_SHOW_MS) {
    fill_solid(leds, NUM_LEDS, CRGB::Green);
    FastLED.show();
    return;
  }
  // 主控同步：游戏失败全红
  if (gameOverShow) {
    if ((now - gameOverShowTime) < GAME_OVER_SHOW_MS) {
      fill_solid(leds, NUM_LEDS, CRGB::Red);
      FastLED.show();
      return;
    }
    gameOverShow = false;
  }
  // 主控同步：本波全部消除胜利，全绿
  if (victoryShow) {
    if ((now - victoryShowTime) < VICTORY_SHOW_MS) {
      fill_solid(leds, NUM_LEDS, CRGB::Green);
      FastLED.show();
      return;
    }
    victoryShow = false;
  }
  // 主控同步：倒计时红→黄→绿（与主控 3 秒一致）
  if (countdownActive) {
    unsigned long elapsed = now - countdownStartTime;
    if (elapsed >= COUNTDOWN_DURATION_MS) {
      countdownActive = false;
    } else {
      if (elapsed < 1000)
        fill_solid(leds, NUM_LEDS, CRGB::Red);
      else if (elapsed < 2000)
        fill_solid(leds, NUM_LEDS, CRGB::Yellow);
      else
        fill_solid(leds, NUM_LEDS, CRGB::Green);
      FastLED.show();
      return;
    }
  }
  if (hostAlignAlert && ledTestMode == LED_OFF) {
    static bool alignBlink = false;
    static unsigned long alignBlinkTs = 0;
    if (now - alignBlinkTs >= 250) {
      alignBlinkTs = now;
      alignBlink = !alignBlink;
    }
    fill_solid(leds, NUM_LEDS, alignBlink ? CRGB::Red : CRGB::Black);
    FastLED.show();
    return;
  }
  // WiFi 状态指示：当没有主控动画 / 游戏颜色、且未进入手动测试模式时，灯环显示网络状态
  if (!wifiConnected && ledTestMode == LED_OFF) {
    if (wifiApMode) {
      // 配网 AP 模式：蓝色闪烁，提示“请连到 SwitchA-Setup 配网”
      static bool toggle = false;
      static unsigned long lastBlink = 0;
      if (now - lastBlink >= 500) {
        lastBlink = now;
        toggle = !toggle;
      }
      fill_solid(leds, NUM_LEDS, toggle ? CRGB::Blue : CRGB::Black);
    } else {
      // 没有连上 WiFi 且也不在配网 AP：红色呼吸
      uint8_t v = sin8(now / 4);
      fill_solid(leds, NUM_LEDS, CHSV(HUE_RED, 255, v));
    }
    FastLED.show();
    return;
  }

  switch (ledTestMode) {
    case LED_OFF:
      FastLED.clear();
      break;
    case LED_ALL_WHITE:
      fill_solid(leds, NUM_LEDS, CRGB::White);
      break;
    case LED_RAINBOW:
      fill_rainbow(leds, NUM_LEDS, ledRainbowHue, 7);
      ledRainbowHue = (ledRainbowHue + 2) % 256;
      break;
    case LED_ONE_BY_ONE:
      FastLED.clear();
      leds[ledOneIndex] = CHSV((ledOneIndex * 256 / NUM_LEDS) % 256, 255, 255);
      ledOneIndex = (ledOneIndex + 1) % NUM_LEDS;
      break;
  }
  FastLED.show();
}

void loop() {
  ArduinoOTA.handle();
  if (wifiApMode || wifiConnected) {
    configServer.handleClient();
  }
  updateWhiteLed();
  updateLedTest();

  unsigned long now = millis();
  ensureMdnsReady(now);
  pollHostAlignHealth(now, hostAlignForceRetry);
  hostAlignForceRetry = false;
  // 心跳：每 2 秒发一次 PA，维持主机在线检测
  static unsigned long lastHb = 0;
  if (espNowReady && (now - lastHb) >= 2000) {
    lastHb = now;
    sendCommand("PA");
  }

  // 伴睡模式：20 分钟无按键视为已睡着，自动进入深度睡眠
  if (nightCompanionMode) {
    static unsigned long lastCompanionSleepCheck = 0;
    if (now - lastCompanionSleepCheck >= SLEEP_CHECK_INTERVAL_MS) {
      lastCompanionSleepCheck = now;
      if ((now - lastPressTime) >= COMPANION_SLEEP_AFTER_MS) {
        addLog("伴睡模式 20 分钟无操作，进入深度睡眠");
        nightCompanionMode = false;
        FastLED.clear();
        FastLED.show();
        ledcWrite(0, 0);
        digitalWrite(BOARD_POWER_LED_GPIO, HIGH);
        delay(200);
        while (digitalRead(BUTTON_GPIO) == LOW) delay(50);
        delay(100);
        esp_deep_sleep_enable_gpio_wakeup(1ULL << BUTTON_GPIO, ESP_GPIO_WAKEUP_GPIO_LOW);
        esp_deep_sleep_start();
      }
    }
  }

  // 省电：未开调试、未在模式选择、未开伴睡、且长时间无主机联系且无按键 -> 深度睡眠
  if (!debugMode && !modeSelectActive && !nightCompanionMode) {
    static unsigned long lastSleepCheck = 0;
    if (now - lastSleepCheck >= SLEEP_CHECK_INTERVAL_MS) {
      lastSleepCheck = now;
      unsigned long ack = lastAckTime;
      unsigned long activity = (lastPressTime > ack) ? lastPressTime : ack;
      if (now >= MIN_UPTIME_BEFORE_SLEEP_MS &&
          (now - activity) >= IDLE_TIMEOUT_MS) {
        addLog("进入深度睡眠省电，按按钮可唤醒");
        FastLED.clear();
        FastLED.show();
        ledcWrite(0, 0);
        digitalWrite(BOARD_POWER_LED_GPIO, HIGH);  // 蓝灯灭，省电
        delay(200);
        esp_deep_sleep_enable_gpio_wakeup(1ULL << BUTTON_GPIO, ESP_GPIO_WAKEUP_GPIO_LOW);
        esp_deep_sleep_start();
      }
    }
  }

  bool raw = (digitalRead(BUTTON_GPIO) == LOW);  // 按下为 LOW

  // 软件防抖：只有稳定 DEBOUNCE_STABLE_MS 才认边沿
  if (raw != lastRawButton) {
    lastButtonChangeTime = now;
    lastRawButton = raw;
  }
  if ((now - lastButtonChangeTime) >= DEBOUNCE_STABLE_MS && raw != lastStableButton) {
    lastStableButton = raw;
    if (raw) {
      // 稳定按下：记录按下时间，用于后续算长按/短按
      pressStartTime = now;
    } else {
      // 稳定松开：得到本次按键时长，交给模式识别和 ESP-NOW 发送
      if (pressStartTime != 0) {
        unsigned long duration = now - pressStartTime;
        handlePressEvent(duration, now);
        pressStartTime = 0;
      }
    }
  }

  // 长按 ≥30 秒：进入“模式选择”，灯环在 updateLedTest 里会变黄，再按 1/2/3 下 + 5 秒确认
  if (raw && lastStableButton && pressStartTime != 0 && (now - pressStartTime) >= MODE_SELECT_ENTER_MS) {
    if (!modeSelectActive) {
      modeSelectActive = true;
      modeSelectTapCount = 0;
      modeSelectLastTapTime = now;
    }
  }

  // 模式选择：5 秒无操作则执行或取消
  if (modeSelectActive && (now - modeSelectLastTapTime) >= MODE_SELECT_IDLE_MS) {
    if (modeSelectTapCount == 1) {
      toggleDebugMode();
    } else if (modeSelectTapCount == 2) {
      wifiPrefs.begin("wifi", false);
      wifiPrefs.clear();
      wifiPrefs.end();
      addLog("模式选择：清除 WiFi 并重启进配网");
      delay(300);
      ESP.restart();
      return;
    } else if (modeSelectTapCount == 3) {
      addLog("模式选择：进入深度睡眠");
      FastLED.clear();
      FastLED.show();
      ledcWrite(0, 0);
      digitalWrite(BOARD_POWER_LED_GPIO, HIGH);
      delay(200);
      while (digitalRead(BUTTON_GPIO) == LOW) delay(50);
      delay(100);
      esp_deep_sleep_enable_gpio_wakeup(1ULL << BUTTON_GPIO, ESP_GPIO_WAKEUP_GPIO_LOW);
      esp_deep_sleep_start();
      return;
    } else if (modeSelectTapCount == 4) {
      nightCompanionMode = !nightCompanionMode;
      addLog("夜间伴睡模式: " + String(nightCompanionMode ? "已开启" : "已关闭"));
    }
    modeSelectActive = false;
  }

  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    cmd.toLowerCase();
    if (cmd == "status") {
      Serial.println("\n--- 状态 ---");
      Serial.print("主机名: ");
      Serial.println(hostname);
      Serial.print("按钮 GPIO: ");
      Serial.println(BUTTON_GPIO);
      Serial.print("灯环: ");
      Serial.print(NUM_LEDS);
      Serial.print(" 颗, 亮度 ");
      Serial.println(FastLED.getBrightness());
      Serial.print("灯效: ");
      Serial.println(ledTestMode == LED_OFF ? "关" : ledTestMode == LED_ALL_WHITE ? "全白" : ledTestMode == LED_RAINBOW ? "彩虹" : "逐个");
      Serial.print("ESP-NOW: ");
      Serial.println(espNowReady ? "就绪" : "未就绪");
      if (WiFi.status() == WL_CONNECTED) {
        Serial.print("本机 IP: ");
        Serial.println(WiFi.localIP());
      } else {
        Serial.println("WiFi: 未连接");
      }
      Serial.println("-------------");
    } else if (cmd == "1") {
      ledTestMode = LED_ALL_WHITE;
      Serial.println("灯效: 全部亮白");
    } else if (cmd == "2") {
      ledTestMode = LED_RAINBOW;
      Serial.println("灯效: 彩虹循环");
    } else if (cmd == "3") {
      ledTestMode = LED_ONE_BY_ONE;
      Serial.println("灯效: 逐个点亮");
    } else if (cmd == "4") {
      ledTestMode = LED_OFF;
      Serial.println("灯效: 关灯");
    } else if (cmd.startsWith("brightness ")) {
      int b = cmd.substring(11).toInt();
      if (b >= 0 && b <= 255) {
        FastLED.setBrightness(b);
        Serial.print("亮度: ");
        Serial.println(b);
      } else {
        Serial.println("亮度 0-255");
      }
    } else if (cmd == "help" || cmd == "led") {
      printLedHelp();
    } else if (cmd.length() > 0) {
      sendCommand(cmd.c_str());
    }
  }

  delay(10);
}
