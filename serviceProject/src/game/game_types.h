#pragma once

#include <stdint.h>

// 与规格一致；PAUSE = 断链暂停
enum class GameState : uint8_t { IDLE = 0, RUNNING, WIN, LOSE, PAUSE };

// 场上运动实体（灯带为离散格点，position 为 LED 索引）
struct GameDot {
  uint32_t id;
  uint8_t colorIndex;
  int position;
  int8_t direction;
  bool active;
};
