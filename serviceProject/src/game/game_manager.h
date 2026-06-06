#pragma once

#include "game_types.h"
#include "queue_manager.h"
#include <FastLED.h>
#include <stdint.h>

class GameManager {
 public:
  static constexpr int kMaxDots = 40;
  static constexpr unsigned long kPauseHardResetMs = 90000;

  void configure(int numLeds, int computerSpawnIntervalMs, int moveIntervalMs);
  void setComputerSpawnIntervalMs(int ms) { computerSpawnIntervalMs_ = ms; }

  GameState state() const { return state_; }
  int dotCount() const { return dotCount_; }
  char expectedButton() const { return expectedButton_; }
  size_t queueSize() const { return queue_.size(); }
  uint8_t queueHeadColor() const { return queue_.peekHead(); }
  unsigned long pauseStartedMs() const { return pauseStartedMs_; }

  void resetToIdle();
  void startGame(unsigned long now, bool countAsFirstPress, bool switchAMacValid, bool switchBMacValid,
                 void (*sendToA)(const uint8_t*, size_t), void (*sendToB)(const uint8_t*, size_t));
  void forceIdle();
  // RST / 双击回 IDLE 后恢复「下一轮由 A 开始」的 TA/RDY
  void syncTurnToIdle(bool switchAMacValid, bool switchBMacValid, void (*sendToA)(const uint8_t*, size_t),
                      void (*sendToB)(const uint8_t*, size_t));

  void tick(unsigned long now, bool connReady, bool testGameMode, bool switchAMacValid, bool switchBMacValid,
            void (*sendToA)(const uint8_t*, size_t), void (*sendToB)(const uint8_t*, size_t));

  void onButtonPress(char btn, unsigned long now, bool connReady, bool testGameMode, bool switchAMacValid,
                     bool switchBMacValid, void (*sendToA)(const uint8_t*, size_t),
                     void (*sendToB)(const uint8_t*, size_t));

  void enterPause(unsigned long now);
  void resumeFromPause(unsigned long now);
  bool shouldHardResetPause(unsigned long now) const;

  void renderStrip(CRGB* leds, int numLeds, unsigned long now, bool connWaitingSearch);

  const GameDot* dots() const { return dots_; }

 private:
  void clearDots();
  void compactDots();
  bool addDot(int position, int direction, uint8_t colorIndex);
  void setTurn(char nextExpected, bool switchAMacValid, bool switchBMacValid, void (*sendToA)(const uint8_t*, size_t),
               void (*sendToB)(const uint8_t*, size_t));
  void setGameResult(GameState r, bool switchAMacValid, bool switchBMacValid, void (*sendToA)(const uint8_t*, size_t),
                     void (*sendToB)(const uint8_t*, size_t));
  void tickRunning(unsigned long now, bool switchAMacValid, bool switchBMacValid, void (*sendToA)(const uint8_t*, size_t),
                   void (*sendToB)(const uint8_t*, size_t));
  // 暂不下发 COL（按钮灯环不同步）；仅保留接口便于以后打开
  void syncButtonsColor(unsigned long now, bool switchAMacValid, bool switchBMacValid,
                        void (*sendToA)(const uint8_t*, size_t), void (*sendToB)(const uint8_t*, size_t));

  QueueManager queue_;
  GameDot dots_[kMaxDots];
  int dotCount_ = 0;
  uint32_t nextDotId_ = 1;
  GameState state_ = GameState::IDLE;
  char expectedButton_ = 'A';
  int numLeds_ = 460;
  int computerSpawnIntervalMs_ = 2000;
  int moveIntervalMs_ = 50;
  unsigned long lastMoveMs_ = 0;
  unsigned long lastComputerSpawnMs_ = 0;
  unsigned long pauseStartedMs_ = 0;

  static constexpr unsigned long kRestartDoubleMs = 450;
  unsigned long lastPressA_ = 0;
  unsigned long lastPressB_ = 0;
};

extern GameManager g_game;
