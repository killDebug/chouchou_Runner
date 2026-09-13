#pragma once

#include <stdint.h>

// 主机侧共享颜色流：电脑定时出点与玩家进攻发射共用同一序列。
// 每局按当前主题的色数做 Fisher–Yates 洗牌 + 随机互质步进，保证每局顺序不同且一局内遍历所有颜色。
// 每消耗一个颜色（电脑出点，或玩家积压清空后的进攻发射）游标 +1。
class ColorStream {
 public:
  static constexpr int kMaxPalette = 7;

  /** 新开一局：paletteLen = 当前主题色数（5~7） */
  void newSession(uint8_t paletteLen);

  uint8_t paletteLen() const { return paletteLen_; }

  /** 当前游标颜色（colorIndex，0..paletteLen-1），即"下一个预告色" */
  uint8_t currentColor() const;

  /** 消耗一个颜色（电脑出点 / 玩家进攻发射后调用） */
  void advanceWave() { cursor_++; }

  uint32_t waveIndex() const { return cursor_; }

  /** 调试用：接下来 n 个波的颜色快照 */
  void debugUpcoming(uint8_t outColors[8], int& outLen, int maxLen = 8) const;

 private:
  uint8_t colorAtWave(uint32_t w) const;

  uint8_t perm_[kMaxPalette] = {0, 1, 2, 3, 4, 5, 6};
  uint8_t paletteLen_ = 7;
  uint8_t step_ = 1;    // 与色数互质，决定局内遍历顺序
  uint8_t offset_ = 0;
  uint32_t cursor_ = 0;
};
