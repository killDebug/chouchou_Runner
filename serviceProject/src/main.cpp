#include <Arduino.h>
#include <FastLED.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_now.h>
#include <ESPmDNS.h>
#include <ArduinoOTA.h>
#include <WebServer.h>
#include <Preferences.h>
#include <Wire.h>
#include <U8g2lib.h>
#include <driver/gpio.h>
#include "game/game_manager.h"

// WS2812B配置
#define LED_PIN 2          // ESP32-C3的GPIO2，可根据实际接线修改
// 规格：5m、约 92 颗/米 → 理论约 460 颗；若实际只接了其中一段请按真实颗数改（例如只焊 72 颗就写 72）
#define NUM_LEDS 460
#define LED_TYPE WS2812B
#define COLOR_ORDER GRB    // WS2812B通常是GRB顺序
#define BRIGHTNESS 50      // 亮度（0-255），建议从50开始测试

// WiFi 配置（用于 OTA）：优先使用 NVS 保存的 SSID/密码，没有或连不上则开 AP 配网
const char* defaultSsid     = "CMCC-tpXT";
const char* defaultPassword = "rw6s6we7";
const char* hostname = "ESP32-C3-LED";

// ESP-NOW 长距离模式开关：三端（主机+A+B）需同时开启/关闭；LR 提升约 10dB 链路预算
#define ENABLE_ESPNOW_LR 1

Preferences wifiPrefs;
Preferences hostPrefs;
WebServer configServer(80);
String wifiSsid;
String wifiPassword;
bool wifiHasConfig   = false;
bool wifiConnected   = false;
bool wifiApMode      = false;
bool httpServerStarted = false;

// ESP-NOW 接收（无线开关发来的命令，低延迟、无需路由器）
#define ESP_NOW_CMD_MAX 32
static char espNowCmdBuf[ESP_NOW_CMD_MAX];
static uint8_t espNowCmdMac[6] = {0};  // 与缓冲内容对应的发送方 MAC（用于按键重发去重）
static volatile bool espNowCmdPending = false;
static volatile uint32_t espNowRecvCount = 0;  // 回调里收到的包数，用于判断主机是否真的收到（状态页显示）
static uint8_t lastSenderMac[6] = {0};  // 最近一次发送者的 MAC，用于回传 ACK
bool espNowDebug = false;               // 串口输入 espnow 开启后，收到的 ESP-NOW 包会打印 raw len+hex，用于验证通讯
uint8_t espNowChannel = 0;              // 主机添加广播 peer 时用的信道，状态页显示便于与 A/B 对比
const bool ENABLE_OFFLINE_MODE = true;  // 离线模式开关：无路由器时在 AP 模式也启用 ESP-NOW
wifi_interface_t espNowIfidx = WIFI_IF_STA;
uint8_t espNowPeerChannel = 0;          // STA=0(当前信道)，AP=固定 AP 信道(1)
// 主机硬件按键：GPIO7 按住 2～5s 松开切换 OLED 调试，满 5s 强制离线重启；GPIO8/9 调速
const bool ENABLE_FORCE_OFFLINE_SWITCH = true;
const int FORCE_OFFLINE_SWITCH_GPIO = 7;
const int GAME_SPEED_UP_GPIO = 8;
const int GAME_SPEED_DOWN_GPIO = 9;
const unsigned long FORCE_OFFLINE_HOLD_MS = 5000UL;
/** GPIO7 按住 2s～5s 内松开：切换 OLED 调试（与满 5s 强制离线区分） */
const unsigned long OLED_DEBUG_HOLD_MIN_MS = 2000UL;
/** GPIO7 三连击（短按）：开关「详细串口/日志」（已识别/回传/收包等）；默认关，仅保留 A/B 按键日志 */
const unsigned long GPIO7_TAP_WINDOW_MS = 900UL;
const unsigned long GPIO7_SHORT_PRESS_MAX_MS = 420UL;
// 电脑出点间隔（毫秒）：由速度档位 1～30 映射，30=最快，1=最慢
const int GAME_SPAWN_INTERVAL_MIN_MS = 400;
const int GAME_SPAWN_INTERVAL_MAX_MS = 5000;
bool forceOfflineBySwitch = false;  // 强制离线模式（NVS 持久化）

#define GAME_SPEED_LEVEL_MIN       1
#define GAME_SPEED_LEVEL_MAX       30
#define GAME_SPEED_LEVEL_DEFAULT   15
/** 速度 1=最慢 … 30=最快 → 出点间隔 ms */
static int intervalMsFromSpeedLevel(int level) {
  if (level < GAME_SPEED_LEVEL_MIN) level = GAME_SPEED_LEVEL_MIN;
  if (level > GAME_SPEED_LEVEL_MAX) level = GAME_SPEED_LEVEL_MAX;
  return (int)map(level, GAME_SPEED_LEVEL_MIN, GAME_SPEED_LEVEL_MAX, GAME_SPAWN_INTERVAL_MAX_MS, GAME_SPAWN_INTERVAL_MIN_MS);
}

// ---------- OLED：GME12864-49 0.96" 128×64 I2C（四线：VCC/GND + SDA/SCL）----------
// 接线：VDD→5V、GND→GND；SCL→GPIO20、SDA→GPIO10（模块丝印常为 SCK=SCL）
#define OLED_ENABLE       1
#define OLED_SDA          10
#define OLED_SCL          20
#define OLED_I2C_ADDR     0x3C   // 白屏常见 0x3C；若全黑可试 0x3D
#define OLED_SCREEN_W     128
#define OLED_SCREEN_H     64
#define OLED_RESET        -1

// ---------- 压电蜂鸣器：GPIO21（血糖仪拆机件多为压电片，需方波/PWM 驱动，直流常亮不响）----------
#define BUZZER_ENABLE       1
#define BUZZER_PIN          21

#if OLED_ENABLE
// 全缓冲 + 文泉驿 12px 中文，避免 Adafruit 默认字库中文乱码
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE, OLED_SCL, OLED_SDA);
bool oledOk = false;
#endif

void onEspNowRecv(const uint8_t* mac, const uint8_t* data, int len);

static bool espNowPayloadIsSwitchA(const uint8_t* data, int len) {
  if (!data || len < 1) return false;
  if (len == 1) return data[0] == 'A' || data[0] == 'a';
  if (len == 2) {
    char c0 = (char)data[0];
    char c1 = (char)data[1];
    return (c0 == 'P' || c0 == 'p') && (c1 == 'A' || c1 == 'a');
  }
  return false;
}

static bool espNowPayloadIsSwitchB(const uint8_t* data, int len) {
  if (!data || len < 1) return false;
  if (len == 1) return data[0] == 'B' || data[0] == 'b';
  if (len == 2) {
    char c0 = (char)data[0];
    char c1 = (char)data[1];
    return (c0 == 'P' || c0 == 'p') && (c1 == 'B' || c1 == 'b');
  }
  return false;
}

// 连接状态：搜索 A/B（蓝呼吸）/ 双开关就绪（彩色流水，仅 A 可开始游戏）
enum ConnState { CONN_WAITING_A, CONN_READY };
ConnState connectionState = CONN_WAITING_A;
/** 双端首次都发现后置 true；短丢包不再回到搜索，除非双端长期同时离线 */
bool connReadyLatched = false;
static unsigned long bothSwitchesDeadSince = 0;
/** 在 ESP-NOW 回调里立即刷新，避免 loop 忙于灯带时漏记心跳 */
volatile unsigned long lastReceivedFromA = 0;
volatile unsigned long lastReceivedFromB = 0;
/** 开关每 2s 发 PA/PB；35s 无包才判单端离线（容忍对战时射频拥塞丢包） */
#define SWITCH_LIVENESS_MS 35000UL
/** 双端同时离线满 90s 才清除配对并回到搜索（单端掉线可自动恢复） */
#define SWITCH_BOTH_DEAD_RESET_MS 90000UL
#define ACK_MSG "ACK_A"
#define ACK_B_MSG "ACK_B"
#define RDY_MSG "RDY"                 // 主机发 B：显示“就绪”彩色流水灯
#define GOF_MSG "GOF"
#define DUP_WINDOW_MS 250UL           // 同一发送方同内容在该窗口内只执行一次（按键三连发去重）
uint8_t switchAMac[6] = {0};
bool switchAMacValid = false;
uint8_t switchBMac[6] = {0};
bool switchBMacValid = false;
unsigned long lastAckToASent = 0;
unsigned long lastRdyToBSent = 0;
#define ACK_A_SEND_INTERVAL_MS 3000   // 仅 IDLE 就绪态定期发 ACK_A/RDY，避免对战时 ESP-NOW 刷屏
#define RDY_B_SEND_INTERVAL_MS 3000

static bool switchAAlive(unsigned long now) {
  if (!switchAMacValid || lastReceivedFromA == 0) return false;
  return (unsigned long)(now - lastReceivedFromA) <= SWITCH_LIVENESS_MS;
}

static bool switchBAlive(unsigned long now) {
  if (!switchBMacValid || lastReceivedFromB == 0) return false;
  return (unsigned long)(now - lastReceivedFromB) <= SWITCH_LIVENESS_MS;
}

// 创建LED数组
CRGB leds[NUM_LEDS];

// 测试模式枚举（保留：用于串口调试；游戏逻辑使用单独状态机）
enum TestMode {
  TEST_ALL_ON,      // 全部点亮
  TEST_ONE_BY_ONE,  // 逐个点亮
  TEST_GAME         // 游戏模式
};

TestMode currentMode = TEST_GAME;
unsigned long lastUpdate = 0;
int currentLed = 0;
String serialInput = "";

const int moveIntervalMs = 50;
// 游戏速度档位：1=最慢，30=最快，默认 15；GPIO8 +1，GPIO9 -1，每次 ±1
int gameSpeedLevel = GAME_SPEED_LEVEL_DEFAULT;
int computerSpawnIntervalMs = intervalMsFromSpeedLevel(GAME_SPEED_LEVEL_DEFAULT);  // 与 gameSpeedLevel 同步
/** 调试模式：游戏运行中首行 Spd + 下方日志；未运行时全屏日志。GPIO7 长按 2～5s 松开切换 */
bool oledSerialMirrorMode = false;
/** 详细开关/ESP-NOW 日志：GPIO7 三连击切换；关时串口与网页日志不刷屏，仅保留 A/B 按键相关 */
bool verboseSwitchLog = false;

static const char* hostGameStateLabel(GameState s) {
  switch (s) {
    case GameState::IDLE: return "IDLE";
    case GameState::RUNNING: return "RUN";
    case GameState::WIN: return "WIN";
    case GameState::LOSE: return "LOSE";
    case GameState::PAUSE: return "PAUSE";
    default: return "?";
  }
}

// 无线开关 A/B：主程序记录最近一次按下的是 A 还是 B（0=无，'A'=开关A，'B'=开关B）
char lastSwitchPressed = 0;

// 网页版串口日志（/status 页可查看，无需接 USB）
#define LOG_MAX_LINES  40
#define LOG_MAX_LEN    80
String logBuf[LOG_MAX_LINES];
int logWriteIdx = 0;
int logLineCount = 0;
#if OLED_ENABLE
/** 防止 addLog→refreshOled→(异步)addLog 重入刷屏/栈溢出 */
static volatile bool s_inOledRefresh = false;
#endif

// 函数前向声明
void loadWifiConfig();
void startConfigAP();
void setupWiFi();
void setupOTA();
void handleConfigRoot();
void handleConfigSave();
void handleStatusPage();
void handleLogPage();
void handleEspNowInfo();
void addLog(const String& msg);
void addLogVerbose(const String& msg);
void printCurrentMode();
void handleSerialInput();
void processCommand(String cmd);
void testAllOn();
void testOneByOne();
void testGame();
void testWaitingForA();  // 搜索中：蓝呼吸
void testReadyStrip();   // 双开关就绪：彩色流水灯
void loadHostConfig();
void saveForceOfflineConfig(bool enabled);
void sendToSwitchA(const uint8_t* data, size_t len);
void sendToSwitchB(const uint8_t* data, size_t len);
void sendColToBothSwitches();
void sendCopiesTo(const uint8_t* mac, const uint8_t* data, size_t len, int copies);
static void flushDelayedCopies(unsigned long now);
static void bridgeSendA(const uint8_t* data, size_t len);
static void bridgeSendB(const uint8_t* data, size_t len);
void hostResetGame();
void playStartupBeep();
void buzzerSpeedFeedback();
void setupOled();
void refreshOledScreen();
static void syncSpawnIntervalFromSpeed();

/** 压电片：GPIO 最大驱动 + 多段短鸣（体感更响）；勿长时间直流 */
void playStartupBeep() {
#if BUZZER_ENABLE
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);
  noTone(BUZZER_PIN);  // 先释放 LEDC，避免 tone 首包报 ledc_get_duty / LEDC is not initialized
  gpio_set_drive_capability((gpio_num_t)BUZZER_PIN, GPIO_DRIVE_CAP_3);
  // 约 5.2kHz、三段脉冲，每段略长，比单段更易听清
  const int f = 5200;
  const int ms = 380;
  for (int k = 0; k < 3; k++) {
    tone(BUZZER_PIN, f, ms);
    delay(ms + 25);
    noTone(BUZZER_PIN);
    if (k < 2) delay(55);
  }
  addLog("蜂鸣器：启动提示音（5.2kHz 三段，强驱动）");
#endif
}

/** 调速度时短促反馈 */
void buzzerSpeedFeedback() {
#if BUZZER_ENABLE
  noTone(BUZZER_PIN);
  gpio_set_drive_capability((gpio_num_t)BUZZER_PIN, GPIO_DRIVE_CAP_3);
  tone(BUZZER_PIN, 4200, 150);
  delay(165);
  noTone(BUZZER_PIN);
#endif
}

// ---------- 游戏音效：无阻塞序列器（millis 时间轴驱动，避免 delay 阻断灯带刷新） ----------
enum GameSound : uint8_t { SND_NONE = 0, SND_START, SND_SHOOT, SND_COLLISION, SND_BAD, SND_WIN, SND_LOSE };
struct SoundStep {
  uint16_t freq;  // Hz，0 = 静音段
  uint16_t ms;
};
static const SoundStep kSndStart[] = {{523, 80}, {659, 80}, {784, 80}, {1047, 150}};       // C5→E5→G5→C6 上爬
static const SoundStep kSndShoot[] = {{880, 30}, {1318, 40}};                              // A5→E6 短哔
static const SoundStep kSndCollision[] = {{1568, 25}, {1047, 25}, {784, 30}};              // G6→C6→G5 下降
static const SoundStep kSndBad[] = {{196, 100}, {0, 30}, {196, 100}};                      // G3 双响警告
static const SoundStep kSndWin[] = {{784, 120}, {1047, 120}, {1318, 120}, {1568, 300}};    // 大三和弦号角
static const SoundStep kSndLose[] = {{330, 150}, {311, 150}, {294, 150}, {262, 400}};      // 下沉沮丧音阶

static const SoundStep* sndSteps = nullptr;
static uint8_t sndStepCount = 0;
static uint8_t sndStepIdx = 0;
static unsigned long sndStepStartMs = 0;

/** 播放一段音效（新音效直接覆盖当前）；实际发声由 loop 里的 buzzerUpdate() 推进 */
static void playGameSound(GameSound id) {
#if BUZZER_ENABLE
  switch (id) {
    case SND_START: sndSteps = kSndStart; sndStepCount = sizeof(kSndStart) / sizeof(SoundStep); break;
    case SND_SHOOT: sndSteps = kSndShoot; sndStepCount = sizeof(kSndShoot) / sizeof(SoundStep); break;
    case SND_COLLISION: sndSteps = kSndCollision; sndStepCount = sizeof(kSndCollision) / sizeof(SoundStep); break;
    case SND_BAD: sndSteps = kSndBad; sndStepCount = sizeof(kSndBad) / sizeof(SoundStep); break;
    case SND_WIN: sndSteps = kSndWin; sndStepCount = sizeof(kSndWin) / sizeof(SoundStep); break;
    case SND_LOSE: sndSteps = kSndLose; sndStepCount = sizeof(kSndLose) / sizeof(SoundStep); break;
    default: return;
  }
  sndStepIdx = 0;
  sndStepStartMs = millis();
  if (sndSteps[0].freq > 0) tone(BUZZER_PIN, sndSteps[0].freq, sndSteps[0].ms);
  else noTone(BUZZER_PIN);
#else
  (void)id;
#endif
}

/** loop 每轮调用：按毫秒推进音效序列；tone/noTone 均非阻塞 */
static void buzzerUpdate() {
#if BUZZER_ENABLE
  if (!sndSteps || sndStepCount == 0) return;
  if (millis() - sndStepStartMs < sndSteps[sndStepIdx].ms) return;
  sndStepIdx++;
  if (sndStepIdx >= sndStepCount) {
    sndSteps = nullptr;
    sndStepCount = 0;
    noTone(BUZZER_PIN);
    return;
  }
  sndStepStartMs = millis();
  if (sndSteps[sndStepIdx].freq > 0) tone(BUZZER_PIN, sndSteps[sndStepIdx].freq, sndSteps[sndStepIdx].ms);
  else noTone(BUZZER_PIN);
#endif
}

/** GameManager 事件 → 音效（保持非阻塞，勿在此 delay） */
static void onGameEvent(GameEvent ev) {
  switch (ev) {
    case GameEvent::START: playGameSound(SND_START); addLog("音效：开局上爬音阶"); break;
    case GameEvent::SHOOT: playGameSound(SND_SHOOT); break;
    case GameEvent::COLLISION: playGameSound(SND_COLLISION); break;
    case GameEvent::BAD: playGameSound(SND_BAD); addLog("按错/未交替：BAD 警告音"); break;
    case GameEvent::WIN: playGameSound(SND_WIN); break;
    case GameEvent::LOSE: playGameSound(SND_LOSE); break;
  }
}

static void syncSpawnIntervalFromSpeed() {
  computerSpawnIntervalMs = intervalMsFromSpeedLevel(gameSpeedLevel);
  g_game.setComputerSpawnIntervalMs(computerSpawnIntervalMs);
}

void setupOled() {
#if OLED_ENABLE
  // 勿在此先调用 Wire.begin：U8g2 的 u8g2.begin() 内部会 Wire.begin(SDA,SCL)，
  // 重复初始化会触发 “Bus already started in Master Mode” 警告。
  uint8_t addr7 = 0x3C;
  u8g2.setI2CAddress(addr7 << 1);
  if (!u8g2.begin()) {
    addr7 = 0x3D;
    u8g2.setI2CAddress(addr7 << 1);
    if (!u8g2.begin()) {
      Serial.println("OLED: U8g2 SSD1306 失败（查 SDA/SCL、供电、地址 0x3C/0x3D）");
      addLog("OLED 初始化失败");
      oledOk = false;
      return;
    }
  }

  oledOk = true;
  u8g2.setPowerSave(0);
  u8g2.setContrast(255);
  u8g2.enableUTF8Print();
  u8g2.setFont(u8g2_font_wqy12_t_chinese1);
  u8g2.clearBuffer();
  u8g2.drawUTF8(0, 12, "ESP32-C3 LED Host");
  u8g2.drawUTF8(0, 24, hostname);
  u8g2.drawUTF8(0, 36, (String("LEDs=") + String(NUM_LEDS)).c_str());
  u8g2.drawUTF8(0, 48, "Boot...");
  u8g2.sendBuffer();
  Wire.setClock(400000);
  addLog(String("OLED OK U8g2 addr=0x") + String(addr7, HEX));
#endif
}

#if OLED_ENABLE
/** 单行宽度不超过 128 像素（中文约 10～11 字）；异常 UTF-8 时限制循环次数 */
static String oledFitUtf8Width(const String& s) {
  u8g2.setFont(u8g2_font_wqy12_t_chinese1);
  if (u8g2.getUTF8Width(s.c_str()) <= 128) return s;
  String t = s;
  int guard = (int)t.length() + 24;
  while (t.length() > 0 && guard-- > 0) {
    if (u8g2.getUTF8Width(t.c_str()) <= 128) return t;
    int len = t.length();
    if ((unsigned char)t[len - 1] < 0x80) t.remove(len - 1);
    else if (len >= 3 && ((unsigned char)t[len - 3] & 0xF0) == 0xE0) t.remove(len - 3, 3);
    else if (len >= 2 && ((unsigned char)t[len - 2] & 0xE0) == 0xC0) t.remove(len - 2, 2);
    else if (len >= 4 && ((unsigned char)t[len - 4] & 0xF8) == 0xF0) t.remove(len - 4, 4);
    else t.remove(len - 1);
  }
  return "";
}

static void oledDrawLinesUtf8(const String* lines, int n) {
  const int maxLines = 5;
  const int lineH = 12;
  int y = 12;
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_wqy12_t_chinese1);
  int lim = n < maxLines ? n : maxLines;
  for (int i = 0; i < lim; i++) {
    String t = oledFitUtf8Width(lines[i]);
    u8g2.drawUTF8(0, y, t.c_str());
    y += lineH;
    if (y > 64) break;
  }
  u8g2.sendBuffer();
}
#endif

/** 取第 k 条日志（0=最旧 … count-1=最新） */
static String oledGetLogLineK(int k) {
  if (k < 0 || k >= logLineCount) return "";
  int j = (logWriteIdx - logLineCount + k + LOG_MAX_LINES) % LOG_MAX_LINES;
  return logBuf[j];
}

/** 与网页 /status 同字段；OLED 用 U8g2 中文绘制时按像素截断 */
static int oledBuildStatusLines(String* out, int maxOut) {
  int n = 0;
  auto push = [&](const String& s) {
    if (n >= maxOut) return;
    out[n++] = s;
  };
  push(String(hostname));
  push(String("WiFi:") + (wifiConnected ? "STA" : (wifiApMode ? "AP" : "NO")));
  if (wifiConnected) {
    push(String("SSID:") + (WiFi.SSID().length() ? WiFi.SSID() : wifiSsid));
    push(String("IP:") + WiFi.localIP().toString());
    push(String("BSSID:") + WiFi.BSSIDstr());
  } else if (wifiApMode) {
    push(String("AP IP:") + WiFi.softAPIP().toString());
  }
  push(String("AP mode:") + String(wifiApMode ? "yes" : "no"));
  if (forceOfflineBySwitch) push("ForceOff:ON");
  push(currentMode == TEST_ALL_ON ? "Mode:ALL" : currentMode == TEST_ONE_BY_ONE ? "Mode:1BY1" : "Mode:GAME");
  push(connectionState == CONN_WAITING_A ? "Host:WAIT" : "Host:RDY");
  push(String("SwA:") + (switchAMacValid ? "Y" : "N") + " B:" + (switchBMacValid ? "Y" : "N"));
  push(String("NOW ch:") + String((int)espNowChannel));
  push(String("NOW rx:") + String(espNowRecvCount));
  push(String("Key:") + (lastSwitchPressed == 0 ? "-" : lastSwitchPressed == 'A' ? "A" : "B"));
  if (currentMode == TEST_GAME) {
    GameState gs = g_game.state();
    push(String("Game:") + hostGameStateLabel(gs) + " D" + String(g_game.dotCount()) + " Q" + String(g_game.pendingCatchUpCount()) +
         " H" + String(g_game.targetHue()));
    if (gs == GameState::RUNNING) push(String("Next:") + String(g_game.expectedButton()));
  }
  push(String("LED:") + String(NUM_LEDS) + " L" + String(FastLED.getBrightness()));
  push(String("Spd:") + String(gameSpeedLevel) + "/" + String(GAME_SPEED_LEVEL_MAX));
  push(String("Intv:") + String(computerSpawnIntervalMs) + "ms");
  push(String("Dbg:") + String(oledSerialMirrorMode ? "ON" : "OFF"));
  push(String("Vrb:") + String(verboseSwitchLog ? "ON" : "OFF"));
  return n;
}

/** 刷新 OLED：常规=多页滚动状态；调试+运行=首行 Spd+日志；调试+空闲=全屏日志（U8g2 中文） */
void refreshOledScreen() {
#if OLED_ENABLE
  if (!oledOk) return;
  if (s_inOledRefresh) return;
  s_inOledRefresh = true;
  const int OLED_PAGE_LINES = 5;
  const int OLED_RUN_LOG_LINES = 4;

  if (oledSerialMirrorMode) {
    static GameState oledPrevGameState = GameState::IDLE;
    static unsigned long oledRunLogScrollMs = 0;
    static int oledRunLogScroll = 0;
    GameState gsNow = g_game.state();
    if (gsNow == GameState::RUNNING && oledPrevGameState != GameState::RUNNING) oledRunLogScroll = 0;
    oledPrevGameState = gsNow;

    if (gsNow == GameState::RUNNING) {
      String spd = String("Spd ") + gameSpeedLevel + "/" + String(GAME_SPEED_LEVEL_MAX) + " " + computerSpawnIntervalMs + "ms";
      if (millis() - oledRunLogScrollMs >= 2000UL) {
        oledRunLogScrollMs = millis();
        int maxScroll = (logLineCount > OLED_RUN_LOG_LINES) ? (logLineCount - OLED_RUN_LOG_LINES) : 0;
        if (maxScroll > 0) {
          oledRunLogScroll++;
          if (oledRunLogScroll > maxScroll) oledRunLogScroll = 0;
        } else {
          oledRunLogScroll = 0;
        }
      }
      String lines[5];
      lines[0] = spd;
      for (int r = 0; r < OLED_RUN_LOG_LINES; r++) {
        int k = oledRunLogScroll + r;
        lines[1 + r] = (k < logLineCount) ? oledGetLogLineK(k) : "";
      }
      oledDrawLinesUtf8(lines, 5);
    } else {
      if (logLineCount <= 0) {
        String le[2];
        le[0] = "LOG(empty)";
        le[1] = "GPIO7 2~5s Dbg";
        oledDrawLinesUtf8(le, 2);
      } else {
        int n = logLineCount < OLED_PAGE_LINES ? logLineCount : OLED_PAGE_LINES;
        String lines[5];
        for (int i = 0; i < n; i++) {
          int idx = logLineCount - n + i;
          lines[i] = oledGetLogLineK(idx);
        }
        oledDrawLinesUtf8(lines, n);
      }
    }
  } else {
    static String oledStatLines[40];
    static unsigned long oledStatPageFlipMs = 0;
    static uint32_t oledStatPageIdx = 0;
    unsigned long now = millis();
    if (oledStatPageFlipMs == 0) oledStatPageFlipMs = now;
    if (now - oledStatPageFlipMs >= 4000UL) {
      oledStatPageFlipMs = now;
      oledStatPageIdx++;
    }
    int total = oledBuildStatusLines(oledStatLines, 40);
    int numPages = (total + OLED_PAGE_LINES - 1) / OLED_PAGE_LINES;
    if (numPages < 1) numPages = 1;
    uint32_t p = oledStatPageIdx % (uint32_t)numPages;
    String pageLines[5];
    for (int i = 0; i < OLED_PAGE_LINES; i++) {
      int idx = (int)p * OLED_PAGE_LINES + i;
      pageLines[i] = (idx < total) ? oledStatLines[idx] : "";
    }
    oledDrawLinesUtf8(pageLines, OLED_PAGE_LINES);
  }
  s_inOledRefresh = false;
#endif
}

void setup() {
  // 初始化串口 - ESP32-C3使用USB CDC，波特率115200
  Serial.begin(115200);
#if ARDUINO_USB_CDC_ON_BOOT || defined(CONFIG_TINYUSB_CDC_ENABLED)
  Serial.setTxBufferSize(4096);
#endif
  
  // 等待USB CDC枚举完成 - ESP32-C3需要这个时间
  delay(2000);
  
  // 立即输出，确保能看到 - 使用简单的ASCII字符
  Serial.write(0x0A);  // 换行
  Serial.write(0x0A);  // 换行
  Serial.println("========================================");
  Serial.println("=== WS2812B LED TEST ===");
  Serial.println("========================================");
  Serial.print("Start time: ");
  Serial.print(millis());
  Serial.println("ms");
  Serial.println("ESP32-C3 Initializing...");
  Serial.println("默认：搜索开关 A/B（灯带蓝呼吸）；双开关就绪后灯带彩色流水，仅按 A 可开始游戏");
  delay(500);
  
  // 再次输出，确保串口工作（勿 Serial.flush：未开串口监视器时 USB CDC 可能永久阻塞）
  Serial.println("Serial port is working!");
  delay(200);
  addLog("========================================");
  addLog("=== 主机 (LED 主控) 启动 ===");
  addLog("========================================");

  playStartupBeep();
  setupOled();
  
  // 初始化FastLED
  Serial.println("开始初始化FastLED...");
  
  FastLED.addLeds<LED_TYPE, LED_PIN, COLOR_ORDER>(leds, NUM_LEDS);
  FastLED.setBrightness(BRIGHTNESS);
  
  // 清空所有LED
  FastLED.clear();
  FastLED.show();
  g_game.configure(NUM_LEDS, computerSpawnIntervalMs, moveIntervalMs);
  g_game.setEventCallback(onGameEvent);  // 游戏事件 → 蜂鸣器音效
  pinMode(FORCE_OFFLINE_SWITCH_GPIO, INPUT_PULLUP);
  pinMode(GAME_SPEED_UP_GPIO, INPUT_PULLUP);
  pinMode(GAME_SPEED_DOWN_GPIO, INPUT_PULLUP);
  loadHostConfig();
  
  Serial.println("LED初始化完成！");
  addLog("LED 初始化完成，数量: " + String(NUM_LEDS));
  Serial.print("LED数量: ");
  Serial.println(NUM_LEDS);
  
  // 暂时禁用WiFi和OTA（用于测试）
  // 初始化WiFi和OTA
  setupWiFi();
  setupOTA();
  bool canStartEspNow = wifiConnected || (ENABLE_OFFLINE_MODE && wifiApMode);
  if (canStartEspNow) {
    delay(800);  // WiFi 稳定后再初始化 ESP-NOW
    espNowIfidx = wifiConnected ? WIFI_IF_STA : WIFI_IF_AP;
    espNowPeerChannel = wifiConnected ? 0 : 1;
    // 提升 ESP-NOW 链路质量（解决“稍远/轻微遮挡就丢包”）：
    // 1) 关 STA 省电——modem sleep 会让 ESP-NOW 收包大量丢失；2) 发射功率拉满；
    // 3) 可选 802.11 LR 长距离协议（三端必须同时开启；与路由器 STA 可共存，若异常改为 0）
    esp_wifi_set_ps(WIFI_PS_NONE);
    esp_wifi_set_max_tx_power(84);
#if ENABLE_ESPNOW_LR
    esp_wifi_set_protocol(espNowIfidx,
                          (uint8_t)(WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N | WIFI_PROTOCOL_LR));
    addLog("ESP-NOW：已关省电+满功率+LR 长距离模式");
#else
    addLog("ESP-NOW：已关省电+满功率");
#endif
    if (esp_now_init() == ESP_OK) {
      // 先添加广播 peer 再注册回调。STA 用 channel=0(当前信道)；离线 AP 用固定 channel=1
      static const uint8_t broadcastMac[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
      esp_now_peer_info_t broadcastPeer = {};
      memcpy(broadcastPeer.peer_addr, broadcastMac, 6);
      broadcastPeer.channel = espNowPeerChannel;
      broadcastPeer.ifidx = espNowIfidx;
      broadcastPeer.encrypt = false;
      if (esp_now_add_peer(&broadcastPeer) != ESP_OK) {
        Serial.println("ESP-NOW 添加广播 peer 失败");
        addLog("ESP-NOW 添加广播 peer 失败");
      } else {
        if (wifiConnected) {
          Serial.println("ESP-NOW 已添加广播 peer（channel=0 当前信道）");
          addLog("ESP-NOW 已添加广播 peer ch=0(当前)");
        } else {
          Serial.println("ESP-NOW 已添加广播 peer（离线 AP ch=1）");
          addLog("ESP-NOW 离线模式已开启（AP ch=1）");
        }
      }
      wifi_second_chan_t second = WIFI_SECOND_CHAN_NONE;
      espNowChannel = 0;
      if (esp_wifi_get_channel(&espNowChannel, &second) == ESP_OK)
        addLog("WiFi 当前信道: " + String((int)espNowChannel));
      else
        espNowChannel = 0;
      esp_now_register_recv_cb(onEspNowRecv);
      Serial.println("\nESP-NOW 已开启，无线开关可发消息控制（同串口命令 1/2/3）");
      addLog(wifiConnected ? "ESP-NOW 已开启（在线模式）" : "ESP-NOW 已开启（离线模式）");
    } else {
      Serial.println("\nESP-NOW 初始化失败");
      addLog("ESP-NOW 初始化失败");
    }
  } else {
    addLog("未连 WiFi（当前 AP 或未配网），未初始化 ESP-NOW，无法收开关 A/B");
  }
  addLog("程序已启动");
  Serial.println("\n=== 串口命令 ===");
  Serial.println("输入 '1' - 全部点亮模式");
  Serial.println("输入 '2' - 逐个点亮模式");
  Serial.println("输入 '3' - 游戏模式（从A点向B点移动）");
  Serial.println("无线开关A 发 A、开关B 发 B，主程序可区分；关键消息三连发+主机去重，COL 同步轮次与颜色");
  Serial.println("输入 'brightness <0-255>' - 设置亮度（例如: brightness 100）");
  Serial.println("输入 'status' - 查看当前状态");
  Serial.println("输入 'debug on/off/debug' - OLED 调试；GPIO7 长按2~5s 同效");
  Serial.println("输入 'verbose on/off' - 详细开关日志；GPIO7 连按3次短按同效");
  Serial.println("输入 'espnow on'|'espnow off' - ESP-NOW 收包 hex 调试");
  Serial.print("\n当前模式: ");
  printCurrentMode();
  Serial.println("\n程序已启动，LED应该已经点亮！");
  refreshOledScreen();
}

void loadWifiConfig() {
  wifiPrefs.begin("wifi", true);
  wifiSsid = wifiPrefs.getString("ssid", "");
  wifiPassword = wifiPrefs.getString("pass", "");
  wifiPrefs.end();
  wifiHasConfig = wifiSsid.length() > 0;
}

void loadHostConfig() {
  hostPrefs.begin("hostcfg", true);
  forceOfflineBySwitch = hostPrefs.getBool("forceoff", false);
  hostPrefs.end();
  if (forceOfflineBySwitch) addLog("强制离线:已启用（GPIO7 长按5s切换）");
}

void saveForceOfflineConfig(bool enabled) {
  hostPrefs.begin("hostcfg", false);
  hostPrefs.putBool("forceoff", enabled);
  hostPrefs.end();
  forceOfflineBySwitch = enabled;
}

void startConfigAP() {
  wifiApMode = true;
  wifiConnected = false;

  WiFi.mode(WIFI_AP);
  const char* apSsid = "Service-Setup";
  const char* apPass = "setup1234";
  WiFi.softAP(apSsid, apPass, 1);
  IPAddress apIP = WiFi.softAPIP();

  Serial.println("进入 WiFi 配网模式 (AP)");
  addLog("进入 WiFi 配网模式 (AP)");
  addLog("AP SSID: " + String(apSsid) + " 密码: " + String(apPass));
  addLog("AP IP: " + apIP.toString() + " -> 浏览器打开配置");

  // 默认首页：与 /status 相同，展示串口日志区；WiFi 表单在 /config
  configServer.on("/", handleStatusPage);
  configServer.on("/config", handleConfigRoot);
  configServer.on("/save", HTTP_POST, handleConfigSave);
  configServer.on("/status", handleStatusPage);
  configServer.on("/log", handleLogPage);
  configServer.on("/espnow-info", handleEspNowInfo);
  if (!httpServerStarted) {
    configServer.begin();
    httpServerStarted = true;
  }
}

void setupWiFi() {
  if (ENABLE_FORCE_OFFLINE_SWITCH && forceOfflineBySwitch) {
    addLog("强制离线模式生效：跳过 STA 连接，直接进入 AP");
    startConfigAP();
    return;
  }
  loadWifiConfig();

  String trySsid = wifiHasConfig ? wifiSsid : String(defaultSsid);
  String tryPass = wifiHasConfig ? wifiPassword : String(defaultPassword);

  if (trySsid.length() == 0) {
    Serial.println("没有任何 WiFi 配置，直接进入配网模式");
    addLog("无 WiFi 配置，直接进入配网模式");
    startConfigAP();
    return;
  }

  Serial.print("\n连接 WiFi: ");
  Serial.println(trySsid);
  addLog("连接 WiFi: " + trySsid);

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
    Serial.println("\nWiFi 连接成功");
    addLog("WiFi 连接成功");
    addLog("本机 IP: " + WiFi.localIP().toString());
    addLog("STA BSSID: " + WiFi.BSSIDstr());
    Serial.print("本机 IP: ");
    Serial.println(WiFi.localIP());

    if (!MDNS.begin(hostname)) {
      Serial.println("mDNS 启动失败");
      addLog("mDNS 启动失败");
    } else {
      Serial.println("mDNS: http://" + String(hostname) + ".local");
      addLog("mDNS: http://" + String(hostname) + ".local");
    }

    if (!wifiHasConfig && trySsid == String(defaultSsid)) {
      wifiPrefs.begin("wifi", false);
      wifiPrefs.putString("ssid", trySsid);
      wifiPrefs.putString("pass", tryPass);
      wifiPrefs.end();
      wifiHasConfig = true;
    }

    configServer.on("/", handleStatusPage);
    configServer.on("/config", handleConfigRoot);
    configServer.on("/status", handleStatusPage);
    configServer.on("/log", handleLogPage);
    configServer.on("/espnow-info", handleEspNowInfo);
    if (!httpServerStarted) {
      configServer.begin();
      httpServerStarted = true;
    }
  } else {
    Serial.println("\nWiFi 连接失败，进入配网 AP 模式");
    addLog("WiFi 连接失败，进入配网 AP 模式");
    startConfigAP();
  }
}

void setupOTA() {
  ArduinoOTA.setHostname(hostname);
  ArduinoOTA.setPassword("12345678"); // OTA密码，建议修改
  
  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH) {
      type = "sketch";
    } else {
      type = "filesystem";
    }
    Serial.println("开始OTA更新: " + type);
    addLog("OTA 开始: " + type);
    // 更新时显示黄色
    fill_solid(leds, NUM_LEDS, CRGB::Yellow);
    FastLED.show();
  });
  
  ArduinoOTA.onEnd([]() {
    Serial.println("\nOTA更新完成！");
    addLog("OTA 更新完成");
    // 更新完成显示绿色
    fill_solid(leds, NUM_LEDS, CRGB::Green);
    FastLED.show();
    delay(2000);
  });
  
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("进度: %u%%\r", (progress / (total / 100)));
    // 显示进度条效果
    int progressLeds = map(progress, 0, total, 0, NUM_LEDS);
    for (int i = 0; i < NUM_LEDS; i++) {
      if (i < progressLeds) {
        leds[i] = CRGB::Blue;
      } else {
        leds[i] = CRGB::Black;
      }
    }
    FastLED.show();
  });
  
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("OTA错误[%u]: ", error);
    addLog("OTA 错误[" + String((unsigned)error) + "]");
    if (error == OTA_AUTH_ERROR) {
      Serial.println("认证失败");
    } else if (error == OTA_BEGIN_ERROR) {
      Serial.println("开始失败");
    } else if (error == OTA_CONNECT_ERROR) {
      Serial.println("连接失败");
    } else if (error == OTA_RECEIVE_ERROR) {
      Serial.println("接收失败");
    } else if (error == OTA_END_ERROR) {
      Serial.println("结束失败");
    }
    // 错误时显示红色
    fill_solid(leds, NUM_LEDS, CRGB::Red);
    FastLED.show();
  });
  
  ArduinoOTA.begin();
  Serial.println("OTA就绪");
  Serial.print("OTA密码: 12345678 (可在代码中修改)\n");
}

void handleConfigRoot() {
  String page = R"(
<!DOCTYPE html>
<html>
  <head>
    <meta charset="utf-8">
    <title>主机 WiFi 配置</title>
    <style>
      body { font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif; padding: 16px; }
      input { padding: 6px 8px; width: 260px; max-width: 100%; }
      button { padding: 6px 18px; margin-top: 12px; }
    </style>
  </head>
  <body>
    <h2>主机 (LED 主控) WiFi 配置</h2>
    <p><a href="/">返回首页（状态与串口日志）</a></p>
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
    <p><a href="/">返回状态与日志</a></p>
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

void addLog(const String& msg) {
  Serial.println(msg);
  String line = msg;
  if (line.length() > (unsigned)LOG_MAX_LEN) line = line.substring(0, LOG_MAX_LEN);
  logBuf[logWriteIdx] = line;
  logWriteIdx = (logWriteIdx + 1) % LOG_MAX_LINES;
  if (logLineCount < LOG_MAX_LINES) logLineCount++;
#if OLED_ENABLE
  if (oledOk && oledSerialMirrorMode && !s_inOledRefresh) {
    refreshOledScreen();
  }
#endif
}

/** 仅 verboseSwitchLog==true 时写入串口+网页日志（已识别/回传/收包等） */
void addLogVerbose(const String& msg) {
  if (!verboseSwitchLog) return;
  addLog(msg);
}

void handleLogPage() {
  String text;
  for (int i = 0; i < logLineCount; i++) {
    int j = (logWriteIdx - logLineCount + i + LOG_MAX_LINES) % LOG_MAX_LINES;
    text += logBuf[j] + "\n";
  }
  configServer.send(200, "text/plain; charset=utf-8", text);
}

void handleEspNowInfo() {
  wifi_second_chan_t second = WIFI_SECOND_CHAN_NONE;
  uint8_t ch = 0;
  if (esp_wifi_get_channel(&ch, &second) != ESP_OK) ch = 0;
  String json = "{\"channel\":" + String((int)ch) +
                ",\"bssid\":\"" + WiFi.BSSIDstr() +
                "\",\"ssid\":\"" + WiFi.SSID() +
                "\",\"ip\":\"" + WiFi.localIP().toString() + "\"}";
  configServer.send(200, "application/json; charset=utf-8", json);
}

void handleStatusPage() {
  String ipSta = WiFi.isConnected() ? WiFi.localIP().toString() : String("-");
  String ipAp  = WiFi.softAPIP().toString();

  const char* modeStr = currentMode == TEST_ALL_ON ? "全部点亮" : currentMode == TEST_ONE_BY_ONE ? "逐个点亮" : "游戏模式";
  const char* connStr = connectionState == CONN_WAITING_A ? "搜索 A/B（蓝呼吸）" : "双开关就绪（彩色流水）";

  String page = "<!DOCTYPE html><html><head><meta charset='utf-8'><title>主机状态</title>";
  page += "<style>body{font-family:-apple-system,sans-serif;padding:16px;} code{background:#f5f5f5;padding:2px 4px;} ";
  page += ".log{background:#1e1e1e;color:#d4d4d4;padding:8px;font-family:monospace;font-size:12px;white-space:pre-wrap;word-break:break-all;max-height:400px;overflow-y:auto;}</style></head><body>";
  page += "<h2>主机 (LED 主控) 运行状态</h2>";
  page += "<p><small>默认首页（<code>/</code>）即本页，含下方串口日志；串口 <code>debug on/off</code>；<b>GPIO7</b> 按住 <b>2～5 秒</b>松开切换 OLED 调试（运行中：首行速度+下方日志；未运行：全屏日志）；<b>按住满 5 秒</b>强制离线并重启。常规屏：多页滚动状态（约 4 秒翻页）。</small></p>";
  page += "<p><small><b>详细日志（默认关）</b>：串口/网页里「已识别」「已回传」「[ESP-NOW] 收到」等会刷屏；<b>GPIO7 连续短按 3 次</b>（约 0.9s 内）切换，关闭后仅保留「收到开关A/B 按下」等按键日志。也可用串口 <code>verbose on</code>/<code>verbose off</code>。</small></p>";
  page += "<p><b>主机名</b>：<code>" + String(hostname) + "</code></p>";
  page += "<p><b>WiFi 已连接</b>：" + String(wifiConnected ? "是" : "否") + "</p>";
  page += "<p><b>当前 SSID</b>：<code>" + (wifiSsid.length() ? wifiSsid : String("-")) + "</code></p>";
  page += "<p><b>STA IP</b>：<code>" + ipSta + "</code></p>";
  page += "<p><b>STA BSSID</b>：<code>" + WiFi.BSSIDstr() + "</code></p>";
  page += "<p><b>通信模式</b>：" + String(wifiConnected ? "在线模式（STA）" : (ENABLE_OFFLINE_MODE ? "离线模式（AP）" : "未启用")) + (forceOfflineBySwitch ? "（强制离线已启用）" : "") + "</p>";
  page += "<p><b>AP 模式</b>：" + String(wifiApMode ? "是" : "否") + "，AP IP：<code>" + ipAp + "</code></p>";
  page += "<p><b>当前模式</b>：" + String(modeStr) + "</p>";
  page += "<p><b>主机状态</b>：" + String(connStr) + "</p>";
  page += "<p><b>开关 A</b>：" + String(switchAMacValid ? "已发现" : "未发现") + "，<b>开关 B</b>：" + String(switchBMacValid ? "已发现" : "未发现") + "</p>";
  page += "<p><b>ESP-NOW 信道</b>：" + String((int)espNowChannel) + "（当前 STA 信道；主机与 A/B 均用 channel=0，同 WiFi 即一致可互通）</p>";
  page += "<p><b>ESP-NOW 收包数</b>：" + String((uint32_t)espNowRecvCount) + "（按 A/B 后刷新，若不变说明主机未收到包）</p>";
  page += "<p><b>最近按下</b>：" + String(lastSwitchPressed == 0 ? "无" : lastSwitchPressed == 'A' ? "开关A" : "开关B") + "</p>";
  if (currentMode == TEST_GAME) {
    GameState gs = g_game.state();
    page += "<p><b>游戏状态</b>：" + String(hostGameStateLabel(gs)) + "，<b>Dot</b>：" + String(g_game.dotCount()) +
            "，<b>波次</b>：" + String(g_game.colorWaveIndex()) +
            "，<b>配色</b>：即时生成" +
            "，<b>目标色相</b>：" + String(g_game.targetHue()) +
            "，<b>目标色</b>：" + String(g_game.targetColorIndex()) +
            "，<b>积压</b>：" + String(g_game.pendingCatchUpCount());
    if (gs == GameState::RUNNING) {
      page += "，<b>下一次应按</b>：" + String(g_game.expectedButton());
    }
    page += "</p>";
  }
  page += "<p><b>LED 数量</b>：" + String(NUM_LEDS) + "，亮度 " + String(FastLED.getBrightness()) + "</p>";
  page += "<p><b>游戏速度</b>：" + String(gameSpeedLevel) + "/" + String(GAME_SPEED_LEVEL_MAX) + "（30=最快，1=最慢，默认15；GPIO8 +1 / GPIO9 -1）</p>";
  page += "<p><b>OLED 调试</b>：" + String(oledSerialMirrorMode ? "开（屏仅日志）" : "关（屏常规）") + "</p>";
  page += "<p><b>详细开关日志</b>：" + String(verboseSwitchLog ? "开（已识别/回传/[ESP-NOW]收包等会显示）" : "关（默认，仅按键日志）") + "</p>";
  page += "<p><b>电脑出点间隔</b>：" + String(computerSpawnIntervalMs) + " ms（由速度档位映射）</p>";
  if (wifiApMode && !ENABLE_OFFLINE_MODE) {
    page += "<p><b style='color:red'>当前为 AP 模式，未启用离线 ESP-NOW，收不到开关的包。请配网后连同一 WiFi（STA）再试。</b></p>";
  }
  page += "<p><b>配网</b>：连 AP <code>Service-Setup</code> 后打开 <a href='http://192.168.4.1/'>首页/状态</a> 或 <a href='http://192.168.4.1/config'>WiFi 配置</a>；STA 下 <a href='http://" + String(hostname) + ".local/'>http://" + String(hostname) + ".local/</a></p>";

  if (logLineCount > 0) {
    page += "<h3>最近运行日志（相当于串口输出）</h3><div id='log' class='log'>";
    for (int i = 0; i < logLineCount; i++) {
      int j = (logWriteIdx - logLineCount + i + LOG_MAX_LINES) % LOG_MAX_LINES;
      page += escapeHtml(logBuf[j]) + "\n";
    }
    page += "</div><p><small>仅保留最近 " + String(LOG_MAX_LINES) + " 条，每 1 秒自动更新。</small></p>";
    page += "<script>setInterval(function(){ fetch('log').then(function(r){ return r.text(); }).then(function(t){ var e=document.getElementById('log'); if(e) e.innerText=t; }); }, 1000);</script>";
  }

  page += "</body></html>";
  configServer.send(200, "text/html", page);
}

// ESP-NOW 接收回调（与 esp_now.h 中 esp_now_recv_cb_t 一致：mac, data, len）
void onEspNowRecv(const uint8_t* mac, const uint8_t* data, int len) {
  espNowRecvCount++;
  if (mac) memcpy(lastSenderMac, mac, 6);
  // 回调里立刻刷新在线时间，不等到 loop（对战时 loop 可能忙于 FastLED）
  if (data && len > 0) {
    unsigned long now = millis();
    if (espNowPayloadIsSwitchA(data, len)) {
      lastReceivedFromA = now;
    } else if (espNowPayloadIsSwitchB(data, len)) {
      lastReceivedFromB = now;
    }
  }
  int n = len < (ESP_NOW_CMD_MAX - 1) ? len : (ESP_NOW_CMD_MAX - 1);
  if (n > 0 && data) {
    memcpy(espNowCmdBuf, data, n);
    espNowCmdBuf[n] = '\0';
    if (mac) memcpy(espNowCmdMac, mac, 6); else memset(espNowCmdMac, 0, 6);
    espNowCmdPending = true;
  }
}

void loop() {
  // 第一次进入loop时输出 - 使用简单ASCII确保输出
  static bool firstLoop = true;
  if (firstLoop) {
    delay(500);  // 等待串口稳定
    Serial.println();
    Serial.println("========================================");
    Serial.println("LOOP STARTED - Program is running!");
    addLog("LOOP 已启动，当前模式: " + String(currentMode == TEST_ALL_ON ? "全部点亮" : currentMode == TEST_ONE_BY_ONE ? "逐个点亮" : "游戏"));
    Serial.print("Time: ");
    Serial.print(millis());
    Serial.println("ms");
    Serial.println("LED should be ON now!");
    Serial.print("Mode: ");
    Serial.println(currentMode == TEST_ALL_ON ? "ALL ON" : "ONE BY ONE");
    Serial.println("========================================");
    firstLoop = false;
  }
  
  // 处理 OTA
  ArduinoOTA.handle();

  // 蜂鸣器音效序列推进（无阻塞）
  buzzerUpdate();

  // 配网/状态页（AP 或 STA 连上时）
  if (wifiApMode || wifiConnected) {
    configServer.handleClient();
  }
  
  // 处理串口输入
  handleSerialInput();
  
  unsigned long currentTime = millis();
  flushDelayedCopies(currentTime);

  // GPIO7：双短按→SET 重置游戏；三连击→详细日志；按住 2s～5s→OLED 调试；满 5s→强制离线重启
  static bool forceRawPrev = false;
  static unsigned long forcePressStart = 0;
  static bool forceHandled = false;
  static unsigned long gpio7TapLastMs = 0;
  static uint8_t gpio7TapCount = 0;
  static unsigned long gpio7SetArmMs = 0;
  bool forceRaw = (digitalRead(FORCE_OFFLINE_SWITCH_GPIO) == LOW);
  if (forceRaw && !forceRawPrev) {
    forcePressStart = currentTime;
    forceHandled = false;
  }
  if (forceRaw && !forceHandled && (currentTime - forcePressStart) >= FORCE_OFFLINE_HOLD_MS) {
    forceHandled = true;
    bool next = !forceOfflineBySwitch;
    saveForceOfflineConfig(next);
    addLog(String("GPIO7 长按5s：强制离线已") + (next ? "启用" : "关闭") + "，即将重启");
    delay(200);
    ESP.restart();
  }
  if (!forceRaw && forceRawPrev) {
    unsigned long dur = currentTime - forcePressStart;
    if (dur >= 25 && dur < GPIO7_SHORT_PRESS_MAX_MS && !forceHandled) {
      if (currentTime - gpio7TapLastMs > GPIO7_TAP_WINDOW_MS) gpio7TapCount = 0;
      gpio7TapCount++;
      gpio7TapLastMs = currentTime;
      if (gpio7TapCount >= 3) {
        gpio7SetArmMs = 0;
        gpio7TapCount = 0;
        verboseSwitchLog = !verboseSwitchLog;
        addLog(String("详细日志(ESP-NOW/识别/回传):") + (verboseSwitchLog ? "开" : "关"));
#if BUZZER_ENABLE
        buzzerSpeedFeedback();
#endif
#if OLED_ENABLE
        if (oledOk) refreshOledScreen();
#endif
      } else if (gpio7TapCount == 2) {
        gpio7SetArmMs = currentTime;
      }
    } else if (dur >= OLED_DEBUG_HOLD_MIN_MS && dur < FORCE_OFFLINE_HOLD_MS && !forceHandled) {
      oledSerialMirrorMode = !oledSerialMirrorMode;
      addLog(String("GPIO7 长按2~5s：OLED调试 ") + (oledSerialMirrorMode ? "开" : "关"));
#if BUZZER_ENABLE
      buzzerSpeedFeedback();
#endif
#if OLED_ENABLE
      if (oledOk) refreshOledScreen();
#endif
    }
    forcePressStart = 0;
  }
  forceRawPrev = forceRaw;

  // GPIO7 双短按 SET：0.9s 内无第 3 击则重置游戏（第 3 击留给详细日志）
  if (gpio7SetArmMs != 0 && gpio7TapCount == 2 && (currentTime - gpio7SetArmMs) >= GPIO7_TAP_WINDOW_MS) {
    hostResetGame();
    gpio7TapCount = 0;
    gpio7SetArmMs = 0;
  }

  // GPIO8/9 调整游戏速度档位 1～30（每次 ±1；30 最快，1 最慢；默认 15）
  static bool upPrev = false;
  static bool downPrev = false;
  bool upRaw = (digitalRead(GAME_SPEED_UP_GPIO) == LOW);
  bool downRaw = (digitalRead(GAME_SPEED_DOWN_GPIO) == LOW);
  if (upRaw && !upPrev) {
    if (gameSpeedLevel < GAME_SPEED_LEVEL_MAX) {
      gameSpeedLevel++;
      syncSpawnIntervalFromSpeed();
      addLog("GPIO8 加速：速度=" + String(gameSpeedLevel) + "/" + String(GAME_SPEED_LEVEL_MAX) + "，出点间隔=" + String(computerSpawnIntervalMs) + "ms");
#if OLED_ENABLE
      refreshOledScreen();
#endif
      buzzerSpeedFeedback();
    }
  }
  if (downRaw && !downPrev) {
    if (gameSpeedLevel > GAME_SPEED_LEVEL_MIN) {
      gameSpeedLevel--;
      syncSpawnIntervalFromSpeed();
      addLog("GPIO9 减速：速度=" + String(gameSpeedLevel) + "/" + String(GAME_SPEED_LEVEL_MAX) + "，出点间隔=" + String(computerSpawnIntervalMs) + "ms");
#if OLED_ENABLE
      refreshOledScreen();
#endif
      buzzerSpeedFeedback();
    }
  }
  upPrev = upRaw;
  downPrev = downRaw;
  
  // 处理 ESP-NOW 收到的命令（在回调里只写缓冲，这里统一执行）
  if (espNowCmdPending) {
    espNowCmdPending = false;
    String cmd(espNowCmdBuf);
    cmd.trim();
    if (cmd.length() > 0) {
      // 按键/心跳三连发去重：同一发送方+同一内容在 DUP_WINDOW_MS 内只执行一次
      // （注意不能放在回调里：心跳/在线时间已在回调逐包刷新，这里只去重命令执行）
      static uint8_t lastDupMac[6] = {0};
      static char lastDupCmd[ESP_NOW_CMD_MAX] = {0};
      static unsigned long lastDupMs = 0;
      if (memcmp(espNowCmdMac, lastDupMac, 6) == 0 && strncmp(cmd.c_str(), lastDupCmd, ESP_NOW_CMD_MAX) == 0 &&
          (currentTime - lastDupMs) < DUP_WINDOW_MS) {
        // 只跳过这条重复命令，绝不能 return 整个 loop（否则 tick/刷灯被掐掉，灯带会停一拍再整条刷新）
      } else {
      strlcpy(lastDupCmd, cmd.c_str(), ESP_NOW_CMD_MAX);
      memcpy(lastDupMac, espNowCmdMac, 6);
      lastDupMs = currentTime;
      if (espNowDebug) {
        Serial.print("[ESP-NOW raw] len=");
        Serial.print((int)cmd.length());
        Serial.print(" hex=");
        for (size_t i = 0; i < (size_t)cmd.length(); i++) {
          Serial.print(" ");
          Serial.print((uint8_t)cmd[i], HEX);
        }
        Serial.println();
      }
      if (verboseSwitchLog) {
        Serial.print("[ESP-NOW] 收到: ");
        Serial.println(cmd);
      }
      // 心跳包 PA/PB 不写入网页日志；详细收包仅 verboseSwitchLog 开时记录
      {
        String cmdLower = cmd;
        cmdLower.toLowerCase();
        if (cmdLower != "pa" && cmdLower != "pb") {
          addLogVerbose("[ESP-NOW] 收到: " + cmd + " len=" + String(cmd.length()));
        }
      }
      // 识别 A/B + 心跳：A/PA、B/PB；RST 为重开命令
      bool isA = cmd.equalsIgnoreCase("a") || cmd.equalsIgnoreCase("pa");
      bool isB = cmd.equalsIgnoreCase("b") || cmd.equalsIgnoreCase("pb");
      bool isAHeartbeatOnly = cmd.equalsIgnoreCase("pa");
      bool isBHeartbeatOnly = cmd.equalsIgnoreCase("pb");
      if (isA) {
        lastReceivedFromA = currentTime;
        memcpy(switchAMac, lastSenderMac, 6);
        switchAMacValid = true;
        esp_now_peer_info_t peer = {};
        memcpy(peer.peer_addr, lastSenderMac, 6);
        peer.channel = espNowPeerChannel;
        peer.ifidx = espNowIfidx;
        peer.encrypt = false;
        esp_err_t addRet = esp_now_add_peer(&peer);
        // 心跳不回 ACK；对战中也不回（TA/COL 已够用），避免按一下就堵 25ms
        if (!isAHeartbeatOnly && g_game.state() != GameState::RUNNING &&
            (addRet == ESP_OK || addRet == ESP_ERR_ESPNOW_EXIST)) {
          sendCopiesTo(lastSenderMac, (const uint8_t*)ACK_MSG, (sizeof(ACK_MSG) - 1), 2);
          if (verboseSwitchLog) Serial.println("已回传 ACK_A，开关A 灯环将显示上线");
          addLogVerbose("已回传 ACK_A，开关A 上线");
        }
        if (switchAMacValid && switchBMacValid) {
          connectionState = CONN_READY;
          connReadyLatched = true;
        }
      } else if (isB) {
        lastReceivedFromB = currentTime;
        memcpy(switchBMac, lastSenderMac, 6);
        switchBMacValid = true;
        addLogVerbose("已识别为 B，设置开关 B 为已发现");
        esp_now_peer_info_t peer = {};
        memcpy(peer.peer_addr, lastSenderMac, 6);
        peer.channel = espNowPeerChannel;
        peer.ifidx = espNowIfidx;
        peer.encrypt = false;
        esp_err_t addRetB = esp_now_add_peer(&peer);
        if (!isBHeartbeatOnly && g_game.state() != GameState::RUNNING &&
            (addRetB == ESP_OK || addRetB == ESP_ERR_ESPNOW_EXIST)) {
          sendCopiesTo(lastSenderMac, (const uint8_t*)ACK_B_MSG, (sizeof(ACK_B_MSG) - 1), 2);
          if (verboseSwitchLog) Serial.println("已回传 ACK_B，开关B 灯环将显示上线");
          addLogVerbose("已回传 ACK_B，开关B 上线");
        }
        if (switchAMacValid && switchBMacValid) {
          connectionState = CONN_READY;
          connReadyLatched = true;
        }
      }
      // 最后再处理命令（依赖 A/B 在线状态时更准确）
      processCommand(cmd);
      }
    }
  }

  // READY 且游戏 IDLE 时定期发 ACK_A / RDY（就绪灯效）；对战中不发，避免占满 ESP-NOW
  if (connectionState == CONN_READY && g_game.state() == GameState::IDLE) {
    if (currentTime - lastAckToASent >= ACK_A_SEND_INTERVAL_MS && switchAMacValid) {
      sendToSwitchA((const uint8_t*)ACK_MSG, sizeof(ACK_MSG) - 1);
      lastAckToASent = currentTime;
    }
    if (currentTime - lastRdyToBSent >= RDY_B_SEND_INTERVAL_MS && switchBMacValid) {
      sendToSwitchB((const uint8_t*)RDY_MSG, 3);
      lastRdyToBSent = currentTime;
    }
  }

  // 连接保活：已配对后保持 READY，单端短丢包只暂停游戏，双端长期同时离线才回搜索
  if (connectionState == CONN_READY || connReadyLatched) {
    bool aAlive = switchAAlive(currentTime);
    bool bAlive = switchBAlive(currentTime);
    GameState gs = g_game.state();

    if (connReadyLatched) {
      connectionState = CONN_READY;
    }

    if (aAlive && bAlive) {
      bothSwitchesDeadSince = 0;
      if (gs == GameState::PAUSE) {
        g_game.resumeFromPause(currentTime);
        addLogVerbose("双开关恢复在线，继续游戏");
      }
    } else {
      if (gs == GameState::RUNNING) {
        g_game.enterPause(currentTime);
        if (verboseSwitchLog) {
          addLogVerbose(String("开关链路抖动，游戏暂停 A=") + (aAlive ? "在线" : "离线") + " B=" +
                        (bAlive ? "在线" : "离线"));
        }
      }

      if (!aAlive && !bAlive) {
        if (bothSwitchesDeadSince == 0) {
          bothSwitchesDeadSince = currentTime;
        } else if ((unsigned long)(currentTime - bothSwitchesDeadSince) >= SWITCH_BOTH_DEAD_RESET_MS) {
          connectionState = CONN_WAITING_A;
          connReadyLatched = false;
          bothSwitchesDeadSince = 0;
          switchAMacValid = false;
          switchBMacValid = false;
          lastReceivedFromA = 0;
          lastReceivedFromB = 0;
          g_game.forceIdle();
          addLog("双开关长期离线，回到搜索 A/B");
        }
      } else {
        bothSwitchesDeadSince = 0;
      }
    }
  }

  // 按键处理里可能排队了重发；取当前时间再 tick/刷灯，避免用循环开头的旧时间戳
  currentTime = millis();

  // COL 颜色同步：游戏状态/轮次/目标色任一变化立即下发；另有 2s 周期刷新兜底抗丢包。
  // 事件驱动不再刷屏（旧版 100ms 轮询已废弃）；开关收到后按 meta 显示“该谁按+按什么色”
  if (connectionState == CONN_READY && currentMode == TEST_GAME) {
    static uint32_t lastColSig = 0xFFFFFFFF;
    static unsigned long lastColSentMs = 0;
    GameState colGs = g_game.state();
    if (colGs == GameState::IDLE || colGs == GameState::RUNNING || colGs == GameState::PAUSE) {
      uint32_t sig = ((uint32_t)(uint8_t)colGs << 24) | ((uint32_t)(uint8_t)g_game.expectedButton() << 16) |
                     ((uint32_t)g_game.targetHue() << 8) | (uint32_t)g_game.targetColorIndex();
      if (sig != lastColSig || (currentTime - lastColSentMs) >= 2000UL) {
        lastColSig = sig;
        lastColSentMs = currentTime;
        sendColToBothSwitches();
      }
    }
  }

  // 显示：搜索中=蓝呼吸，就绪未开始=彩色流水，就绪且游戏已开始=testGame
  if (connectionState == CONN_WAITING_A) {
    testWaitingForA();
  } else {
    if (currentMode == TEST_GAME) {
      g_game.tick(currentTime, connectionState == CONN_READY, true, switchAMacValid, switchBMacValid, bridgeSendA, bridgeSendB);
      testGame();
    } else
      testReadyStrip();
  }

  unsigned long ledInterval = (connectionState != CONN_WAITING_A && currentMode == TEST_GAME) ? 33UL : 100UL;
  if (currentTime - lastUpdate >= ledInterval) {
    FastLED.show();
    lastUpdate = currentTime;
  }

#if OLED_ENABLE
  // OLED 放在刷灯之后：全缓冲 I2C 很慢，对战时拉长间隔，避免整条灯带像被刷新了一下
  static unsigned long lastOledMs = 0;
  unsigned long oledPeriod = oledSerialMirrorMode ? 400UL
                                                  : (g_game.state() == GameState::RUNNING ? 2500UL : 1000UL);
  if (oledOk && (millis() - lastOledMs >= oledPeriod)) {
    lastOledMs = millis();
    refreshOledScreen();
  }
#endif
}

void handleSerialInput() {
  if (Serial.available()) {
    char c = Serial.read();
    
    if (c == '\n' || c == '\r') {
      if (serialInput.length() > 0) {
        processCommand(serialInput);
        serialInput = "";
      }
    } else {
      serialInput += c;
    }
  }
}

void processCommand(String cmd) {
  cmd.trim();
  String raw = cmd;
  cmd.toLowerCase();

  if (cmd == "1") {
    currentMode = TEST_ALL_ON;
    Serial.println("切换到：全部点亮模式");
    printCurrentMode();
  } else if (cmd == "2") {
    currentMode = TEST_ONE_BY_ONE;
    currentLed = 0;
    Serial.println("切换到：逐个点亮模式");
    printCurrentMode();
  } else if (cmd == "3") {
    currentMode = TEST_GAME;
    g_game.forceIdle();
    g_game.syncTurnToIdle(switchAMacValid, switchBMacValid, bridgeSendA, bridgeSendB);
    Serial.println("切换到：游戏模式（Dot 版）");
    printCurrentMode();
  } else if (cmd == "set") {
    hostResetGame();
  } else if (cmd == "rst!") {
    // 开关长按 2s 发来的强制重置：任何状态都接受（对战卡死时的脱困手段）
    hostResetGame();
    addLog("开关 RST!（长按）：游戏已强制重置");
  } else if (cmd == "rst") {
    if (currentMode == TEST_GAME && g_game.allowSwitchDoubleTapReset()) {
      hostResetGame();
      addLog("开关双击 RST：游戏已重置");
    } else {
      addLogVerbose("忽略 RST：对战进行中；结果出来后双击可重置，或主机 GPIO7 双短按 SET");
    }
  } else if (cmd == "a") {
    lastSwitchPressed = 'A';
    if (g_game.state() == GameState::RUNNING) addLogVerbose("收到开关A 按下");
    else addLog("收到开关A 按下");
    g_game.onButtonPress('A', millis(), connectionState == CONN_READY, currentMode == TEST_GAME, switchAMacValid,
                         switchBMacValid, bridgeSendA, bridgeSendB);
  } else if (cmd == "b") {
    lastSwitchPressed = 'B';
    if (g_game.state() == GameState::RUNNING) addLogVerbose("收到开关B 按下");
    else addLog("收到开关B 按下");
    g_game.onButtonPress('B', millis(), connectionState == CONN_READY, currentMode == TEST_GAME, switchAMacValid,
                         switchBMacValid, bridgeSendA, bridgeSendB);
  } else if (cmd == "pa" || cmd == "pb") {
    // 心跳：仅用于在线检测，不触发游戏逻辑
  } else if (cmd.startsWith("brightness ")) {
    int brightness = cmd.substring(11).toInt();
    if (brightness >= 0 && brightness <= 255) {
      FastLED.setBrightness(brightness);
      Serial.print("亮度设置为: ");
      Serial.println(brightness);
    } else {
      Serial.println("亮度值必须在0-255之间");
    }
  } else if (cmd == "espnow" || cmd == "espnow on") {
    espNowDebug = true;
    Serial.println("ESP-NOW 调试已开：收到的包会打印 raw len+hex，用于验证 B 是否发到主机");
  } else if (cmd == "espnow off") {
    espNowDebug = false;
    Serial.println("ESP-NOW 调试已关");
  } else if (cmd == "verbose off") {
    verboseSwitchLog = false;
    Serial.println("详细开关日志：关（仅保留按键 A/B 等）");
    addLog("详细日志(开关/ESP-NOW):关");
#if OLED_ENABLE
    if (oledOk) refreshOledScreen();
#endif
  } else if (cmd == "verbose on") {
    verboseSwitchLog = true;
    Serial.println("详细开关日志：开（已识别/回传/收包等）");
    addLog("详细日志(开关/ESP-NOW):开");
#if OLED_ENABLE
    if (oledOk) refreshOledScreen();
#endif
  } else if (cmd == "verbose") {
    verboseSwitchLog = !verboseSwitchLog;
    Serial.println(verboseSwitchLog ? "详细开关日志：开" : "详细开关日志：关");
    addLog(String("详细日志(开关/ESP-NOW):") + (verboseSwitchLog ? "开" : "关"));
#if OLED_ENABLE
    if (oledOk) refreshOledScreen();
#endif
  } else if (cmd == "debug off") {
    oledSerialMirrorMode = false;
    Serial.println("OLED 调试：关（恢复常规摘要）");
    addLog("OLED 调试模式：关");
#if OLED_ENABLE
    if (oledOk) refreshOledScreen();
#endif
  } else if (cmd == "debug on") {
    oledSerialMirrorMode = true;
    Serial.println("OLED 调试：开（屏仅显示串口日志区）");
    addLog("OLED 调试模式：开");  // addLog 在镜像模式下会刷新 OLED
  } else if (cmd == "debug") {
    oledSerialMirrorMode = !oledSerialMirrorMode;
    Serial.println(oledSerialMirrorMode ? "OLED 调试：开" : "OLED 调试：关");
    addLog(String("OLED 调试：") + (oledSerialMirrorMode ? "开" : "关"));
#if OLED_ENABLE
    if (oledOk && !oledSerialMirrorMode) refreshOledScreen();  // 关镜像时 addLog 不会刷屏
#endif
  } else if (cmd == "status") {
    Serial.println("\n=== 当前状态 ===");
    Serial.print("连接状态: ");
    Serial.println(connectionState == CONN_WAITING_A ? "搜索 A/B" : "双开关就绪");
    Serial.print("ESP-NOW 调试: ");
    Serial.println(espNowDebug ? "开" : "关");
    Serial.print("OLED 调试(仅日志): ");
    Serial.println(oledSerialMirrorMode ? "开" : "关");
    Serial.print("详细开关日志: ");
    Serial.println(verboseSwitchLog ? "开" : "关");
    Serial.print("LED数量: ");
    Serial.println(NUM_LEDS);
    Serial.print("当前亮度: ");
    Serial.println(FastLED.getBrightness());
    printCurrentMode();
    Serial.print("最近按下: ");
    Serial.println(lastSwitchPressed == 0 ? "无" : lastSwitchPressed == 'A' ? "开关A" : "开关B");
    if (WiFi.status() == WL_CONNECTED) {
      Serial.print("WiFi IP: ");
      Serial.println(WiFi.localIP());
    } else {
      Serial.println("WiFi: 未连接");
    }
  } else {
    Serial.println("未知命令。可用: 1,2,3,A,B, brightness, status, debug, verbose on/off, espnow on/off");
  }
}

void printCurrentMode() {
  Serial.print("当前模式: ");
  if (currentMode == TEST_ALL_ON) {
    Serial.println("全部点亮（白色）");
  } else if (currentMode == TEST_ONE_BY_ONE) {
    Serial.println("逐个点亮（彩虹色）");
  } else if (currentMode == TEST_GAME) {
    Serial.println("游戏模式（Dot：电脑0→末端，玩家末端→0）");
  }
}

// 搜索 A/B 时的灯效：蓝呼吸
void testWaitingForA() {
  uint8_t b = 40 + (uint8_t)(30 * (1 + sin(2 * 3.14159f * millis() / 2000.0f)) / 2);
  fill_solid(leds, NUM_LEDS, CHSV(160, 255, b));  // 160 ≈ 蓝色
}

// 双开关就绪、等待按 A 开始时的灯效：彩色流水
void testReadyStrip() {
  unsigned long t = millis();
  uint8_t hueOffset = (t / 50) % 256;
  for (int i = 0; i < NUM_LEDS; i++) {
    leds[i] = CHSV((hueOffset + (i * 256 / 24)) % 256, 255, 200);
  }
}

// 测试1：全部点亮（白色）
void testAllOn() {
  fill_solid(leds, NUM_LEDS, CRGB::White);
}

// 测试2：逐个点亮（彩虹色）
void testOneByOne() {
  // 清空所有LED
  FastLED.clear();
  
  // 点亮当前LED，使用彩虹色
  uint8_t hue = map(currentLed, 0, NUM_LEDS, 0, 255);
  leds[currentLed] = CHSV(hue, 255, 255);
  
  // 移动到下一个LED
  currentLed++;
  if (currentLed >= NUM_LEDS) {
    currentLed = 0;
    delay(500); // 循环完成后稍作停顿
  }
  
  delay(20); // 每个LED之间的延迟
}

// 旧颜色回传玩法已弃用（改为 Dot + 状态机玩法）

// 向开关 A 发短消息（ACK_A、TA、RDY 等，内部双发抗丢包）
void sendToSwitchA(const uint8_t* data, size_t len) {
  if (!switchAMacValid || !data || len == 0) return;
  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, switchAMac, 6);
  peer.channel = espNowPeerChannel;
  peer.ifidx = espNowIfidx;
  peer.encrypt = false;
  esp_err_t addRet = esp_now_add_peer(&peer);
  if (addRet != ESP_OK && addRet != ESP_ERR_ESPNOW_EXIST) return;
  sendCopiesTo(switchAMac, data, len, 2);
}

// 向开关 B 发短消息（ACK_B、TB、RDY 等，内部双发抗丢包）
void sendToSwitchB(const uint8_t* data, size_t len) {
  if (!switchBMacValid || !data || len == 0) return;
  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, switchBMac, 6);
  peer.channel = espNowPeerChannel;
  peer.ifidx = espNowIfidx;
  peer.encrypt = false;
  esp_err_t addRetB = esp_now_add_peer(&peer);
  if (addRetB != ESP_OK && addRetB != ESP_ERR_ESPNOW_EXIST) return;
  sendCopiesTo(switchBMac, data, len, 2);
}

/** COL 同步帧：'C','O','L' + H,S,V + meta（低 bit=该开关可按，高 4bit=GameState）。A/B 的 meta 不同 */
void sendColToBothSwitches() {
  // 目标色 = pending 队头最旧（防守）或色流下一个预告色（进攻），下发当场生成的色相
  uint8_t h = g_game.targetHue();
  uint8_t s = 255, v = 255;
  uint8_t gs = (uint8_t)g_game.state();
  uint8_t metaA = (uint8_t)((gs << 4) | (g_game.expectedButton() == 'A' ? 1 : 0));
  uint8_t metaB = (uint8_t)((gs << 4) | (g_game.expectedButton() == 'B' ? 1 : 0));
  uint8_t bufA[7] = {'C', 'O', 'L', h, s, v, metaA};
  uint8_t bufB[7] = {'C', 'O', 'L', h, s, v, metaB};
  sendToSwitchA(bufA, sizeof(bufA));
  sendToSwitchB(bufB, sizeof(bufB));
}

// 游戏模式（Future Queue + Active Dots；tick 在 loop 中已调用）
void testGame() {
  g_game.renderStrip(leds, NUM_LEDS, millis(), false);
}

/** 关键指令多连发抗丢包：首包立刻发，后续拷贝按 25ms 排队，不再 delay 堵灯带 */
#define DELAYED_COPY_SLOTS 8
#define DELAYED_COPY_GAP_MS 25UL
#define DELAYED_COPY_MAX_LEN 16
struct DelayedCopy {
  uint8_t mac[6];
  uint8_t data[DELAYED_COPY_MAX_LEN];
  uint8_t len;
  uint8_t remaining;
  unsigned long nextMs;
};
static DelayedCopy delayedCopies[DELAYED_COPY_SLOTS];

static void enqueueDelayedCopy(const uint8_t* mac, const uint8_t* data, size_t len, int extra) {
  if (extra <= 0 || !mac || !data || len == 0 || len > DELAYED_COPY_MAX_LEN) return;
  for (int i = 0; i < DELAYED_COPY_SLOTS; i++) {
    if (delayedCopies[i].remaining != 0) continue;
    memcpy(delayedCopies[i].mac, mac, 6);
    memcpy(delayedCopies[i].data, data, len);
    delayedCopies[i].len = (uint8_t)len;
    delayedCopies[i].remaining = (uint8_t)extra;
    delayedCopies[i].nextMs = millis() + DELAYED_COPY_GAP_MS;
    return;
  }
}

static void flushDelayedCopies(unsigned long now) {
  for (int i = 0; i < DELAYED_COPY_SLOTS; i++) {
    DelayedCopy& s = delayedCopies[i];
    if (s.remaining == 0) continue;
    if ((long)(now - s.nextMs) < 0) continue;
    esp_now_send(s.mac, s.data, s.len);
    s.remaining--;
    s.nextMs = now + DELAYED_COPY_GAP_MS;
  }
}

void sendCopiesTo(const uint8_t* mac, const uint8_t* data, size_t len, int copies) {
  if (!mac || !data || len == 0) return;
  if (copies < 1) copies = 1;
  esp_now_send(mac, data, len);
  if (copies > 1) enqueueDelayedCopy(mac, data, len, copies - 1);
}

static void bridgeSendA(const uint8_t* data, size_t len) { sendToSwitchA(data, len); }

static void bridgeSendB(const uint8_t* data, size_t len) { sendToSwitchB(data, len); }

void hostResetGame() {
  g_game.forceIdle();
  g_game.syncTurnToIdle(switchAMacValid, switchBMacValid, bridgeSendA, bridgeSendB);
  addLog("主机 SET：游戏已重置回待机");
#if BUZZER_ENABLE
  buzzerSpeedFeedback();
#endif
#if OLED_ENABLE
  if (oledOk) refreshOledScreen();
#endif
}
