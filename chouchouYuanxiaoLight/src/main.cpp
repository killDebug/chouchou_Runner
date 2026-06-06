/**
 * 元宵节灯笼灯效版 - ESP32-C3 Super Mini
 * 只保留 WS2812B 灯环控制，自动循环几种“灯笼风”炫酷灯效。
 *
 * 电路：灯带数据线接 LED_PIN，VCC 接 5V（视灯带规格），GND 共地。
 */
#include <Arduino.h>
#include <FastLED.h>

// WS281x 灯环（5050，一圈 12 颗，可按实际数量改 NUM_LEDS）
#define LED_PIN      2
#define NUM_LEDS     12
#define LED_TYPE     WS2812B
#define COLOR_ORDER  GRB
// 整体亮度调大，如果灯环太亮可适当往下调
#define BRIGHTNESS   220

CRGB leds[NUM_LEDS];

// 元宵灯笼灯效模式
enum EffectMode {
  EFFECT_BREATH = 0,   // 整体红橙呼吸灯，模拟灯笼一明一暗
  EFFECT_RING,         // 金色流光沿灯笼边缘旋转
  EFFECT_SPARKLE,      // 红底金色星星闪烁，像灯笼金纹反光
  EFFECT_POLICE,       // 蓝红警灯效果，小朋友最爱
  EFFECT_METEOR,       // 流星拖尾扫过灯笼
  EFFECT_RAINBOW,      // 全彩彩虹波浪，环形循环
  EFFECT_COUNT
};

EffectMode currentEffect = EFFECT_BREATH;
unsigned long lastEffectSwitch = 0;
const unsigned long EFFECT_DURATION_MS = 12000;  // 每种灯效持续时间（毫秒）

// 前置声明
void lanternBreath();
void lanternRing();
void lanternSparkle();
void lanternPolice();
void lanternMeteor();
void lanternRainbow();

void setup() {
  Serial.begin(115200);
  delay(500);

  Serial.println();
  Serial.println("========================================");
  Serial.println("   ESP32-C3 元宵灯笼灯环灯效");
  Serial.println("   仅保留灯环控制，自动炫酷循环");
  Serial.println("========================================");

  FastLED.addLeds<LED_TYPE, LED_PIN, COLOR_ORDER>(leds, NUM_LEDS);
  FastLED.setBrightness(BRIGHTNESS);
  FastLED.clear(true);

  Serial.print("灯环: GPIO ");
  Serial.print(LED_PIN);
  Serial.print(", ");
  Serial.print(NUM_LEDS);
  Serial.println(" 颗 WS281x (WS2812B)");
}

void loop() {
  unsigned long now = millis();

  // 周期性切换不同元宵灯笼灯效
  if (now - lastEffectSwitch > EFFECT_DURATION_MS) {
    lastEffectSwitch = now;
    currentEffect = static_cast<EffectMode>((currentEffect + 1) % EFFECT_COUNT);
  }

  switch (currentEffect) {
    case EFFECT_BREATH:
      lanternBreath();
      break;
    case EFFECT_RING:
      lanternRing();
      break;
    case EFFECT_SPARKLE:
      lanternSparkle();
      break;
    case EFFECT_POLICE:
      lanternPolice();
      break;
    case EFFECT_METEOR:
      lanternMeteor();
      break;
    case EFFECT_RAINBOW:
      lanternRainbow();
      break;
    default:
      lanternBreath();
      break;
  }

  FastLED.show();
  // 小 delay 让动画更平滑，避免占满 CPU
  delay(10);
}

// 温暖红橙呼吸灯，模拟灯笼整体一明一暗起伏
void lanternBreath() {
  // beatsin8：sin 波，40 为速度，40~255 为亮度范围
  uint8_t v = beatsin8(40, 40, 255);
  // H≈10 左右偏橙的红，S=饱和度
  CHSV c(10, 255, v);
  for (int i = 0; i < NUM_LEDS; i++) {
    leds[i] = c;
  }
}

// 环形流光：一圈金色火焰绕灯笼边缘跑马，带尾巴
void lanternRing() {
  static uint8_t pos = 0;

  // 整体红色打底，先稍微暗一点
  for (int i = 0; i < NUM_LEDS; i++) {
    leds[i] = CHSV(8, 255, 40);
  }

  // 使用淡出形成流动尾巴
  fadeToBlackBy(leds, NUM_LEDS, 40);

  // 当前位置点亮金黄火焰色
  leds[pos] = CHSV(20, 220, 255);
  // 邻居稍微弱一点，形成“火舌”效果
  leds[(pos + NUM_LEDS - 1) % NUM_LEDS] = CHSV(15, 230, 150);
  leds[(pos + 1) % NUM_LEDS]           = CHSV(25, 230, 150);

  // 控制转动速度：80ms 移动一颗灯
  EVERY_N_MILLISECONDS(80) {
    pos = (pos + 1) % NUM_LEDS;
  }
}

// 红底随机金色星星，模拟灯笼上的金纹/流光闪烁
void lanternSparkle() {
  // 对当前颜色做轻微衰减，同时保证整体有红色底光
  for (int i = 0; i < NUM_LEDS; i++) {
    leds[i].nscale8(220);  // 整体淡出
    if (leds[i].getAverageLight() < 10) {
      // 底色保持为偏暗的红光
      leds[i] = CHSV(5, 255, 30);
    }
  }

  // 随机几颗闪亮的金色点，带一点“跳动感”
  if (random8() < 80) {  // 概率控制闪烁频率（数值越大星星越多）
    int idx = random8(NUM_LEDS);
    leds[idx] = CHSV(35, 180, 255); // 偏金黄色
  }
}

// 蓝红警灯效果：左右两半交替闪烁，同时带一点轻微渐变，减弱生硬感
void lanternPolice() {
  static bool phase = false;
  static unsigned long lastToggle = 0;
  unsigned long now = millis();

  // 每 160ms 在两种状态间切换（可调快慢）
  if (now - lastToggle > 160) {
    lastToggle = now;
    phase = !phase;
  }

  // 先整体清空
  fill_solid(leds, NUM_LEDS, CRGB::Black);

  // 一边蓝，一边红：模拟车顶双灯
  for (int i = 0; i < NUM_LEDS; i++) {
    bool leftHalf = (i < NUM_LEDS / 2);
    if (phase) {
      // Phase A：左蓝右红
      if (leftHalf) {
        leds[i] = CHSV(160, 255, 255);  // 蓝
      } else {
        leds[i] = CHSV(0, 255, 255);    // 红
      }
    } else {
      // Phase B：左红右蓝
      if (leftHalf) {
        leds[i] = CHSV(0, 255, 255);    // 红
      } else {
        leds[i] = CHSV(160, 255, 255);  // 蓝
      }
    }
  }

  // 轻微闪烁抖动：不每次都全亮，做一点随机变化
  if (random8() < 40) {
    int idx = random8(NUM_LEDS);
    leds[idx].nscale8(150);
  }
}

// 流星拖尾扫过：亮点从一侧扫到另一侧，带渐渐消失的尾巴
void lanternMeteor() {
  static uint8_t head = 0;

  // 逐渐淡出当前所有像素形成尾巴
  fadeToBlackBy(leds, NUM_LEDS, 40);

  // 流星头——暖黄色亮点
  leds[head] = CHSV(30, 200, 255);
  // 头部附近略亮，让流星有“核”
  leds[(head + NUM_LEDS - 1) % NUM_LEDS] += CHSV(25, 220, 180);

  // 控制移动速度与方向（这里顺时针）
  EVERY_N_MILLISECONDS(90) {
    head = (head + 1) % NUM_LEDS;
  }
}

// 全彩彩虹波浪：整圈彩虹色不断流动，适合小朋友看“七彩灯笼”
void lanternRainbow() {
  static uint8_t baseHue = 0;

  // 为每一颗灯分配不同的 Hue 偏移，形成一整圈的彩虹
  for (int i = 0; i < NUM_LEDS; i++) {
    uint8_t hue = baseHue + (uint8_t)(255 / NUM_LEDS) * i;
    // 适当降低饱和度/亮度，避免太刺眼
    leds[i] = CHSV(hue, 220, 255);
  }

  // 缓慢滚动 Hue，让彩虹沿环形流动
  EVERY_N_MILLISECONDS(50) {
    baseHue++;
  }
}

