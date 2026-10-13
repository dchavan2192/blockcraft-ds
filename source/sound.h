// Sound effects and ambient music
#pragma once
#include "common.h"
#include "assets_gen.h"

void sound_init(void);
void sound_play(int sfx, int vol, int pan, int pitch_pct);
void sound_play_at(int sfx, int32_t x, int32_t y, int32_t z, int vol, int pitch_pct);
void sound_block(int block_id, bool is_step, int32_t x, int32_t y, int32_t z);
void music_tick(void);
