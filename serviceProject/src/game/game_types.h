#pragma once

#include <stdint.h>

// 与规格一致；PAUSE = 断链暂停
enum class GameState : uint8_t { IDLE = 0, RUNNING, WIN, LOSE, PAUSE };

// 场上运动实体（灯带为离散格点，position 为 LED 索引）
struct GameDot {
  uint32_t id;
  uint8_t colorIndex;  // 色流递增序号（同色碰撞判定依据）
  uint8_t hue;         // 实际渲染色相（每出一颗现场生成，配对点共用）
  int position;
  int8_t direction;
  bool active;
};
