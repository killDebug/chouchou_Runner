#include "color_stream.h"
#include <esp_random.h>

static uint8_t gcdU8(uint8_t a, uint8_t b) {
  while (b) {
    uint8_t t = (uint8_t)(a % b);
    a = b;
    b = t;
  }
  return a;
}

void ColorStream::newSession(uint8_t paletteLen) {
  if (paletteLen < 2) paletteLen = 2;
  if (paletteLen > kMaxPalette) paletteLen = kMaxPalette;
  paletteLen_ = paletteLen;

  for (int i = 0; i < kMaxPalette; i++) perm_[i] = (uint8_t)i;
  // Fisher–Yates 洗牌：每局颜色出现顺序不同
  for (int i = paletteLen_ - 1; i > 0; i--) {
    int j = (int)(esp_random() % (uint32_t)(i + 1));
    uint8_t t = perm_[i];
    perm_[i] = perm_[j];
    perm_[j] = t;
  }

  // 随机选一个与色数互质的步进：一局内能遍历到所有颜色
  uint8_t coprime[kMaxPalette];
  uint8_t n = 0;
  for (uint8_t s = 1; s < paletteLen_; s++) {
    if (gcdU8(s, paletteLen_) == 1) coprime[n++] = s;
  }
  step_ = n ? coprime[esp_random() % n] : 1;
  offset_ = (uint8_t)(esp_random() % paletteLen_);
  cursor_ = 0;
}

uint8_t ColorStream::colorAtWave(uint32_t w) const {
  uint8_t slot = (uint8_t)((w * (uint32_t)step_ + (uint32_t)offset_) % (uint32_t)paletteLen_);
  return perm_[slot];
}

uint8_t ColorStream::currentColor() const { return colorAtWave(cursor_); }

void ColorStream::debugUpcoming(uint8_t outColors[8], int& outLen, int maxLen) const {
  outLen = 0;
  if (!outColors || maxLen <= 0) return;
  int n = maxLen < 8 ? maxLen : 8;
  for (int i = 0; i < n; i++) {
    outColors[outLen++] = colorAtWave(cursor_ + (uint32_t)i);
  }
}
