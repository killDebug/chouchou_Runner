#pragma once

#include <Arduino.h>
#include <deque>

// Future Color Queue：仅彩虹 7 色，严格按 0→1→…→6→0 循环补尾
class QueueManager {
 public:
  static constexpr int MIN_QUEUE_SIZE = 20;
  static constexpr int RAINBOW_LEN = 7;

  void reset();
  void ensureMinLength();
  // 生成一个点：取队头颜色并从队列移除，随后补足长度
  uint8_t consumeHeadForSpawn();
  size_t size() const { return q_.size(); }
  bool empty() const { return q_.empty(); }
  uint8_t peekHead() const;
  // 调试用
  void debugSnapshot(uint8_t outHeadColors[8], int& outLen) const;

 private:
  void appendNextRainbow();

  std::deque<uint8_t> q_;
};
