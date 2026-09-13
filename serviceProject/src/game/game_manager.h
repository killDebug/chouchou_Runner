#pragma once

#include "game_types.h"
#include "color_stream.h"
#include "game_palette.h"
#include <FastLED.h>
#include <stdint.h>

/** 游戏事件 → 主机播放对应蜂鸣器音效（main.cpp 注册回调；回调内勿阻塞） */
enum class GameEvent : uint8_t { START, SHOOT, COLLISION, BAD, WIN, LOSE };
typedef void (*GameEventFn)(GameEvent);

class GameManager {
 public:
  static constexpr int kMaxDots = 40;
  static constexpr unsigned long kPauseHardResetMs = 90000;

  void configure(int numLeds, int computerSpawnIntervalMs, int moveIntervalMs);
  void setComputerSpawnIntervalMs(int ms) { computerSpawnIntervalMs_ = ms; }
  /** 注册音效事件回调（main.cpp 的无阻塞蜂鸣器序列器） */
  void setEventCallback(GameEventFn cb) { eventCb_ = cb; }

  GameState state() const { return state_; }
  int dotCount() const { return dotCount_; }
  char expectedButton() const { return expectedButton_; }
  uint32_t colorWaveIndex() const { return colors_.waveIndex(); }
  uint8_t colorThemeIndex() const { return themeIndex_; }
  int pendingCatchUpCount() const { return pendingCount_; }
  unsigned long pauseStartedMs() const { return pauseStartedMs_; }

  /** 当前目标色：有积压=队头最旧（防守）；空=色流预告色（进攻，电脑下一拍跟同色） */
  uint8_t targetColorIndex() const;

  void resetToIdle();
  void startGame(unsigned long now, bool countAsFirstPress, bool switchAMacValid, bool switchBMacValid,
                 void (*sendToA)(const uint8_t*, size_t), void (*sendToB)(const uint8_t*, size_t));
  void forceIdle();
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

  /** 开关双击 RST：仅 IDLE / WIN / LOSE 允许；RUNNING / PAUSE 对战过程中拒绝 */
  bool allowSwitchDoubleTapReset() const;

  void renderStrip(CRGB* leds, int numLeds, unsigned long now, bool connWaitingSearch);

  const GameDot* dots() const { return dots_; }

 private:
  void fireEvent(GameEvent e);
  void clearDots();
  void compactDots();
  bool addDot(int position, int direction, uint8_t colorIndex, int16_t fixedHue = -1, uint8_t* outHue = nullptr);
  bool findOldestComputerHue(uint8_t colorIndex, uint8_t& hue) const;
  void spawnComputerDot(uint8_t colorIndex);
  void spawnPlayerDot(uint8_t colorIndex);
  void setTurn(char nextExpected, bool switchAMacValid, bool switchBMacValid, void (*sendToA)(const uint8_t*, size_t),
               void (*sendToB)(const uint8_t*, size_t));
  void setGameResult(GameState r, bool switchAMacValid, bool switchBMacValid, void (*sendToA)(const uint8_t*, size_t),
                     void (*sendToB)(const uint8_t*, size_t));
  void tickRunning(unsigned long now, bool switchAMacValid, bool switchBMacValid, void (*sendToA)(const uint8_t*, size_t),
                   void (*sendToB)(const uint8_t*, size_t));
  void clearPending();
  bool pushPending(uint8_t colorIndex);
  bool popPending(uint8_t& colorIndex);
  uint8_t peekPending() const;
  void clearPlayerLead();
  bool pushPlayerLead(uint8_t colorIndex, uint8_t hue);
  bool popPlayerLead(uint8_t& colorIndex, uint8_t& hue);
  uint8_t peekPlayerLead() const;
  uint8_t peekPlayerLeadHue() const;

  static constexpr int kMaxPending = 16;
  uint8_t pendingColors_[kMaxPending];
  int pendingCount_ = 0;
  uint8_t playerLeadColors_[kMaxPending];
  uint8_t playerLeadHues_[kMaxPending];
  int playerLeadCount_ = 0;
  bool playerShotThisTick_ = false;

  ColorStream colors_;
  GameDot dots_[kMaxDots];
  int dotCount_ = 0;
  uint32_t nextDotId_ = 1;
  GameState state_ = GameState::IDLE;
  char expectedButton_ = 'A';
  uint8_t themeIndex_ = 0;
  int numLeds_ = 460;
  int computerSpawnIntervalMs_ = 2000;
  int moveIntervalMs_ = 50;
  unsigned long lastMoveMs_ = 0;
  unsigned long lastComputerSpawnMs_ = 0;
  unsigned long pauseStartedMs_ = 0;
  unsigned long resultAtMs_ = 0;  // WIN/LOSE 时刻，用于胜利波浪动画
  GameEventFn eventCb_ = nullptr;
};

extern GameManager g_game;
