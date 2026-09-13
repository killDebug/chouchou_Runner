#pragma once

#include <stdint.h>

// 与规格一致；PAUSE = 断链暂停
enum class GameState : uint8_t { IDLE = 0, RUNNING, WIN, LOSE, PAUSE };

// 场上运动实体（灯带为离散格点，position 为 LED 索引）
struct GameDot {
  uint32_t id;
  uint8_t colorIndex;  // 主题内颜色序号（同色碰撞判定依据，与色相微抖无关）
  uint8_t hue;         // 实际渲染色相 = 基础色相 ±5 随机微抖（每次发射略有质感差异）
  int position;
  int8_t direction;
  bool active;
};
