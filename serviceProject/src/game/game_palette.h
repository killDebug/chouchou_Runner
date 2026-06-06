#pragma once

#include <FastLED.h>

// 与 colorIndex 0..NUM_GAME_COLORS-1 对应（彩虹七色顺序）
static constexpr int NUM_GAME_COLORS = 7;

inline CHSV gamePaletteHsv(uint8_t colorIndex) {
  static const uint8_t hues[NUM_GAME_COLORS] = {
      0,    // 红
      32,   // 橙
      64,   // 黄
      96,   // 绿
      128,  // 青
      160,  // 蓝
      208,  // 紫
  };
  uint8_t i = colorIndex % NUM_GAME_COLORS;
  return CHSV(hues[i], 255, 255);
}

inline void gamePaletteToHsvBytes(uint8_t colorIndex, uint8_t& h, uint8_t& s, uint8_t& v) {
  CHSV c = gamePaletteHsv(colorIndex);
  h = c.h;
  s = c.s;
  v = c.v;
}
