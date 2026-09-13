#include "game_manager.h"
#include "collision_manager.h"
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

void GameManager::fireEvent(GameEvent e) {
  if (eventCb_) eventCb_(e);
}

uint8_t GameManager::targetColorIndex() const {
  if (pendingCount_ > 0) return peekPending();
  return colors_.currentColor();
}

uint8_t GameManager::targetHue() const {
  if (pendingCount_ > 0) {
    uint8_t hue = 0;
    if (findOldestComputerHue(peekPending(), hue)) return hue;
  }
  return colors_.currentHue();
}

void GameManager::consumeColor() {
  uint8_t avoid[kMaxDots];
  int n = 0;
  for (int i = 0; i < kMaxDots; i++) {
    if (!dots_[i].active) continue;
    avoid[n++] = dots_[i].hue;
  }
  colors_.advanceWave(avoid, n);
}

// ---------- pending FIFO：电脑已出、等玩家防守跟色的颜色队列 ----------

void GameManager::clearPending() {
  pendingCount_ = 0;
}

bool GameManager::pushPending(uint8_t colorIndex) {
  if (pendingCount_ >= kMaxPending) return false;
  pendingColors_[pendingCount_++] = colorIndex;
  return true;
}

bool GameManager::popPending(uint8_t& colorIndex) {
  if (pendingCount_ <= 0) return false;
  colorIndex = pendingColors_[0];
  if (pendingCount_ > 1) {
    memmove(pendingColors_, pendingColors_ + 1, (size_t)(pendingCount_ - 1));
  }
  pendingCount_--;
  return true;
}

uint8_t GameManager::peekPending() const {
  if (pendingCount_ <= 0) return 0;
  return pendingColors_[0];
}

void GameManager::clearPlayerLead() {
  playerLeadCount_ = 0;
}

bool GameManager::pushPlayerLead(uint8_t colorIndex, uint8_t hue) {
  if (playerLeadCount_ >= kMaxPending) return false;
  playerLeadColors_[playerLeadCount_] = colorIndex;
  playerLeadHues_[playerLeadCount_] = hue;
  playerLeadCount_++;
  return true;
}

bool GameManager::popPlayerLead(uint8_t& colorIndex, uint8_t& hue) {
  if (playerLeadCount_ <= 0) return false;
  colorIndex = playerLeadColors_[0];
  hue = playerLeadHues_[0];
  if (playerLeadCount_ > 1) {
    memmove(playerLeadColors_, playerLeadColors_ + 1, (size_t)(playerLeadCount_ - 1));
    memmove(playerLeadHues_, playerLeadHues_ + 1, (size_t)(playerLeadCount_ - 1));
  }
  playerLeadCount_--;
  return true;
}

uint8_t GameManager::peekPlayerLead() const {
  if (playerLeadCount_ <= 0) return 0;
  return playerLeadColors_[0];
}

uint8_t GameManager::peekPlayerLeadHue() const {
  if (playerLeadCount_ <= 0) return 0;
  return playerLeadHues_[0];
}

// ---------- 场上光点 ----------

void GameManager::clearDots() {
  dotCount_ = 0;
  for (int i = 0; i < kMaxDots; i++) dots_[i].active = false;
  clearPending();
  clearPlayerLead();
  playerShotThisTick_ = false;
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

bool GameManager::findOldestComputerHue(uint8_t colorIndex, uint8_t& hue) const {
  int bestPos = -1;
  bool found = false;
  for (int i = 0; i < kMaxDots; i++) {
    if (!dots_[i].active || dots_[i].direction != +1) continue;
    if (dots_[i].colorIndex != colorIndex) continue;
    if (!found || dots_[i].position > bestPos) {
      found = true;
      bestPos = dots_[i].position;
      hue = dots_[i].hue;
    }
  }
  return found;
}

bool GameManager::addDot(int position, int direction, uint8_t colorIndex, int16_t fixedHue, uint8_t* outHue) {
  if (dotCount_ >= kMaxDots) return false;
  for (int i = 0; i < kMaxDots; i++) {
    if (!dots_[i].active) {
      dots_[i].id = nextDotId_++;
      dots_[i].colorIndex = colorIndex;
      if (fixedHue >= 0) {
        dots_[i].hue = (uint8_t)fixedHue;
      } else {
        dots_[i].hue = colors_.currentHue();
      }
      if (outHue) *outHue = dots_[i].hue;
      dots_[i].position = position;
      dots_[i].direction = (int8_t)direction;
      dots_[i].active = true;
      dotCount_++;
      return true;
    }
  }
  return false;
}

void GameManager::spawnComputerDot(uint8_t colorIndex) {
  addDot(0, +1, colorIndex);
}

void GameManager::spawnPlayerDot(uint8_t colorIndex) {
  addDot(numLeds_ - 1, -1, colorIndex);
}

// ---------- 回合与结果同步 ----------

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
  resultAtMs_ = millis();
  if (r == GameState::WIN) {
    if (switchAMacValid) sendToA((const uint8_t*)"WIN", 3);
    if (switchBMacValid) sendToB((const uint8_t*)"WIN", 3);
    fireEvent(GameEvent::WIN);
  } else if (r == GameState::LOSE) {
    if (switchAMacValid) sendToA((const uint8_t*)GOF_MSG, 3);
    if (switchBMacValid) sendToB((const uint8_t*)GOF_MSG, 3);
    fireEvent(GameEvent::LOSE);
  }
}

// ---------- 主循环 ----------

void GameManager::tickRunning(unsigned long now, bool switchAMacValid, bool switchBMacValid, void (*sendToA)(const uint8_t*, size_t),
                              void (*sendToB)(const uint8_t*, size_t)) {
  // 电脑按间隔出点：有玩家抢先则跟同色同色相；否则取色流。玩家刚出手的同一帧不再出新色。
  if (now - lastComputerSpawnMs_ >= (unsigned long)computerSpawnIntervalMs_) {
    if (playerLeadCount_ > 0) {
      uint8_t c = peekPlayerLead();
      uint8_t h = peekPlayerLeadHue();
      if (addDot(0, +1, c, (int16_t)h)) {
        popPlayerLead(c, h);
        lastComputerSpawnMs_ = now;
      }
    } else if (!playerShotThisTick_) {
      uint8_t c = colors_.currentColor();
      uint8_t h = colors_.currentHue();
      if (addDot(0, +1, c, (int16_t)h)) {
        pushPending(c);
        consumeColor();
        lastComputerSpawnMs_ = now;
      }
    }
  }
  playerShotThisTick_ = false;

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

  int dotsBefore = dotCount_;
  gameResolveCollisions(dots_, kMaxDots, dotCount_, prevPos);
  compactDots();
  if (dotCount_ < dotsBefore) {
    fireEvent(GameEvent::COLLISION);
  }

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
  if (state_ == GameState::RUNNING) {
    tickRunning(now, switchAMacValid, switchBMacValid, sendToA, sendToB);
  }
  // IDLE/PAUSE 的开关颜色同步由 main.cpp 的 COL 事件驱动负责，这里无需周期下发
}

// ---------- 状态切换 ----------

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

bool GameManager::allowSwitchDoubleTapReset() const {
  // PAUSE 允许：断链暂停时对局已停摆，双击重置是合理的脱困手段
  return state_ == GameState::IDLE || state_ == GameState::WIN || state_ == GameState::LOSE ||
         state_ == GameState::PAUSE;
}

void GameManager::startGame(unsigned long now, bool countAsFirstPress, bool switchAMacValid, bool switchBMacValid,
                            void (*sendToA)(const uint8_t*, size_t), void (*sendToB)(const uint8_t*, size_t)) {
  (void)countAsFirstPress;
  colors_.newSession();

  state_ = GameState::RUNNING;
  clearDots();
  lastMoveMs_ = now;
  // 开局立刻出第一颗电脑点并入防守队列，避免「间隔未到、积压为空」时玩家被当成进攻乱发射
  uint8_t first = colors_.currentColor();
  uint8_t firstHue = colors_.currentHue();
  if (addDot(0, +1, first, (int16_t)firstHue)) {
    pushPending(first);
    consumeColor();
  }
  lastComputerSpawnMs_ = now;  // 下一颗仍按完整间隔
  // A 按下开局，按交替规则把回合交给 B
  setTurn('B', switchAMacValid, switchBMacValid, sendToA, sendToB);
  fireEvent(GameEvent::START);
}

// ---------- 玩家按键：攻防发射 ----------

void GameManager::onButtonPress(char btn, unsigned long now, bool connReady, bool testGameMode, bool switchAMacValid,
                                bool switchBMacValid, void (*sendToA)(const uint8_t*, size_t),
                                void (*sendToB)(const uint8_t*, size_t)) {
  (void)now;
  if (!connReady || !testGameMode) return;

  if (state_ == GameState::IDLE) {
    if (btn == 'A') startGame(now, true, switchAMacValid, switchBMacValid, sendToA, sendToB);
    return;
  }

  if (state_ == GameState::WIN || state_ == GameState::LOSE) {
    resetToIdle();
    syncTurnToIdle(switchAMacValid, switchBMacValid, sendToA, sendToB);
    return;
  }

  if (state_ != GameState::RUNNING) return;

  // 严格 A/B 交替：非期望按键忽略 + BAD 惩罚（灯环闪红 + 警告音）
  if (btn != expectedButton_) {
    if (btn == 'A' && switchAMacValid) sendToA((const uint8_t*)"BAD", 3);
    if (btn == 'B' && switchBMacValid) sendToB((const uint8_t*)"BAD", 3);
    fireEvent(GameEvent::BAD);
    return;
  }

  uint8_t c = 0;
  int16_t hue = -1;
  const bool defending = pendingCount_ > 0;
  if (defending) {
    c = peekPending();
    uint8_t matchedHue = 0;
    if (findOldestComputerHue(c, matchedHue)) hue = (int16_t)matchedHue;
  } else {
    if (playerLeadCount_ >= kMaxPending) return;
    c = colors_.currentColor();
    hue = (int16_t)colors_.currentHue();
  }
  uint8_t spawnedHue = 0;
  if (!addDot(numLeds_ - 1, -1, c, hue, &spawnedHue)) return;
  if (defending) {
    popPending(c);
  } else {
    consumeColor();
    pushPlayerLead(c, spawnedHue);
  }
  playerShotThisTick_ = true;
  setTurn(btn == 'A' ? 'B' : 'A', switchAMacValid, switchBMacValid, sendToA, sendToB);
  fireEvent(GameEvent::SHOOT);
}

// ---------- 渲染 ----------

void GameManager::renderStrip(CRGB* leds, int numLeds, unsigned long now, bool connWaitingSearch) {
  (void)connWaitingSearch;
  if (state_ == GameState::IDLE) {
    // 待机：全彩虹全长慢速流动
    fill_rainbow(leds, numLeds, (uint8_t)((now / 60) & 0xFF), 1);
    return;
  }

  if (state_ == GameState::RUNNING) {
    memset(leds, 0, sizeof(CRGB) * (size_t)numLeds);
    for (int i = 0; i < kMaxDots; i++) {
      if (!dots_[i].active) continue;
      int p = dots_[i].position;
      if (p < 0 || p >= numLeds) continue;
      leds[p] = CHSV(dots_[i].hue, 255, 255);
    }
    return;
  }

  if (state_ == GameState::PAUSE) {
    uint8_t v = 60 + (uint8_t)(40 * (1 + sin(2 * 3.14159f * (float)now / 900.0f)) / 2);
    fill_solid(leds, numLeds, CHSV(43, 255, v));
    return;
  }

  if (state_ == GameState::WIN) {
    // 玩家胜利：从 0 端和末端同时向中间爆发全彩波浪，汇合后全场高亮炫彩闪烁
    unsigned long e = now - resultAtMs_;
    int half = numLeds / 2;
    int front = (int)(e / 4);  // 约 0.9s 两端波浪在中间汇合（460 颗）
    bool met = front >= half;
    for (int i = 0; i < numLeds; i++) {
      int d = (i < half) ? i : (numLeds - 1 - i);
      if (met || d <= front) {
        uint8_t hue = (uint8_t)((uint8_t)((uint32_t)i * 255 / (uint32_t)(numLeds > 1 ? numLeds : 1)) +
                                (uint8_t)(now / 12));
        leds[i] = CHSV(hue, 255, 255);
      } else {
        leds[i] = CRGB::Black;
      }
    }
    if (met && random8() < 80) {
      leds[random16(numLeds)] = CRGB::White;  // 汇合后的炫彩闪烁点缀
    }
    return;
  }

  // LOSE：全红呼吸
  uint8_t v = 80 + (sin8(now / 6) / 2);
  fill_solid(leds, numLeds, CHSV(HUE_RED, 255, v));
}
