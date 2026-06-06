#include "game_manager.h"
#include "collision_manager.h"
#include "game_palette.h"
#include <Arduino.h>
#include <string.h>

#define RDY_MSG "RDY"
#define GOF_MSG "GOF"

GameManager g_game;

void GameManager::configure(int numLeds, int computerSpawnIntervalMs, int moveIntervalMs) {
  numLeds_ = numLeds;
  computerSpawnIntervalMs_ = computerSpawnIntervalMs;
  moveIntervalMs_ = moveIntervalMs;
}

void GameManager::clearDots() {
  dotCount_ = 0;
  for (int i = 0; i < kMaxDots; i++) dots_[i].active = false;
}

void GameManager::compactDots() {
  int w = 0;
  for (int i = 0; i < kMaxDots; i++) {
    if (!dots_[i].active) continue;
    if (w != i) dots_[w] = dots_[i];
    w++;
  }
  for (int i = w; i < kMaxDots; i++) dots_[i].active = false;
  dotCount_ = w;
}

bool GameManager::addDot(int position, int direction, uint8_t colorIndex) {
  if (dotCount_ >= kMaxDots) return false;
  for (int i = 0; i < kMaxDots; i++) {
    if (!dots_[i].active) {
      dots_[i].id = nextDotId_++;
      dots_[i].colorIndex = colorIndex % NUM_GAME_COLORS;
      dots_[i].position = position;
      dots_[i].direction = (int8_t)direction;
      dots_[i].active = true;
      dotCount_++;
      return true;
    }
  }
  return false;
}

void GameManager::setTurn(char nextExpected, bool switchAMacValid, bool switchBMacValid, void (*sendToA)(const uint8_t*, size_t),
                          void (*sendToB)(const uint8_t*, size_t)) {
  expectedButton_ = nextExpected;
  static const uint8_t TA[] = {'T', 'A'};
  static const uint8_t TB[] = {'T', 'B'};
  if (nextExpected == 'A') {
    if (switchAMacValid) sendToA(TA, 2);
    if (switchBMacValid) sendToB((const uint8_t*)RDY_MSG, 3);
  } else {
    if (switchBMacValid) sendToB(TB, 2);
    if (switchAMacValid) sendToA((const uint8_t*)RDY_MSG, 3);
  }
}

void GameManager::setGameResult(GameState r, bool switchAMacValid, bool switchBMacValid, void (*sendToA)(const uint8_t*, size_t),
                                void (*sendToB)(const uint8_t*, size_t)) {
  state_ = r;
  if (r == GameState::WIN) {
    if (switchAMacValid) sendToA((const uint8_t*)"WIN", 3);
    if (switchBMacValid) sendToB((const uint8_t*)"WIN", 3);
  } else if (r == GameState::LOSE) {
    if (switchAMacValid) sendToA((const uint8_t*)GOF_MSG, 3);
    if (switchBMacValid) sendToB((const uint8_t*)GOF_MSG, 3);
  }
}

void GameManager::syncButtonsColor(unsigned long now, bool switchAMacValid, bool switchBMacValid,
                                   void (*sendToA)(const uint8_t*, size_t), void (*sendToB)(const uint8_t*, size_t)) {
  (void)now;
  (void)switchAMacValid;
  (void)switchBMacValid;
  (void)sendToA;
  (void)sendToB;
  // 当前阶段：不同步按钮灯环；颜色仅以主机 Future Queue + consume 为准
}

void GameManager::tickRunning(unsigned long now, bool switchAMacValid, bool switchBMacValid, void (*sendToA)(const uint8_t*, size_t),
                              void (*sendToB)(const uint8_t*, size_t)) {
  if (now - lastComputerSpawnMs_ >= (unsigned long)computerSpawnIntervalMs_) {
    uint8_t c = queue_.consumeHeadForSpawn();
    addDot(0, +1, c);
    lastComputerSpawnMs_ = now;
  }

  if (now - lastMoveMs_ < (unsigned long)moveIntervalMs_) {
    return;
  }
  lastMoveMs_ = now;

  int prevPos[kMaxDots];
  for (int i = 0; i < kMaxDots; i++) prevPos[i] = dots_[i].position;

  for (int i = 0; i < kMaxDots; i++) {
    if (!dots_[i].active) continue;
    dots_[i].position += dots_[i].direction;
  }

  gameResolveCollisions(dots_, kMaxDots, dotCount_, prevPos);
  compactDots();

  for (int i = 0; i < kMaxDots; i++) {
    if (!dots_[i].active) continue;
    if (dots_[i].direction == +1 && dots_[i].position >= (numLeds_ - 1)) {
      setGameResult(GameState::LOSE, switchAMacValid, switchBMacValid, sendToA, sendToB);
      clearDots();
      return;
    }
    if (dots_[i].direction == -1 && dots_[i].position <= 0) {
      setGameResult(GameState::WIN, switchAMacValid, switchBMacValid, sendToA, sendToB);
      clearDots();
      return;
    }
  }

}

void GameManager::tick(unsigned long now, bool connReady, bool testGameMode, bool switchAMacValid, bool switchBMacValid,
                       void (*sendToA)(const uint8_t*, size_t), void (*sendToB)(const uint8_t*, size_t)) {
  (void)connReady;
  if (!testGameMode) return;

  // 游戏未进行时持续补足 Future Color Queue，开局按 A 即消费当前队头（与 OLED/调试 peek 一致）
  if (state_ == GameState::IDLE || state_ == GameState::PAUSE) {
    queue_.ensureMinLength();
  }

  if (state_ == GameState::RUNNING) {
    tickRunning(now, switchAMacValid, switchBMacValid, sendToA, sendToB);
  } else if (state_ == GameState::IDLE) {
    syncButtonsColor(now, switchAMacValid, switchBMacValid, sendToA, sendToB);
  } else if (state_ == GameState::PAUSE) {
    syncButtonsColor(now, switchAMacValid, switchBMacValid, sendToA, sendToB);
  }
}

void GameManager::resetToIdle() {
  state_ = GameState::IDLE;
  clearDots();
  expectedButton_ = 'A';
}

void GameManager::forceIdle() { resetToIdle(); }

void GameManager::syncTurnToIdle(bool switchAMacValid, bool switchBMacValid, void (*sendToA)(const uint8_t*, size_t),
                                 void (*sendToB)(const uint8_t*, size_t)) {
  setTurn('A', switchAMacValid, switchBMacValid, sendToA, sendToB);
}

void GameManager::enterPause(unsigned long now) {
  if (state_ != GameState::RUNNING) return;
  state_ = GameState::PAUSE;
  pauseStartedMs_ = now;
}

void GameManager::resumeFromPause(unsigned long now) {
  (void)now;
  if (state_ != GameState::PAUSE) return;
  state_ = GameState::RUNNING;
  pauseStartedMs_ = 0;
}

bool GameManager::shouldHardResetPause(unsigned long now) const {
  if (state_ != GameState::PAUSE) return false;
  return pauseStartedMs_ != 0 && (now - pauseStartedMs_) >= kPauseHardResetMs;
}

void GameManager::startGame(unsigned long now, bool countAsFirstPress, bool switchAMacValid, bool switchBMacValid,
                            void (*sendToA)(const uint8_t*, size_t), void (*sendToB)(const uint8_t*, size_t)) {
  state_ = GameState::RUNNING;
  clearDots();
  // 不 reset 队列：颜色流在 IDLE 已备好，开局消费的就是当前队头
  queue_.ensureMinLength();
  lastMoveMs_ = now;
  lastComputerSpawnMs_ = now;
  expectedButton_ = 'A';

  // 拔河消消乐：索引 0 = B 端，numLeds-1 = E 端；+1 为「主机/压力」B→E，-1 为玩家 E→B。
  // 按 A 开局 = 从 Future Queue 取出队头「一次」，同时进入两轨：主机一颗 + 玩家第一发（同色对撞可消）。
  if (countAsFirstPress) {
    uint8_t c = queue_.consumeHeadForSpawn();
    addDot(0, +1, c);
    addDot(numLeds_ - 1, -1, c);
    setTurn('B', switchAMacValid, switchBMacValid, sendToA, sendToB);
  } else {
    uint8_t c = queue_.consumeHeadForSpawn();
    addDot(0, +1, c);
    setTurn('A', switchAMacValid, switchBMacValid, sendToA, sendToB);
  }
}

void GameManager::onButtonPress(char btn, unsigned long now, bool connReady, bool testGameMode, bool switchAMacValid,
                                bool switchBMacValid, void (*sendToA)(const uint8_t*, size_t),
                                void (*sendToB)(const uint8_t*, size_t)) {
  if (btn == 'A') {
    if (lastPressA_ && (now - lastPressA_) <= kRestartDoubleMs) {
      resetToIdle();
      setTurn('A', switchAMacValid, switchBMacValid, sendToA, sendToB);
    }
    lastPressA_ = now;
  } else {
    if (lastPressB_ && (now - lastPressB_) <= kRestartDoubleMs) {
      resetToIdle();
      setTurn('A', switchAMacValid, switchBMacValid, sendToA, sendToB);
    }
    lastPressB_ = now;
  }

  if (!connReady || !testGameMode) return;

  if (state_ == GameState::IDLE) {
    if (btn == 'A') startGame(now, true, switchAMacValid, switchBMacValid, sendToA, sendToB);
    return;
  }

  if (state_ != GameState::RUNNING) return;

  if (btn != expectedButton_) {
    if (btn == 'A' && switchAMacValid) sendToA((const uint8_t*)"BAD", 3);
    if (btn == 'B' && switchBMacValid) sendToB((const uint8_t*)"BAD", 3);
    return;
  }

  uint8_t c = queue_.consumeHeadForSpawn();
  addDot(numLeds_ - 1, -1, c);
  setTurn(btn == 'A' ? 'B' : 'A', switchAMacValid, switchBMacValid, sendToA, sendToB);
}

void GameManager::renderStrip(CRGB* leds, int numLeds, unsigned long now, bool connWaitingSearch) {
  (void)connWaitingSearch;
  if (state_ == GameState::IDLE) {
    unsigned long t = now;
    uint8_t hueOffset = (t / 50) % 256;
    for (int i = 0; i < numLeds; i++) {
      leds[i] = CHSV((hueOffset + (i * 256 / 24)) % 256, 255, 200);
    }
    return;
  }

  if (state_ == GameState::RUNNING) {
    memset(leds, 0, sizeof(CRGB) * (size_t)numLeds);
    for (int i = 0; i < kMaxDots; i++) {
      if (!dots_[i].active) continue;
      int p = dots_[i].position;
      if (p < 0 || p >= numLeds) continue;
      leds[p] = gamePaletteHsv(dots_[i].colorIndex);
    }
    return;
  }

  if (state_ == GameState::PAUSE) {
    uint8_t v = 60 + (uint8_t)(40 * (1 + sin(2 * 3.14159f * (float)now / 900.0f)) / 2);
    fill_solid(leds, numLeds, CHSV(43, 255, v));
    return;
  }

  if (state_ == GameState::WIN) {
    uint8_t v = 80 + (sin8(now / 6) / 2);
    fill_solid(leds, numLeds, CHSV(HUE_GREEN, 255, v));
  } else {
    uint8_t v = 80 + (sin8(now / 6) / 2);
    fill_solid(leds, numLeds, CHSV(HUE_RED, 255, v));
  }
}
