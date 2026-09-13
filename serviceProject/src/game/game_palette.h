#pragma once

#include <FastLED.h>
#include <stdint.h>

// 主题调色板：每局 newSession 时随机挑选一套主题作为本局核心颜色。
// 主题内各色 HSV 色相互相隔开，保证肉眼可分辨；
// 同色碰撞判定用的是 colorIndex（主题内序号），与发射时的色相微抖无关。

// 主题 1 - 经典彩虹（7 色）
static const uint8_t kThemeRainbowHues[] = {0, 28, 48, 96, 128, 160, 200};
// 主题 2 - 赛博霓虹（6 色）
static const uint8_t kThemeNeonHues[] = {230, 42, 90, 140, 190, 5};
// 主题 3 - 暖阳热带（5 色）
static const uint8_t kThemeTropicalHues[] = {0, 20, 40, 55, 240};
// 主题 4 - 冰雪极光（5 色）
static const uint8_t kThemeAuroraHues[] = {85, 115, 135, 165, 195};

struct GameTheme {
  const char* name;
  const uint8_t* hues;
  uint8_t count;
};

static const GameTheme kGameThemes[] = {
    {"经典彩虹", kThemeRainbowHues, (uint8_t)(sizeof(kThemeRainbowHues) / sizeof(kThemeRainbowHues[0]))},
    {"赛博霓虹", kThemeNeonHues, (uint8_t)(sizeof(kThemeNeonHues) / sizeof(kThemeNeonHues[0]))},
    {"暖阳热带", kThemeTropicalHues, (uint8_t)(sizeof(kThemeTropicalHues) / sizeof(kThemeTropicalHues[0]))},
    {"冰雪极光", kThemeAuroraHues, (uint8_t)(sizeof(kThemeAuroraHues) / sizeof(kThemeAuroraHues[0]))},
};
static constexpr uint8_t kNumGameThemes = (uint8_t)(sizeof(kGameThemes) / sizeof(kGameThemes[0]));
static constexpr uint8_t kMaxGameColors = 7;  // 主题色数上限（点池/FIFO 按此预留）

/** 主题内某颜色序号的基础色相（未加抖动） */
inline uint8_t gameThemeHue(uint8_t themeIndex, uint8_t colorIndex) {
  const GameTheme& t = kGameThemes[themeIndex % kNumGameThemes];
  return t.hues[colorIndex % t.count];
}

/** 主题内某颜色序号的 HSV（发射时刻再加 ±5 色相微抖，见 GameManager::addDot） */
inline CHSV gamePaletteHsv(uint8_t themeIndex, uint8_t colorIndex) {
  return CHSV(gameThemeHue(themeIndex, colorIndex), 255, 255);
}
