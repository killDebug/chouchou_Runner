#include "queue_manager.h"
#include <Arduino.h>

void QueueManager::reset() {
  q_.clear();
  ensureMinLength();
}

uint8_t QueueManager::peekHead() const {
  if (q_.empty()) return 0;
  return q_.front();
}

void QueueManager::appendNextRainbow() {
  uint8_t next = q_.empty() ? 0 : (uint8_t)((q_.back() + 1) % (uint8_t)RAINBOW_LEN);
  q_.push_back(next);
}

void QueueManager::ensureMinLength() {
  while ((int)q_.size() < MIN_QUEUE_SIZE) {
    appendNextRainbow();
  }
}

uint8_t QueueManager::consumeHeadForSpawn() {
  ensureMinLength();
  uint8_t c = q_.front();
  q_.pop_front();
  ensureMinLength();
  return c;
}

void QueueManager::debugSnapshot(uint8_t outHeadColors[8], int& outLen) const {
  outLen = 0;
  for (size_t i = 0; i < q_.size() && outLen < 8; i++) {
    outHeadColors[outLen++] = q_[i];
  }
}
