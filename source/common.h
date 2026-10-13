// BlockCraft DS - shared definitions
#pragma once

#include <nds.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#define GAME_NAME "BlockCraft DS"
#define GAME_VERSION "1.0"

// ---------------------------------------------------------------------------
// Fixed point (20.12) helpers. 1 block = 4096 units.
// ---------------------------------------------------------------------------
#define FX_SHIFT 12
#define FX_ONE   (1 << FX_SHIFT)
#define FX(n)    ((int32_t)((n) * FX_ONE))
#define FXI(n)   ((int32_t)(n) << FX_SHIFT)
#define FX_FLOOR(v) ((v) >> FX_SHIFT)

static inline int32_t fxmul(int32_t a, int32_t b) { return (int32_t)(((int64_t)a * b) >> FX_SHIFT); }
static inline int32_t fxdiv(int32_t a, int32_t b) { return (int32_t)(((int64_t)a << FX_SHIFT) / b); }
static inline int32_t iabs(int32_t v) { return v < 0 ? -v : v; }
static inline int32_t imin(int32_t a, int32_t b) { return a < b ? a : b; }
static inline int32_t imax(int32_t a, int32_t b) { return a > b ? a : b; }
static inline int32_t iclamp(int32_t v, int32_t a, int32_t b) { return v < a ? a : v > b ? b : v; }

// Angles use the libnds convention: 32768 = full turn.
#define ANG_FULL 32768
#define ANG_DEG(d) ((int)((d) * ANG_FULL / 360))

uint32_t rng_next(void);
static inline int rng_range(int n) { return (int)((rng_next() >> 8) % (uint32_t)n); }
static inline int32_t rng_fx(void) { return (int32_t)(rng_next() >> 20); } // 0..4095

// ---------------------------------------------------------------------------
// World dimensions
// ---------------------------------------------------------------------------
#define WORLD_H 64
#define SEC_SIZE 16
#define SEC_Y (WORLD_H / SEC_SIZE)
#define SEA_LEVEL 30

// Faces: 0=-X(west) 1=+X(east) 2=-Y(down) 3=+Y(up) 4=-Z(north) 5=+Z(south)
enum { FACE_W = 0, FACE_E, FACE_D, FACE_U, FACE_N, FACE_S };
extern const int8_t face_dx[6], face_dy[6], face_dz[6];
static inline int face_opposite(int f) { return f ^ 1; }

// Horizontal facing: 0=N(-Z) 1=E(+X) 2=S(+Z) 3=W(-X)
extern const uint8_t facing_to_face[4];

// Game modes
enum { MODE_SURVIVAL = 0, MODE_CREATIVE = 1 };

#define TICKS_PER_SEC 20
#define DAY_LENGTH 24000

// Debug helper
void debug_log(const char *fmt, ...);

// Lightweight profiling marks (timer 0 at 32.7 kHz)
extern uint16_t prof_t[8];
#define PROF_MARK(i) (prof_t[i] = TIMER_DATA(0))
