/* 喇叭音效 PCM 嵌入固件 */
.section .rodata
.align 4
.global game_clips_bin_start
.global game_clips_bin_end
game_clips_bin_start:
.incbin "src/audio/game_clips.bin"
.align 4
game_clips_bin_end:
.byte 0
