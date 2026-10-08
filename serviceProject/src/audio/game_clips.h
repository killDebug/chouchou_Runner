#pragma once
#include <stdint.h>
// 自动生成：喇叭游戏音效（单声道 s16le，无 WAV 头）。勿手改。
enum AmpClipGroup : uint8_t {
  AMP_G_START,
  AMP_G_CLEAR,
  AMP_G_WRONG,
  AMP_G_WIN_SFX,
  AMP_G_WIN_VOICE,
  AMP_G_LOSE_SFX,
  AMP_G_LOSE_VOICE,
  AMP_G_BEEP,
  AMP_G_RESET,
  AMP_G_SPEED_UP,
  AMP_G_SPEED_SLOW,
  AMP_G_SPEED_FAST,
  AMP_G_CHEER,
  AMP_G_ONLINE_A,
  AMP_G_ONLINE_B,
  AMP_G_READY,
  AMP_G_SHOT,
  AMP_G_COUNT
};

struct AmpClipRef {
  uint32_t offset;
  uint32_t samples;
  uint16_t rate;
};

extern const uint8_t game_clips_bin_start[];
extern const uint8_t game_clips_bin_end[];

static const AmpClipRef kAmpClips[] = {
  { 0, 4402, 8000 }, // 02_start_01.wav
  { 8804, 5053, 8000 }, // 02_start_02.wav
  { 18912, 4799, 8000 }, // 02_start_03.wav
  { 28512, 9680, 8000 }, // 02_start_04.wav
  { 47872, 7136, 8000 }, // 02_start_05.wav
  { 62144, 2091, 16000 }, // 04_clear_01.wav
  { 66328, 2190, 16000 }, // 04_clear_02.wav
  { 70708, 2038, 16000 }, // 04_clear_03.wav
  { 74784, 5158, 16000 }, // 04_clear_04.wav
  { 85100, 4725, 16000 }, // 04_clear_05.wav
  { 94552, 3353, 16000 }, // 04_clear_06.wav
  { 101260, 4657, 16000 }, // 04_clear_07.wav
  { 110576, 6620, 16000 }, // 04_clear_08.wav
  { 123816, 4317, 8000 }, // 05_wrong_01.wav
  { 132452, 3247, 8000 }, // 05_wrong_02.wav
  { 138948, 7965, 8000 }, // 05_wrong_03.wav
  { 154880, 8002, 8000 }, // 05_wrong_04.wav
  { 170884, 11162, 8000 }, // 05_wrong_05.wav
  { 193208, 14393, 16000 }, // 06_win_sfx_01.wav
  { 221996, 16871, 16000 }, // 06_win_sfx_02.wav
  { 255740, 18914, 16000 }, // 06_win_sfx_03.wav
  { 293568, 9474, 8000 }, // 20_win_line_01.wav
  { 312516, 9842, 8000 }, // 20_win_line_02.wav
  { 332200, 7842, 8000 }, // 20_win_line_03.wav
  { 347884, 6714, 8000 }, // 20_win_line_04.wav
  { 361312, 17049, 8000 }, // 20_win_line_05.wav
  { 395412, 8715, 8000 }, // 20_win_line_06.wav
  { 412844, 14525, 8000 }, // 20_win_line_07.wav
  { 441896, 16254, 8000 }, // 20_win_line_08.wav
  { 474404, 10334, 8000 }, // 20_win_line_09.wav
  { 495072, 12759, 8000 }, // 20_win_line_10.wav
  { 520592, 5181, 16000 }, // 07_lose_sfx_01.wav
  { 530956, 3790, 16000 }, // 07_lose_sfx_02.wav
  { 538536, 2512, 16000 }, // 07_lose_sfx_03.wav
  { 543560, 13109, 8000 }, // 21_lose_line_01.wav
  { 569780, 17781, 8000 }, // 21_lose_line_02.wav
  { 605344, 8782, 8000 }, // 21_lose_line_03.wav
  { 622908, 13425, 8000 }, // 21_lose_line_04.wav
  { 649760, 20353, 8000 }, // 21_lose_line_05.wav
  { 690468, 17763, 8000 }, // 21_lose_line_06.wav
  { 725996, 11585, 8000 }, // 21_lose_line_07.wav
  { 749168, 13913, 8000 }, // 21_lose_line_08.wav
  { 776996, 19570, 8000 }, // 21_lose_line_09.wav
  { 816136, 10624, 8000 }, // 21_lose_line_10.wav
  { 837384, 3304, 16000 }, // 08_beep.wav
  { 843992, 7732, 8000 }, // 08_reset_01.wav
  { 859456, 14515, 8000 }, // 08_reset_02.wav
  { 888488, 12854, 8000 }, // 08_speed_up_01.wav
  { 914196, 14247, 8000 }, // 08_speed_up_02.wav
  { 942692, 17562, 8000 }, // 08_speed_slow_01.wav
  { 977816, 16324, 8000 }, // 08_speed_slow_02.wav
  { 1010464, 18984, 8000 }, // 08_speed_fast_01.wav
  { 1048432, 15646, 8000 }, // 08_speed_fast_02.wav
  { 1079724, 5291, 8000 }, // 17_combo_first_02.wav
  { 1090308, 6348, 8000 }, // 09_online_ss_01.wav
  { 1103004, 5526, 8000 }, // 10_online_ll_01.wav
  { 1114056, 18418, 8000 }, // 11_ready_02.wav
  { 1150892, 3634, 16000 }, // 03_shot_ss_01.wav
};
static const uint16_t kAmpClipTotal = 58;
static const uint16_t kAmpGroupBegin[AMP_G_COUNT] = {0, 5, 13, 18, 21, 31, 34, 44, 45, 47, 49, 51, 53, 54, 55, 56, 57};
static const uint16_t kAmpGroupCount[AMP_G_COUNT] = {5, 8, 5, 3, 10, 3, 10, 1, 2, 2, 2, 2, 1, 1, 1, 1, 1};
