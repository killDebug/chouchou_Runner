#include "collision_manager.h"
#include <string.h>

void gameResolveCollisions(GameDot* dots, int maxDots, int& dotCount, const int* prevPos) {
  bool remove[64];
  if (maxDots > 64) maxDots = 64;
  memset(remove, 0, sizeof(remove));

  for (int i = 0; i < maxDots; i++) {
    if (!dots[i].active) continue;
    for (int j = i + 1; j < maxDots; j++) {
      if (!dots[j].active) continue;
      if (dots[i].direction != -dots[j].direction) continue;
      if (dots[i].colorIndex != dots[j].colorIndex) continue;

      if (dots[i].position == dots[j].position) {
        remove[i] = true;
        remove[j] = true;
        continue;
      }
      if (prevPos[i] == dots[j].position && prevPos[j] == dots[i].position) {
        remove[i] = true;
        remove[j] = true;
      }
    }
  }

  for (int i = 0; i < maxDots; i++) {
    if (remove[i] && dots[i].active) {
      dots[i].active = false;
      dotCount--;
    }
  }
  if (dotCount < 0) dotCount = 0;
}
