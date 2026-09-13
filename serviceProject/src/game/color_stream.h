#pragma once

#include <stdint.h>

// 主机侧共享颜色流：电脑定时出点与玩家进攻发射共用同一序列。
// 每消耗一个颜色就现场挑一个新色相，躲开最近用过的和灯带上还在跑的颜色，
// 不再开局锁死 5~7 色循环。
class ColorStream {
 public:
  /** 新开一局：重置序号并立刻生成第一颗预告色 */
  void newSession();

  /** 当前预告色的碰撞序号（递增，与色相无关） */
  uint8_t currentColor() const { return id_; }

  /** 当前预告色的色相（0~255），开关 COL 与灯带出点共用 */
  uint8_t currentHue() const { return hue_; }

  /**
   * 消耗当前色并生成下一颗。
   * avoidHues：场上已有色相，尽量不要跟它们撞脸。
   */
  void advanceWave(const uint8_t* avoidHues = nullptr, int avoidCount = 0);

  uint32_t waveIndex() const { return cursor_; }

  /** 调试用：接下来只有「当前预告色」可预知，后续要现场生成 */
  void debugUpcoming(uint8_t outColors[8], int& outLen, int maxLen = 8) const;

 private:
  void pickNext(const uint8_t* avoidHues, int avoidCount);
  int minDistToKnown(uint8_t hue, const uint8_t* avoidHues, int avoidCount) const;
  static int hueDist(uint8_t a, uint8_t b);

  static constexpr int kRecentKeep = 16;
  static constexpr int kMinHueSep = 22;

  uint8_t id_ = 0;
  uint8_t hue_ = 0;
  uint8_t nextId_ = 0;
  uint32_t cursor_ = 0;
  uint8_t recentHues_[kRecentKeep] = {};
  uint8_t recentCount_ = 0;
};
