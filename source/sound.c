// Sound effects and gentle generative ambient music
#include "sound.h"
#include "game.h"
#include "blocks.h"
#include "render.h"
#include "world.h"
#include "sfx_bin.h"

static bool sound_ok;

void sound_init(void)
{
    soundEnable();
    sound_ok = true;
}

void sound_play(int sfx, int vol, int pan, int pitch_pct)
{
    if (!sound_ok || sfx < 0 || sfx >= SFX_COUNT || settings.volume <= 0)
        return;
    int v = vol * settings.volume / 10;
    if (v <= 0)
        return;
    if (v > 127)
        v = 127;
    uint32_t off = sfx_table[sfx][0], len = sfx_table[sfx][1];
    int freq = SFX_RATE * pitch_pct / 100;
    if (freq < 1000)
        freq = 1000;
    if (freq > 60000)
        freq = 60000;
    soundPlaySample(sfx_bin + off, SoundFormat_8Bit, len, freq, v, iclamp(pan, 0, 127), false, 0);
}

void sound_play_at(int sfx, int32_t x, int32_t y, int32_t z, int vol, int pitch_pct)
{
    int32_t dx = world_wrap_delta_fx(x - cam.x);
    int32_t dy = y - cam.y;
    int32_t dz = world_wrap_delta_fx(z - cam.z);
    int32_t ad = iabs(dx) + iabs(dy) + iabs(dz);
    int32_t maxd = FX(20);
    if (ad >= maxd)
        return;
    int v = vol * (maxd - ad) / maxd;
    // pan from the camera's right vector
    int32_t rx = cosLerp(cam.yaw), rz = sinLerp(cam.yaw);
    int32_t side = fxmul(dx, rx) + fxmul(dz, rz);
    int pan = 64 + (int)(side * 48 / (ad + FX(2)));
    sound_play(sfx, v, pan, pitch_pct);
}

void sound_block(int block_id, bool is_step, int32_t x, int32_t y, int32_t z)
{
    static const int8_t snd_map[] = { SFX_DIG_STONE, SFX_DIG_WOOD, SFX_DIG_GRASS, SFX_DIG_SAND, SFX_DIG_GLASS, SFX_DIG_CLOTH, -1 };
    int st = blocks[block_id].sound;
    if (st >= SND_NONE)
        return;
    int sfx = snd_map[st];
    if (is_step && st == SND_GLASS)
        sfx = SFX_DIG_STONE;
    int vol = is_step ? 40 : 110;
    int pitch = is_step ? 120 : 85 + (rng_next() & 15);
    sound_play_at(sfx, x, y, z, vol, pitch);
}

// ---------------------------------------------------------------------------
// Ambient music: slow pentatonic phrases using a synthesized piano note
// ---------------------------------------------------------------------------

static int music_wait = 600;
static int phrase_len, phrase_pos, note_wait;
static uint8_t phrase[12];
static uint8_t phrase_dur[12];

static const uint16_t scale_pct[] = { 50, 56, 63, 75, 84, 100, 112, 125, 150, 168, 200 };

void music_tick(void)
{
    if (!settings.music || settings.volume <= 0)
        return;
    if (phrase_len) {
        if (--note_wait > 0)
            return;
        int n = phrase[phrase_pos];
        sound_play(SFX_NOTE, 55, 40 + (int)(rng_next() & 47), scale_pct[n]);
        // occasional harmony
        if ((rng_next() & 3) == 0 && n >= 3)
            sound_play(SFX_NOTE, 35, 64, scale_pct[n - 3]);
        note_wait = phrase_dur[phrase_pos];
        phrase_pos++;
        if (phrase_pos >= phrase_len) {
            phrase_len = 0;
            music_wait = 20 * (40 + (int)(rng_next() % 80));
        }
        return;
    }
    if (--music_wait > 0)
        return;
    // compose a phrase: random walk on the scale
    phrase_len = 5 + (int)(rng_next() % 7);
    int n = 3 + (int)(rng_next() % 5);
    for (int i = 0; i < phrase_len; i++) {
        phrase[i] = n;
        static const uint8_t durs[] = { 10, 10, 15, 20, 20, 30 };
        phrase_dur[i] = durs[rng_next() % 6];
        int step = (int)(rng_next() % 5) - 2;
        n = iclamp(n + step, 0, 10);
    }
    phrase_pos = 0;
    note_wait = 1;
}
