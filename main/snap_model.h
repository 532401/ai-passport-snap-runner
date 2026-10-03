#pragma once

#include <stdbool.h>
#include <stdint.h>

#define SNAP_HAZARD_COUNT 5
#define SNAP_PLAYER_X 58.0f
#define SNAP_PLAYER_HALF_W 7.0f
#define SNAP_PLAYER_HEIGHT 22.0f
#define SNAP_GRAVITY 800.0f

typedef struct {
    float min;
    float max;
} snap_range_t;

typedef struct {
    float loud[3];
    float soft[3];
    unsigned count;
} snap_calibration_t;

bool snap_range_valid(snap_range_t range);
bool snap_calibration_add(snap_calibration_t *cal, float amplitude, bool clipped);
bool snap_calibration_finish(const snap_calibration_t *cal, snap_range_t *range);
unsigned snap_level(snap_range_t range, float amplitude);
float snap_jump_height(unsigned level);

typedef enum { SNAP_ROCK, SNAP_PIT } snap_hazard_kind_t;
typedef struct {
    float x;
    float width;
    float height;
    snap_hazard_kind_t kind;
    bool hit;
} snap_hazard_t;

typedef struct {
    snap_hazard_t hazards[SNAP_HAZARD_COUNT];
    uint32_t random;
    float distance;
    float speed;
    float y;
    float velocity;
    float invincible;
    float hurt_flash;
    unsigned lives;
    unsigned last_level;
    bool grounded;
    bool paused;
    bool practice;
} snap_game_t;

void snap_game_start(snap_game_t *game, uint32_t seed, bool practice);
bool snap_game_jump(snap_game_t *game, unsigned level);
/* Fixed-step simulation: each call is 10 ms; no wall-clock or platform access. */
void snap_game_step(snap_game_t *game);
unsigned snap_game_score(const snap_game_t *game);
/* Controller pacing never catches up by spinning after a missed deadline. */
unsigned snap_loop_delay_ms(uint64_t work_ms);
