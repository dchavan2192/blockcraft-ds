// World renderer
#pragma once
#include "common.h"

typedef struct {
    int32_t x, y, z;   // eye position (fx, blocks)
    int yaw, pitch;    // libnds angle units; yaw 0 looks north (-Z), pitch > 0 looks down
} Camera;

extern Camera cam;
extern int render_distance;  // in chunks
extern uint32_t render_frame_no;
extern int stat_sections_drawn, stat_polys, stat_verts, stat_builds, stat_budget, stat_bfs, stat_budget_hit;

void render_init(void);
void render_frame(void);
// Build pending sections; returns number built. budget_ticks <= 0 means build everything visible.
int render_build_pending(int32_t budget_ticks);
// Count of sections still waiting to be built near the camera
int render_pending_count(void);
void render_free_far(void);

// Highlighted block (cursor) and break progress (0..9, -1 = none)
void render_set_target(bool valid, int x, int y, int z, int crack);

// Sky / sky_daylight
void sky_update(int32_t time, bool underwater, bool in_lava);
extern uint8_t sky_daylight;   // 0..31 light color level
extern uint16_t sky_color, fog_color;

// Camera direction helpers (fx unit vectors)
void camera_forward(int32_t *dx, int32_t *dy, int32_t *dz);

// Draw helpers for entities (in render.c)
void draw_box(int32_t x0, int32_t y0, int32_t z0, int32_t x1, int32_t y1, int32_t z1);
void render_begin_entities(void);
void render_end_entities(void);
// Relative position of a world point to the camera (wrap aware), fx
void render_rel(int32_t wx, int32_t wy, int32_t wz, int32_t *rx, int32_t *ry, int32_t *rz);

// Hooks implemented elsewhere
void entities_render(void);
void player_render_hand(void);
int render_debug_mask(void);
extern bool cam_underwater_fog;
