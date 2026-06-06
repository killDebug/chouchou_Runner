#pragma once

#include "game_types.h"

// 同色 + 反向 才抵消；保留同格与擦肩交换
void gameResolveCollisions(GameDot* dots, int maxDots, int& dotCount, const int* prevPos);
