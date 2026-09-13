#include "color_stream.h"
#include <esp_random.h>

int ColorStream::hueDist(uint8_t a, uint8_t b) {
  int d = (int)a - (int)b;
  if (d < 0) d = -d;
  if (d > 128) d = 256 - d;
  return d;
}

int ColorStream::minDistToKnown(uint8_t hue, const uint8_t* avoidHues, int avoidCount) const {
  int minDist = 256;
  for (int i = 0; i < recentCount_; i++) {
    int d = hueDist(hue, recentHues_[i]);
    if (d < minDist) minDist = d;
  }
  if (avoidHues && avoidCount > 0) {
    for (int i = 0; i < avoidCount; i++) {
      int d = hueDist(hue, avoidHues[i]);
      if (d < minDist) minDist = d;
    }
  }
  return minDist;
}

void ColorStream::pickNext(const uint8_t* avoidHues, int avoidCount) {
  id_ = nextId_++;
  uint8_t best = (uint8_t)(hue_ + 97);
  int bestMin = -1;
  const bool haveKnown = (recentCount_ > 0) || (avoidHues && avoidCount > 0);

  if (!haveKnown) {
    hue_ = (uint8_t)esp_random();
  } else {
    for (int attempt = 0; attempt < 28; attempt++) {
      uint8_t cand = (uint8_t)esp_random();
      int md = minDistToKnown(cand, avoidHues, avoidCount);
      if (md > bestMin) {
        bestMin = md;
        best = cand;
        if (md >= kMinHueSep) break;
      }
    }
    if (bestMin < kMinHueSep) {
      // 色环挤满时按与 256 互质的步长跳开，避免又回到刚用过的色
      int jitter = (int)(esp_random() % 13) - 6;
      best = (uint8_t)(hue_ + 97 + jitter);
    }
    hue_ = best;
  }

  if (recentCount_ < kRecentKeep) {
    recentHues_[recentCount_++] = hue_;
  } else {
    for (int i = 1; i < kRecentKeep; i++) recentHues_[i - 1] = recentHues_[i];
    recentHues_[kRecentKeep - 1] = hue_;
  }
}

void ColorStream::newSession() {
  nextId_ = 0;
  cursor_ = 0;
  recentCount_ = 0;
  hue_ = 0;
  pickNext(nullptr, 0);
}

void ColorStream::advanceWave(const uint8_t* avoidHues, int avoidCount) {
  cursor_++;
  pickNext(avoidHues, avoidCount);
}

void ColorStream::debugUpcoming(uint8_t outColors[8], int& outLen, int maxLen) const {
  outLen = 0;
  if (!outColors || maxLen <= 0) return;
  outColors[outLen++] = id_;
}
