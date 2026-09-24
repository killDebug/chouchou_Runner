# 音效包与主机状态对应

选用目录：`pcm8k/`（已从 `esp32-pcm8k.tar.gz` 解压）

板上是 **ESP32-S3 N8R8（8MB Flash）**，还要留双 OTA。三套解压体积：pcm16k **5.30MB** 放不下；pcm8k **2.77MB** 能进大约 3.5MB 的文件系统；adpcm16k 更小，但要固件解码。N8R8 用 **pcm8k**。

格式：单声道 16bit。人声 `voice/` 为 **8kHz**，短音效 `sfx/` 为 **16kHz**。共 149 条（人声 113，音效 36）。

角色对照（按编号，和 `chouchouGameVoice/游戏需要的音效.md` 同号）：

- **开关 A = 闪闪 `ss`**
- **开关 B = 亮亮 `ll`**

随机：同一格里的 `*_01`…任抽一条。胜利先播 `06` 再随机 `20`；失败先播 `07` 再随机 `21`。调速先播 `08_beep` 再播对应人声。断线恢复后重播当前回合的 `12`。

固件里还没有孩子名字，开机只用 `01_boot_noname_*`，`01_boot_named_*` 先留着。

## 已经有蜂鸣器，换成这些文件

| 现在 | 代码 | 播放 |
|------|------|------|
| 开机 | `setup()` → `playStartupBeep()` | `sfx/01_boot_sfx_chime.wav` 或 `_02`，接着 `voice/01_boot_noname_*.wav` |
| 开局 | `GameEvent::START`（就绪后按开关 A） | `voice/02_start_*.wav` |
| 玩家发射 | `GameEvent::SHOOT` | 轮到 A：`sfx/03_shot_ss_*.wav`；轮到 B：`sfx/03_shot_ll_*.wav` |
| 同色消除 | `GameEvent::COLLISION` | `sfx/04_clear_*.wav` |
| 按错 | `GameEvent::BAD` | `voice/05_wrong_*.wav` |
| 胜利 | `GameEvent::WIN` / `GameState::WIN` | `sfx/06_win_sfx_*.wav`，接着 `voice/20_win_line_*.wav` |
| 失败 | `GameEvent::LOSE` / `GameState::LOSE` | `sfx/07_lose_sfx_*.wav`，接着 `voice/21_lose_line_*.wav` |
| 加速 | `GAME_SPEED_UP` → `buzzerSpeedFeedback()` | `sfx/08_beep.wav`，接着 `voice/08_speed_up_*.wav`；已经是最高档再用 `08_speed_fast_*.wav` |
| 减速 | `GAME_SPEED_DOWN` | `sfx/08_beep.wav`，接着 `voice/08_speed_slow_*.wav` |
| 重置 | `hostResetGame()`（功能键双击 SET） | `sfx/08_beep.wav`，接着 `voice/08_reset_*.wav` |

## 灯已经有了，声音还没接

| 现在 | 代码 | 播放 |
|------|------|------|
| A 首次上线 | 回 `ACK_A` | `voice/09_online_ss_*.wav` |
| B 首次上线 | 回 `ACK_B` | `voice/10_online_ll_*.wav` |
| 两端就绪 | `ConnState::CONN_READY` | `voice/11_ready_*.wav` |
| 轮到 A | `setTurn('A')` 下发 `TA` | `sfx/12_turn_ding.wav`，接着 `voice/12_turn_ss_*.wav` |
| 轮到 B | `setTurn('B')` 下发 `TB` | `sfx/12_turn_ding.wav`，接着 `voice/12_turn_ll_*.wav` |
| 断线暂停 | `GameState::PAUSE` / `enterPause()` | `voice/13_pause_*.wav`；久停未回再用 `13_pause_nudge_*.wav` |
| 恢复继续 | `resumeFromPause()` | `voice/14_resume_*.wav`，然后重播上面的 `12` |
| 回到搜索 | 双端都掉，`ConnState::CONN_WAITING_A` | `voice/15_search_*.wav`；还在找再用 `15_search_nudge_01.wav` |
| 电脑出点 | `spawnComputerDot()` | `sfx/16_cpu_shot_*.wav`（很轻，可关） |

`GameState::IDLE` 不单独播。`RUNNING` 只在发射、消除、轮转时播。

## 素材在，固件还没有触发点

| 建议 | 播放 | 说明 |
|------|------|------|
| 连消 | `voice/17_combo_first_*.wav` 第一次；`17_combo_3_*` / `_5_*` / `_7_*` / `_10_*` 按连消数 | 程序里没有连消计数 |
| 快赢 | `voice/18_almost_win_*.wav`；催促用 `18_almost_win_n*.wav` | 没有「积压快清空」事件 |
| 快输 | `voice/19_almost_lose_*.wav` | 没有「电脑点快到头」事件 |
| 开局倒计时 | `voice/22_countdown_3.wav` `_2` `_1`，或整句 `22_countdown_voice.wav`；短音 `sfx/22_countdown_sfx_*.wav` | 旧 `CD` 已删，先不接 |

OTA、配网、WiFi 连上继续只亮灯，不配人声。

## 01 开机

### 音效 `pcm8k/sfx/`

- `01_boot_sfx_chime.wav`
- `01_boot_sfx_chime_02.wav`

### 人声 `pcm8k/voice/`

- `01_boot_noname_01.wav`（现用）
- `01_boot_noname_02.wav`
- `01_boot_noname_03.wav`
- `01_boot_named_01.wav`（暂不用）
- `01_boot_named_02.wav`
- `01_boot_named_03.wav`

## 02 开局口号

### 人声 `pcm8k/voice/`

- `02_start_01.wav`
- `02_start_02.wav`
- `02_start_03.wav`
- `02_start_04.wav`
- `02_start_05.wav`

## 03 玩家发射

### 音效 `pcm8k/sfx/`

开关 A（闪闪）：

- `03_shot_ss_01.wav`
- `03_shot_ss_02.wav`
- `03_shot_ss_03.wav`
- `03_shot_ss_04.wav`
- `03_shot_ss_05.wav`

开关 B（亮亮）：

- `03_shot_ll_01.wav`
- `03_shot_ll_02.wav`
- `03_shot_ll_03.wav`
- `03_shot_ll_04.wav`
- `03_shot_ll_05.wav`

## 04 同色消除

### 音效 `pcm8k/sfx/`

- `04_clear_01.wav`
- `04_clear_02.wav`
- `04_clear_03.wav`
- `04_clear_04.wav`
- `04_clear_05.wav`
- `04_clear_06.wav`
- `04_clear_07.wav`
- `04_clear_08.wav`

## 05 按错

### 人声 `pcm8k/voice/`

- `05_wrong_01.wav`
- `05_wrong_02.wav`
- `05_wrong_03.wav`
- `05_wrong_04.wav`
- `05_wrong_05.wav`

## 06 胜利音效底

### 音效 `pcm8k/sfx/`

- `06_win_sfx_01.wav`
- `06_win_sfx_02.wav`
- `06_win_sfx_03.wav`

## 07 失败音效底

### 音效 `pcm8k/sfx/`

- `07_lose_sfx_01.wav`
- `07_lose_sfx_02.wav`
- `07_lose_sfx_03.wav`

## 08 调速重置

### 音效 `pcm8k/sfx/`

- `08_beep.wav`

### 人声 `pcm8k/voice/`

- `08_speed_up_01.wav`
- `08_speed_up_02.wav`
- `08_speed_fast_01.wav`
- `08_speed_fast_02.wav`
- `08_speed_slow_01.wav`
- `08_speed_slow_02.wav`
- `08_reset_01.wav`
- `08_reset_02.wav`

## 09 开关 A 上线（闪闪）

### 人声 `pcm8k/voice/`

- `09_online_ss_01.wav`
- `09_online_ss_02.wav`
- `09_online_ss_03.wav`
- `09_online_ss_04.wav`

## 10 开关 B 上线（亮亮）

### 人声 `pcm8k/voice/`

- `10_online_ll_01.wav`
- `10_online_ll_02.wav`
- `10_online_ll_03.wav`
- `10_online_ll_04.wav`

## 11 两端就绪

### 人声 `pcm8k/voice/`

- `11_ready_01.wav`
- `11_ready_02.wav`
- `11_ready_03.wav`
- `11_ready_04.wav`

## 12 轮到谁

### 音效 `pcm8k/sfx/`

- `12_turn_ding.wav`

### 人声 `pcm8k/voice/`

开关 A：

- `12_turn_ss_01.wav`
- `12_turn_ss_02.wav`
- `12_turn_ss_03.wav`

开关 B：

- `12_turn_ll_01.wav`
- `12_turn_ll_02.wav`
- `12_turn_ll_03.wav`

## 13 断线暂停

### 人声 `pcm8k/voice/`

- `13_pause_01.wav`
- `13_pause_02.wav`
- `13_pause_03.wav`
- `13_pause_04.wav`
- `13_pause_05.wav`
- `13_pause_06.wav`
- `13_pause_nudge_01.wav`
- `13_pause_nudge_02.wav`

## 14 恢复继续

### 人声 `pcm8k/voice/`

- `14_resume_01.wav`
- `14_resume_02.wav`
- `14_resume_03.wav`

## 15 回搜索

### 人声 `pcm8k/voice/`

- `15_search_01.wav`
- `15_search_02.wav`
- `15_search_03.wav`
- `15_search_nudge_01.wav`

## 16 电脑出点

### 音效 `pcm8k/sfx/`

- `16_cpu_shot_01.wav`
- `16_cpu_shot_02.wav`
- `16_cpu_shot_03.wav`
- `16_cpu_shot_04.wav`
- `16_cpu_shot_05.wav`

## 17 连消里程碑

### 人声 `pcm8k/voice/`

- `17_combo_first_01.wav`
- `17_combo_first_02.wav`
- `17_combo_first_03.wav`
- `17_combo_first_04.wav`
- `17_combo_3_01.wav`
- `17_combo_3_02.wav`
- `17_combo_3_03.wav`
- `17_combo_3_04.wav`
- `17_combo_5_01.wav`
- `17_combo_5_02.wav`
- `17_combo_5_03.wav`
- `17_combo_5_04.wav`
- `17_combo_7_01.wav`
- `17_combo_7_02.wav`
- `17_combo_7_03.wav`
- `17_combo_10_01.wav`
- `17_combo_10_02.wav`
- `17_combo_10_03.wav`
- `17_combo_10_04.wav`

## 18 快赢

### 人声 `pcm8k/voice/`

- `18_almost_win_01.wav`
- `18_almost_win_02.wav`
- `18_almost_win_03.wav`
- `18_almost_win_n1.wav`
- `18_almost_win_n2.wav`
- `18_almost_win_n3.wav`
- `18_almost_win_n4.wav`
- `18_almost_win_n5.wav`

## 19 快输

### 人声 `pcm8k/voice/`

- `19_almost_lose_01.wav`
- `19_almost_lose_02.wav`
- `19_almost_lose_03.wav`
- `19_almost_lose_04.wav`
- `19_almost_lose_05.wav`

## 20 胜利表扬池

### 人声 `pcm8k/voice/`

- `20_win_line_01.wav`
- `20_win_line_02.wav`
- `20_win_line_03.wav`
- `20_win_line_04.wav`
- `20_win_line_05.wav`
- `20_win_line_06.wav`
- `20_win_line_07.wav`
- `20_win_line_08.wav`
- `20_win_line_09.wav`
- `20_win_line_10.wav`

## 21 失败安慰池

### 人声 `pcm8k/voice/`

- `21_lose_line_01.wav`
- `21_lose_line_02.wav`
- `21_lose_line_03.wav`
- `21_lose_line_04.wav`
- `21_lose_line_05.wav`
- `21_lose_line_06.wav`
- `21_lose_line_07.wav`
- `21_lose_line_08.wav`
- `21_lose_line_09.wav`
- `21_lose_line_10.wav`

## 22 开局倒计时

### 音效 `pcm8k/sfx/`

- `22_countdown_sfx_1.wav`
- `22_countdown_sfx_2.wav`
- `22_countdown_sfx_3.wav`

### 人声 `pcm8k/voice/`

- `22_countdown_3.wav`
- `22_countdown_2.wav`
- `22_countdown_1.wav`
- `22_countdown_voice.wav`
