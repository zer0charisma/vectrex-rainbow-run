/*
 * Rainbow Run
 * Toolchain: VectreC (CMOC). Build:
 *   cmoc --vectrex -I $VECTREC/stdlib -L $VECTREC/stdlib -o rainbow.bin main.c
 *
 * Five levels. In each, a trophy comes down the rainbow after ~18 s:
 * diamond, heart, horseshoe, moon, rainbow. Reach it to clear the level:
 * LEVEL COMPLETE and a cheer sparkle in, in bubble letters like the
 * title. Clear the fifth: YOU WIN!! YOU RULE THE WORLD!
 * Each level is faster and busier.
 *
 * Controls:
 *   Joystick left/right  switch lanes (6: a corner on each side and four
 *                        across the floor)
 *   Joystick up/down     run forward / drift back along the rainbow
 *   Button 1             jump
 *   Button 2             blink: instantly reappear at a random spot
 *
 * Hazards:
 *   Tumbleweed  rolls sideways across lanes        -> jump or dodge
 *   Hobble      rope across TWO lanes              -> jump
 *   Eagle       flies overhead, shadows your lane, -> dodge the fireball
 *               then fires a fireball at you          (jumping won't help)
 *
 * Notes for editing:
 * - Timings count frames (50 per second on a real Vectrex).
 * - No initialized globals: everything is set in code, so nothing
 *   depends on the startup code copying a .data section.
 * - CMOC multiplies two 8-bit values in 8 bits, so cast one side to
 *   (int16_t) / (uint16_t) whenever a product can exceed 255.
 * - CMOC can't build tables of pointers, so shapes and texts are
 *   picked with switch statements.
 */
#include <vectrex/bios.h>
#include "shapes.h"
#include "road.h"
#include "music.h"
#include "title.h"
#include "font.h"

/* ---- Cartridge header (BIOS boot screen) ---- */
#pragma vx_copyright "2026"
#pragma vx_title_pos 10, -80
#pragma vx_title_size -6, 48
#pragma vx_title "g RAINBOW RUN"
#pragma vx_music vx_music_1

/* ---- Road ---- */
#define VP_Y         125     /* vanishing point: top of the screen */
#define LANE_STEP    64      /* lateral units per lane */
#define POS_MAX      ((LANES - 1) * LANE_STEP)

/* ---- Unicorn ---- */
#define PT_MIN       64      /* furthest up: 3/4 of the way to the top */
#define PT_MAX       245     /* nearest the bottom edge */
#define PT_START     225
#define START_LANE   2
#define PT_SPEED     3       /* depth units per frame when moving */
#define PLAYER_LIFT  10      /* stand on the lane, not in it */
#define SCALE_FAR    0x20    /* unicorn size at the top of its range;
                                it grows to about 0x50 at the bottom */
#define JUMP_FRAMES  22      /* shorter = snappier jump */
#define JUMP_DIV     3       /* smaller = higher jump (peak ~40) */
#define JUMP_CLEAR   8       /* jump height that clears ground hazards */
#define INVULN_FRAMES 60
#define MAX_HEALTH   3

/* ---- Blink ---- */
#define BLINK_IN      8      /* frames to grow back in at the new spot */
#define VANISH_FRAMES 10     /* sparkle burst left behind at the old spot */

/* ---- Hazards ---- */
#define EAGLE_ALT     90     /* cruising altitude (near-edge units) */
#define SHOT_TRACK    6      /* how fast a fireball slides toward its lane */
#define MAX_OBS       5      /* last slot is kept free for fireballs */

/* ---- Levels ---- */
#define LEVELS         5
#define LEVEL_FRAMES   850   /* ~17 s of play before the trophy appears */
#define TROPHY_RETRY   350   /* ~7 s until the next one if you miss it */
#define TROPHY_ALT     16    /* how high the trophy floats over its lane */
#define LEVELUP_FRAMES 200   /* celebration between levels: ~4 s */
#define INTRO_FRAMES   75    /* "LEVEL n" before a level starts: ~1.5 s */
#define FIRST_SPAWN    12    /* frames into a level before the first hazard */
#define WIN_FRAMES     650   /* the finale, then back to the title */
/* Text scales: 0x80 = the title's letter size */
#define TITLE_SCALE    0x80
#define DONE_SCALE     0x22  /* LEVEL COMPLETE: small */
#define CHEER_SCALE    0x3A  /* the cheer underneath: bigger */
#define WIN_BIG_SCALE  0x50  /* YOU WIN!! */
#define WIN_SUB_SCALE  0x34  /* YOU RULE THE WORLD! */
#define INTRO_SCALE    0x50  /* LEVEL n */
#define CHEER_DELAY    20    /* frames before the cheer starts appearing */

/* Difficulty per level: obstacle speed, gap between spawns (frames:
   minimum + random part), and share of eagles (out of 256). */
static const uint16_t lvl_speed[LEVELS] = { 0x0230, 0x0290, 0x02F0, 0x0350, 0x03B0 };
static const uint8_t  lvl_gap[LEVELS]   = { 38, 32, 27, 23, 19 };
static const uint8_t  lvl_mask[LEVELS]  = { 31, 31, 31, 15, 15 };
static const uint8_t  lvl_eagle[LEVELS] = { 40, 55, 70, 85, 100 };

/* ---- HUD ---- */
#define HEART_SCALE   0x90
#define HEART_GAP     24

#define MAX_SPARKLES  6

/* Music timing: the VIA timer the BIOS uses to pace frames */
#define VIA_T2_HI     (*(volatile uint8_t *)0xD009)
#define VIA_IFR       (*(volatile uint8_t *)0xD00D)
#define FRAME_UNITS   117    /* one 50 Hz frame = 30000 cycles = 117 x 256 */

/* Set to 1 to show a frame-time meter: a line at the bottom of the screen
   that grows when a frame takes longer than 1/50 s (the game can't keep
   up). No line = every frame on time. */
#define SHOW_PERF     0

enum { STATE_TITLE, STATE_INTRO, STATE_PLAY, STATE_GAMEOVER, STATE_LEVELUP, STATE_WIN };
enum { OBS_TUMBLEWEED, OBS_TROPHY, OBS_HOBBLE, OBS_EAGLE, OBS_SHOT };

typedef struct {
    uint16_t z16;   /* depth, 8.8 fixed point: 0 = far, 0xFFxx = near */
    uint8_t  edge;  /* which lane line it rides down */
} Sparkle;

typedef struct {
    uint8_t  active;
    uint8_t  type;
    uint8_t  checked;    /* collision already resolved this pass */
    uint8_t  fired;      /* eagle: has fired its fireball */
    uint8_t  t0;         /* fireball: depth it was fired from */
    uint16_t z16;
    uint16_t speed;
    int16_t  pos;        /* lateral position: lane * 64, 0..POS_MAX */
    int16_t  target;     /* eagle / fireball: lateral position to head for */
    int8_t   drift;      /* tumbleweed: sideways roll per frame */
} Obstacle;

static uint8_t  state;
static uint8_t  frame;
static uint16_t rng;
static uint16_t music_acc;
static uint8_t  frame_ticks;       /* 1/50 s ticks the last frame took */

/* Road curve */
static uint16_t road_tick;
static int8_t   curve_tab[CURVE_BUCKETS];  /* road's sideways shift by depth */
static int8_t   road_buf[1 + 2 * (ROAD_SAMPLES - 1)];

/* Game */
static uint8_t  health;
static uint8_t  invuln;
static uint8_t  level;          /* 1..5 */
static uint16_t trophy_timer;   /* frames until the trophy can appear */
static uint16_t state_timer;
static uint16_t base_speed;
static uint8_t  spawn_timer;

/* Unicorn */
static uint8_t  player_lane;
static int16_t  player_pos;     /* slides toward player_lane * 64 */
static uint8_t  player_t;       /* depth along the rainbow */
static uint8_t  jump_t;
static uint8_t  joy_held;
static uint8_t  blink_t;
static uint8_t  vanish_t;
static int8_t   vanish_y, vanish_x;
static uint8_t  vanish_scale;

/* Title */
static uint8_t  title_t;
static uint8_t  title_spot;
#define GLINTS 6
static uint8_t  glint_idx[GLINTS];
static uint8_t  glint_age[GLINTS];

static Sparkle  sparkles[MAX_SPARKLES];
static Obstacle obs[MAX_OBS];

/* Celebration */

/* Result of project_pos() / project_edge(): a screen position */
static int16_t  sy, sx;

/* ================================================================ */
/* Math helpers                                                      */
/* ================================================================ */

static uint8_t rnd(void)
{
    rng ^= rng << 7;
    rng ^= rng >> 9;
    rng ^= rng << 8;
    return (uint8_t)rng;
}

static int8_t clamp8(int16_t v)
{
    if (v > 127)  return 127;
    if (v < -127) return -127;
    return (int8_t)v;
}

/* Linear depth -> perspective depth: slow far away, fast up close. */
static uint8_t persp(uint16_t z16)
{
    uint16_t d = z16 >> 8;
    return (uint8_t)((d * d) >> 8);
}

/* Rebuild the road's curve for this frame from two precomputed tables
   (road.h): a slow bend of the far end left and right, plus a smaller
   weave. Just table reads and adds: no multiplying. */
static void road_update(void)
{
    uint8_t  i;
    uint16_t bi, wi;

    road_tick++;
    bi = (uint16_t)((road_tick >> 3) & 63) * CURVE_BUCKETS;
    wi = (uint16_t)((road_tick >> 2) & 63) * CURVE_BUCKETS;
    for (i = 0; i < CURVE_BUCKETS; i++)
        curve_tab[i] = (int8_t)(bend_tab[bi + i] + weave_tab[wi + i]);
}

/* The curve at any depth t: blend the two nearest table entries.
   (Neighbouring entries differ by at most 5, so this product fits in a
   byte even with CMOC's 8-bit multiply.) */
static int8_t curve_at(uint8_t t)
{
    uint8_t i = t >> 3;
    int8_t  c = curve_tab[i];
    if (i < CURVE_BUCKETS - 1)
        c = (int8_t)(c + (((int8_t)(curve_tab[i + 1] - c) * (int8_t)(t & 7)) >> 3));
    return c;
}

/* Look up a depth table row (lane or edge) at depth t, blending the two
   nearest buckets. Steps between buckets are at most 4. */
static int8_t depth_lookup(const int8_t *row, uint8_t t)
{
    uint8_t i = t >> 2;
    int8_t  v = row[i];
    return (int8_t)(v + (((int8_t)(row[i + 1] - v) * (int8_t)(t & 3)) >> 2));
}

static uint8_t pos_to_lane(int16_t pos)
{
    return (uint8_t)((pos + 32) >> 6);
}

/* Lift something off the road by alt (shrinking with distance). On the
   corner lanes "up" also leans toward the middle of the screen. */
static void lift(uint8_t lane, uint8_t t, int16_t alt)
{
    int16_t a;
    if (!alt) return;
    a = (alt * (int16_t)t) >> 8;
    if (lane_lean[lane]) {
        int16_t d = (a >> 1) + (a >> 3) + (a >> 4);   /* about 0.7 */
        sy += d;
        sx += lane_lean[lane] > 0 ? d : -d;
    } else {
        sy += a;
    }
}

/* Screen position (sy, sx) of a lateral position at depth t, lifted by
   alt. Positions between lanes (rolling tumbleweeds, the unicorn mid-
   step) blend the two lanes. */
static void project_pos(int16_t pos, uint8_t t, int16_t alt)
{
    uint8_t L, f;
    const int8_t *ry, *rx;

    if (pos < 0)       pos = 0;
    if (pos > POS_MAX) pos = POS_MAX;
    L  = (uint8_t)(pos >> 6);
    f  = (uint8_t)(pos & 63);
    ry = lane_ty + (uint16_t)L * DEPTH_BUCKETS;
    rx = lane_tx + (uint16_t)L * DEPTH_BUCKETS;
    sy = depth_lookup(ry, t);
    sx = depth_lookup(rx, t);
    if (f) {
        int16_t y2 = depth_lookup(ry + DEPTH_BUCKETS, t);
        int16_t x2 = depth_lookup(rx + DEPTH_BUCKETS, t);
        sy += ((y2 - sy) * (int16_t)f) >> 6;
        sx += ((x2 - sx) * (int16_t)f) >> 6;
    }
    sx += curve_at(t);
    lift(pos_to_lane(pos), t, alt);
}

/* Screen position of a lane edge (the line between two lanes) */
static void project_edge(uint8_t edge, uint8_t t)
{
    sy = depth_lookup(edge_ty + (uint16_t)edge * DEPTH_BUCKETS, t);
    sx = depth_lookup(edge_tx + (uint16_t)edge * DEPTH_BUCKETS, t)
         + curve_at(t);
}

static uint8_t jump_height(void)
{
    uint8_t t;
    if (jump_t == 0) return 0;
    t = JUMP_FRAMES - jump_t;
    return (uint8_t)(((uint16_t)t * (JUMP_FRAMES - t)) / JUMP_DIV);
}

/* Unicorn size: SCALE_FAR at the top of its range, growing evenly to
   about 0x50 at the bottom (17/64 per depth step: no division needed) */
static uint8_t player_scale(void)
{
    uint16_t span = (uint16_t)(player_t - PT_MIN);
    return (uint8_t)(SCALE_FAR + ((span * 17) >> 6));
}

static void project_player(void)
{
    project_pos(player_pos, player_t, (int16_t)PLAYER_LIFT + jump_height());
}

/* ================================================================ */
/* Drawing helpers                                                   */
/* ================================================================ */

/* Each outline goes to the BIOS in one call (see shapes.h) */
static void draw_shape(const int8_t *p)
{
    uint8_t polys = (uint8_t)*p++;
    while (polys--) {
        moveto_d(p[0], p[1]);
        draw_vlc((char *)(p + 2));
        p += 3 + 2 * (p[2] + 1);
    }
}

/* All art is stored with doubled coordinates, so it's drawn at half the
   scale asked for: same size on screen, half the beam time per line. */
static void draw_at(int8_t y, int8_t x, uint8_t scale, const int8_t *shape)
{
    reset0ref();
    set_scale(0x7F);
    moveto_d(y, x);
    set_scale(scale >> 1);
    draw_shape(shape);
}

static const int8_t *trophy_shape(uint8_t lvl)     /* lvl 1..5 */
{
    switch (lvl) {
    case 1:  return shape_trophy_diamond;
    case 2:  return shape_trophy_heart;
    case 3:  return shape_trophy_horseshoe;
    case 4:  return shape_trophy_moon;
    default: return shape_trophy_rainbow;
    }
}

/* A trophy: drawn twice so it glows, with glints twinkling around it */
static void draw_trophy(int8_t y, int8_t x, uint8_t scale, uint8_t lvl)
{
    uint8_t i;
    int8_t  r = (int8_t)(((uint16_t)scale * 40) >> 7);

    intensity_a(0x7F);
    draw_at(y, x, scale, trophy_shape(lvl));
    draw_at(y, x, scale, trophy_shape(lvl));

    for (i = 0; i < 4; i++) {
        uint8_t ph = (uint8_t)(frame * 3 + i * 64);
        int8_t dy = (int8_t)(((int16_t)sine64[(ph >> 2) & 63] * r) >> 7);
        int8_t dx = (int8_t)(((int16_t)sine64[((ph >> 2) + 16) & 63] * r) >> 7);
        intensity_a((uint8_t)(0x50 + ((frame + i * 9) & 0x2F)));
        draw_at(clamp8((int16_t)y + dy), clamp8((int16_t)x + dx),
                (uint8_t)(0x18 + ((frame + i * 5) & 15)), shape_glint);
    }
}

/* A trophy on a text screen: drawn once, steady, no glints */
static void draw_trophy_calm(int8_t y, int8_t x, uint8_t scale, uint8_t lvl)
{
    intensity_a(0x7F);
    draw_at(y, x, scale, trophy_shape(lvl));
}

/* ================================================================ */
/* Spawning                                                          */
/* ================================================================ */

static uint8_t pick_type(void)
{
    uint8_t r = rnd();
    if (r < lvl_eagle[level - 1])        return OBS_EAGLE;
    if (r < lvl_eagle[level - 1] + 70)   return OBS_HOBBLE;
    return OBS_TUMBLEWEED;
}

static void spawn_obstacle(Obstacle *o, uint8_t type)
{
    uint8_t r = rnd();

    o->active  = 1;
    o->type    = type;
    o->checked = 0;
    o->fired   = 0;
    o->z16     = 0;
    o->speed   = base_speed;
    o->drift   = 0;
    o->pos     = (int16_t)(r % LANES) * LANE_STEP;
    o->target  = o->pos;

    switch (type) {
    case OBS_TUMBLEWEED:
        o->drift = (int8_t)((r >> 4) % 3) - 1;   /* -1, 0, +1 */
        o->drift = (int8_t)(o->drift * 2);
        break;
    case OBS_HOBBLE:
        o->pos = (int16_t)(r % (LANES - 1)) * LANE_STEP;   /* left lane of a pair */
        break;
    case OBS_EAGLE:
        o->speed = base_speed + (base_speed >> 2);
        break;
    case OBS_TROPHY:
        o->speed = base_speed - (base_speed >> 2);    /* a bit slower: catchable */
        break;
    }
}

static void fire_shot(Obstacle *eagle, uint8_t t)
{
    uint8_t i;
    for (i = 0; i < MAX_OBS; i++) {
        Obstacle *s = &obs[i];
        if (s->active) continue;
        s->active  = 1;
        s->type    = OBS_SHOT;
        s->checked = 0;
        s->fired   = 0;
        s->t0      = t;
        s->z16     = eagle->z16;
        s->speed   = eagle->speed << 1;
        s->pos     = eagle->pos;
        s->target  = (int16_t)player_lane * LANE_STEP;
        s->drift   = 0;
        return;
    }
}

static uint8_t trophy_on_rainbow(void)
{
    uint8_t i;
    for (i = 0; i < MAX_OBS; i++)
        if (obs[i].active && obs[i].type == OBS_TROPHY) return 1;
    return 0;
}

static void clear_obstacles(void)
{
    uint8_t i;
    for (i = 0; i < MAX_OBS; i++) obs[i].active = 0;
}

/* ================================================================ */
/* Game state                                                        */
/* ================================================================ */

/* Every level starts the same clean way: an empty road, the unicorn
   standing still on its lane (no jump, blink or hurt-flash carried over),
   then "LEVEL n" for two seconds before hazards start coming. */
static void start_level(void)
{
    base_speed   = lvl_speed[level - 1];
    trophy_timer = LEVEL_FRAMES;
    spawn_timer  = FIRST_SPAWN;
    clear_obstacles();

    jump_t     = 0;
    blink_t    = 0;
    vanish_t   = 0;
    invuln     = 0;
    joy_held   = 1;
    player_pos = (int16_t)player_lane * LANE_STEP;

    state       = STATE_INTRO;
    state_timer = INTRO_FRAMES;
}

static void game_init(void)
{
    uint8_t i;

    health      = MAX_HEALTH;
    invuln      = 0;
    level       = 1;
    player_lane = START_LANE;
    player_pos  = START_LANE * LANE_STEP;
    player_t    = PT_START;
    jump_t      = 0;
    blink_t     = 0;
    vanish_t    = 0;
    joy_held    = 1;

    for (i = 0; i < MAX_SPARKLES; i++) {
        sparkles[i].z16 = (uint16_t)i << 13;
        sparkles[i].edge = (uint8_t)(rnd() % EDGES);
    }
    start_level();
}

static void collect_trophy(void)
{
    clear_obstacles();
    jump_t      = 0;
    blink_t     = 0;
    invuln      = 0;
    if (health < MAX_HEALTH) health++;      /* a little reward */
    if (level >= LEVELS) {
        state       = STATE_WIN;
        state_timer = WIN_FRAMES;
    } else {
        state       = STATE_LEVELUP;
        state_timer = LEVELUP_FRAMES;
    }
}

/* Count a screen timer down by real time: if a busy frame took longer
   than 1/50 s, count all of it, so a 4-second screen lasts 4 seconds.
   Returns 1 when the timer runs out. */
static uint8_t timer_tick(void)
{
    uint8_t t = frame_ticks ? frame_ticks : 1;
    if (state_timer > t) { state_timer -= t; return 0; }
    state_timer = 0;
    return 1;
}

static void hurt(void)
{
    invuln = INVULN_FRAMES;
    if (--health == 0) {
        state    = STATE_GAMEOVER;
        music_on = 0;
    }
}

/* Blink: leave a sparkle burst behind and reappear somewhere random */
static void blink(void)
{
    uint8_t i;

    project_player();
    vanish_y     = clamp8(sy);
    vanish_x     = clamp8(sx);
    vanish_scale = player_scale();
    vanish_t     = VANISH_FRAMES;

    player_lane = (uint8_t)(rnd() % LANES);
    player_pos  = (int16_t)player_lane * LANE_STEP;
    player_t    = PT_MIN + rnd() % (PT_MAX - PT_MIN);
    jump_t      = 0;
    blink_t     = BLINK_IN;
    joy_held    = 1;

    /* Anything now behind the unicorn can't hit it */
    for (i = 0; i < MAX_OBS; i++)
        if (obs[i].active && persp(obs[i].z16) >= player_t)
            obs[i].checked = 1;
}

/* ================================================================ */
/* Update                                                            */
/* ================================================================ */

static void update_input(void)
{
    uint8_t left, right;

    controller_check_joysticks();
    controller_check_buttons();

    if (controller_button_1_2_pressed()) {      /* blink: always instant */
        blink();
        return;
    }

    if (controller_joystick_1_up()   && player_t > PT_MIN + PT_SPEED)
        player_t -= PT_SPEED;
    if (controller_joystick_1_down() && player_t < PT_MAX - PT_SPEED)
        player_t += PT_SPEED;

    left  = controller_joystick_1_left();
    right = controller_joystick_1_right();
    if (!left && !right) {
        joy_held = 0;
    } else if (!joy_held) {
        if (left  && player_lane > 0)          player_lane--;
        if (right && player_lane < LANES - 1)  player_lane++;
        joy_held = 1;
    }

    if (controller_button_1_1_pressed() && jump_t == 0)
        jump_t = JUMP_FRAMES;
}

static void update_player(void)
{
    int16_t target = (int16_t)player_lane * LANE_STEP;

    if (player_pos < target - 16)      player_pos += 16;
    else if (player_pos > target + 16) player_pos -= 16;
    else                               player_pos = target;

    if (jump_t)   jump_t--;
    if (invuln)   invuln--;
    if (blink_t)  blink_t--;
    if (vanish_t) vanish_t--;
}

static void update_sparkles(void)
{
    uint8_t i;
    for (i = 0; i < MAX_SPARKLES; i++) {
        uint16_t nz = sparkles[i].z16 + (base_speed << 1);
        if (nz < sparkles[i].z16) {
            sparkles[i].edge = (uint8_t)(rnd() % EDGES);
            nz = 0;
        }
        sparkles[i].z16 = nz;
    }
}

static int16_t shot_alt(Obstacle *o, uint8_t t)
{
    if (t >= player_t || player_t <= o->t0) return 0;
    return (int16_t)(EAGLE_ALT * (int16_t)(player_t - t))
           / (int16_t)(player_t - o->t0);
}

static uint8_t collides(Obstacle *o)
{
    uint8_t lane = pos_to_lane(o->pos);
    uint8_t airborne = jump_height() >= JUMP_CLEAR;

    switch (o->type) {
    case OBS_HOBBLE:
        return !airborne && (player_lane == lane || player_lane == lane + 1);
    case OBS_SHOT:
        return player_lane == lane;
    case OBS_TUMBLEWEED:
        return !airborne && player_lane == lane;
    default:
        return 0;
    }
}

static void slide_toward(Obstacle *o, uint8_t step)
{
    if (o->pos < o->target - step)      o->pos += step;
    else if (o->pos > o->target + step) o->pos -= step;
    else                                o->pos = o->target;
}

static void move_obstacle(Obstacle *o, uint8_t t)
{
    switch (o->type) {
    case OBS_TUMBLEWEED:
        if (frame & 1) {
            o->pos += o->drift;
            if (o->pos <= 0)       { o->pos = 0;       o->drift = -o->drift; }
            if (o->pos >= POS_MAX) { o->pos = POS_MAX; o->drift = -o->drift; }
        }
        break;
    case OBS_EAGLE:
        if (!o->fired) {
            o->target = (int16_t)player_lane * LANE_STEP;
            slide_toward(o, 3);
            if (t >= (player_t >> 1)) {
                o->fired = 1;
                fire_shot(o, t);
            }
        }
        break;
    case OBS_SHOT:
        slide_toward(o, SHOT_TRACK);
        break;
    }
}

static void update_obstacles(void)
{
    uint8_t i, t;
    Obstacle *o;

    if (trophy_timer) trophy_timer--;

    if (spawn_timer) spawn_timer--;
    if (spawn_timer == 0 || (trophy_timer == 0 && !trophy_on_rainbow())) {
        for (i = 0; i < MAX_OBS - 1; i++) {
            if (!obs[i].active) {
                if (trophy_timer == 0 && !trophy_on_rainbow()) {
                    spawn_obstacle(&obs[i], OBS_TROPHY);
                    trophy_timer = TROPHY_RETRY;
                } else {
                    spawn_obstacle(&obs[i], pick_type());
                }
                spawn_timer = (uint8_t)(lvl_gap[level - 1]
                                        + (rnd() & lvl_mask[level - 1]));
                break;
            }
        }
    }

    for (i = 0; i < MAX_OBS; i++) {
        uint16_t nz;
        o = &obs[i];
        if (!o->active) continue;

        nz = o->z16 + o->speed;
        if (nz < o->z16) { o->active = 0; continue; }
        o->z16 = nz;

        t = persp(o->z16);
        move_obstacle(o, t);

        if (t >= player_t && !o->checked) {
            o->checked = 1;
            if (o->type == OBS_TROPHY) {
                if (pos_to_lane(o->pos) == player_lane) {
                    collect_trophy();
                    return;
                }
            } else if (invuln == 0 && collides(o)) {
                o->active = 0;
                hurt();
            }
        }
    }

    /* Within a level, things keep speeding up a little */
    if (frame == 0) base_speed += 8;
}

/* Keep the music on tempo even when a busy frame runs long: the BIOS
   paces frames with VIA timer 2, which keeps counting down past zero, so
   its high byte tells us roughly how late this frame is. */
static void music_frame(void)
{
    uint8_t late = 0;
    if (VIA_IFR & 0x20) late = (uint8_t)(0xFF - VIA_T2_HI);
#if SHOW_PERF
    if (late) {
        intensity_a(0x7F);
        reset0ref();
        set_scale(0x7F);
        moveto_d(-120, -120);
        draw_line_d(0, (int8_t)(late > 120 ? 120 : late));
    }
#endif
    music_acc += (uint16_t)late + FRAME_UNITS;   /* 16-bit: late can be up to 255 */
    frame_ticks = 0;
    while (music_acc >= FRAME_UNITS) {
        music_update();
        music_acc -= FRAME_UNITS;
        frame_ticks++;
    }
}

/* ================================================================ */
/* Drawing                                                           */
/* ================================================================ */

static void draw_rainbow(void)
{
    uint8_t e, k, base;
    int16_t cx[ROAD_SAMPLES];

    for (k = 0; k < ROAD_SAMPLES; k++) cx[k] = curve_at(road_t[k]);

    for (e = 0; e < EDGES; e++) {
        base = (uint8_t)(e * ROAD_SAMPLES);
        road_buf[0] = ROAD_SAMPLES - 2;
        /* Deltas are doubled and drawn at half scale: same picture, but
           the beam finishes each line in half the time */
        for (k = 1; k < ROAD_SAMPLES; k++) {
            road_buf[2 * k - 1] = clamp8(2 * ((int16_t)edge_sy[base + k]
                                              - edge_sy[base + k - 1]));
            road_buf[2 * k]     = clamp8(2 * (((int16_t)edge_sx[base + k] + cx[k])
                                     - ((int16_t)edge_sx[base + k - 1] + cx[k - 1])));
        }
        intensity_a((e == 0 || e == EDGES - 1) ? 0x48 : 0x58);
        reset0ref();
        set_scale(0x7F);
        moveto_d(edge_sy[base], clamp8((int16_t)edge_sx[base] + cx[0]));
        set_scale(0x40);
        draw_vlc((char *)road_buf);
    }
}

static void draw_sparkles(void)
{
    uint8_t i, t;
    for (i = 0; i < MAX_SPARKLES; i++) {
        t = persp(sparkles[i].z16);
        project_edge(sparkles[i].edge, t);
        intensity_a((uint8_t)(0x30 + ((frame + (i << 4)) & 0x3F)));
        reset0ref();
        set_scale(0x7F);
        dot_d(clamp8(sy), clamp8(sx));
    }
}

static void draw_obstacle(Obstacle *o)
{
    uint8_t t = persp(o->z16);
    uint8_t scale = (uint8_t)(0x08 + (t >> 1));
    uint8_t lane;
    const int8_t *shape;

    switch (o->type) {
    case OBS_TROPHY:
        project_pos(o->pos, t, TROPHY_ALT);
        draw_trophy(clamp8(sy), clamp8(sx), scale, level);
        return;
    case OBS_HOBBLE:
        /* Drawn across the edge between its two lanes */
        lane = pos_to_lane(o->pos);
        project_edge((uint8_t)(lane + 1), t);
        intensity_a(0x60);
        shape = shape_hobble;
        break;
    case OBS_EAGLE:
        project_pos(o->pos, t, EAGLE_ALT);
        intensity_a(0x7F);
        shape = (frame & 4) ? shape_eagle_up : shape_eagle_down;
        break;
    case OBS_SHOT:
        project_pos(o->pos, t, shot_alt(o, t));
        intensity_a((uint8_t)(0x60 + (frame & 0x1F)));
        scale = (uint8_t)(0x10 + (t >> 2));
        shape = shape_star;
        break;
    default:
        project_pos(o->pos, t, 0);
        intensity_a(0x68);
        shape = (frame & 8) ? shape_tumbleweed_a : shape_tumbleweed_b;
        break;
    }
    draw_at(clamp8(sy), clamp8(sx), scale, shape);
}

static void draw_obstacles(void)
{
    uint8_t i;
    for (i = 0; i < MAX_OBS; i++)
        if (obs[i].active) draw_obstacle(&obs[i]);
}

static void draw_player(void)
{
    int8_t  y, x;
    uint8_t scale = player_scale();

    if (vanish_t) {
        intensity_a((uint8_t)(0x0C * vanish_t + 0x07));
        draw_at(vanish_y, vanish_x,
                (uint8_t)(vanish_scale + 6 * (VANISH_FRAMES - vanish_t)),
                shape_star);
    }

    project_player();
    y = clamp8(sy);
    x = clamp8(sx);

    if (blink_t) {
        uint8_t k = (uint8_t)(BLINK_IN + 1 - blink_t);
        intensity_a(0x7F);
        draw_at(y, x, (uint8_t)(scale + 6 * blink_t), shape_star);
        intensity_a((uint8_t)(0x10 * k - 1));
        draw_at(y, x, (uint8_t)(((uint16_t)scale * k) >> 3), shape_unicorn);
        return;
    }

    if (invuln && (frame & 2)) return;
    intensity_a(state == STATE_GAMEOVER ? 0x30 : 0x7F);
    draw_at(y, x, scale, shape_unicorn);
}

static void draw_hud(void)
{
    uint8_t i;
    intensity_a(0x7F);
    for (i = 0; i < health; i++)
        draw_at(112, (int8_t)(-112 + HEART_GAP * (int16_t)i),
                HEART_SCALE, shape_heart);

    /* Trophies collected so far, small, top-right. Hidden during the
       level-complete screen (the big trophy is showing) to keep it light. */
    if (state == STATE_LEVELUP) return;
    intensity_a(0x60);
    for (i = 1; i < level; i++)
        draw_at(112, (int8_t)(118 - 22 * (int16_t)i), 0x14, trophy_shape(i));
}

/* Cheer for finishing each level */
static char *phrase(uint8_t lvl)
{
    switch (lvl) {
    case 1:  return (char *)"HELL YEAH!";
    case 2:  return (char *)"RADICAL WORK!";
    case 3:  return (char *)"THAT'S PRETTY NEAT!";
    default: return (char *)"RIGHT ON BITCH!";
    }
}

static uint8_t phrase_len(uint8_t lvl)
{
    switch (lvl) {
    case 1:  return 10;
    case 2:  return 13;
    case 3:  return 19;
    default: return 15;
    }
}

/* Width of a string in bubble letters at a given scale (display units).
   Glyph widths are at title size (scale 0x80), hence the >> 7. */
static int16_t text_width(const char *s, uint8_t len, uint8_t scale)
{
    int16_t w = 0;
    uint8_t i;
    for (i = 0; i < len; i++) w += glyph_adv(s[i]);
    return (int16_t)(((uint16_t)(w - 6) * scale) >> 7);   /* 0x80 = title size */
}

/* Bubble-letter text, centered. Letter positions are worked out once per
   piece of text and cached, so drawing it each frame is just the BIOS
   tracing the letters. A soft shine drifts across it; letters appear one
   at a time. No glints or double-drawing: text screens stay calm and light.
   slot = which cache line to use (one per line of text on screen). */
#define TEXT_SLOTS  3
#define TEXT_MAX    20
typedef struct {
    const char *s;
    uint8_t     scale;
    int8_t      xs[TEXT_MAX];
} TextLayout;
static TextLayout text_cache[TEXT_SLOTS];

static void draw_text(uint8_t slot, int8_t y, const char *s, uint8_t len,
                      uint8_t shown, uint8_t scale)
{
    TextLayout *c = &text_cache[slot];
    uint8_t i, w, v;
    const int8_t *g;

    if (c->s != s || c->scale != scale) {          /* lay it out once */
        int16_t x = -(text_width(s, len, scale) >> 1);
        for (i = 0; i < len; i++) {
            c->xs[i] = clamp8(x);
            x += ((uint16_t)glyph_adv(s[i]) * scale) >> 7;
        }
        c->s = s;
        c->scale = scale;
    }

    if (shown > len) shown = len;
    for (i = 0; i < shown; i++) {
        g = glyph(s[i]);
        if (!g) continue;
        w = (uint8_t)(((frame << 1) - (i << 4)) & 0x7F);
        v = (w < 64) ? w : (uint8_t)(127 - w);         /* 0..63 */
        intensity_a((uint8_t)(0x5C + (v >> 2)));        /* gentle */
        draw_at(y, c->xs[i], scale, g);
    }
}

/* LEVEL COMPLETE, then the level's cheer. bt = frames since it started. */
static void draw_celebration(int8_t y, uint8_t bt, uint8_t lvl)
{
    draw_text(0, y, "LEVEL COMPLETE", 14, (uint8_t)(bt >> 1), DONE_SCALE);
    if (bt > CHEER_DELAY)
        draw_text(1, (int8_t)(y - 30), phrase(lvl), phrase_len(lvl),
                  (uint8_t)((bt - CHEER_DELAY) >> 1), CHEER_SCALE);
}

static char *level_label(uint8_t lvl)
{
    switch (lvl) {
    case 1:  return (char *)"LEVEL 1";
    case 2:  return (char *)"LEVEL 2";
    case 3:  return (char *)"LEVEL 3";
    case 4:  return (char *)"LEVEL 4";
    default: return (char *)"LEVEL 5";
    }
}

/* Before a level: LEVEL n sparkles in over the empty road */
static void draw_intro(void)
{
    uint8_t it = (uint8_t)(INTRO_FRAMES - state_timer);
    draw_text(0, 20, level_label(level), 7, (uint8_t)(it >> 1), INTRO_SCALE);
}

/* Between levels: the trophy you just won, glowing above the text */
static void draw_levelup(void)
{
    uint16_t el = LEVELUP_FRAMES - state_timer;
    uint8_t  bt = (uint8_t)(el > 255 ? 255 : el);      /* fits a byte */
    draw_trophy_calm(62, 0, 0x48, level);
    draw_celebration(18, bt, level);
}

/* The win screen: YOU WIN!! and YOU RULE THE WORLD! sparkling in,
   with all five trophies in a row and the unicorn bouncing. */
static void draw_win(void)
{
    uint8_t  i;
    uint16_t el = WIN_FRAMES - state_timer;
    uint8_t  wt = (uint8_t)(el > 255 ? 255 : el);

    draw_text(0, 48, "YOU WIN!!", 9, (uint8_t)(wt >> 1), WIN_BIG_SCALE);
    if (wt > 24)
        draw_text(1, 14, "YOU RULE THE WORLD!", 19,
                  (uint8_t)((wt - 24) >> 1), WIN_SUB_SCALE);

    if (wt > 50)
        for (i = 1; i <= LEVELS; i++)
            draw_trophy_calm(-35, (int8_t)(-100 + 50 * (int16_t)(i - 1)), 0x2C, i);

    intensity_a(0x7F);
    draw_at((int8_t)(-90 + (sine64[(frame << 1) & 63] >> 4)), 0, 0x48,
            shape_unicorn);
}

/* ================================================================ */
/* Title screen                                                      */
/* ================================================================ */

#define TITLE_SPOTS     5
#define TITLE_BLINK     60
#define TITLE_LETTER_W  30

static int8_t title_spot_y(uint8_t i)
{
    switch (i) {
    case 0:  return 82;
    case 1:  return 82;
    case 2:  return 0;
    case 3:  return -48;
    default: return 0;
    }
}

static int8_t title_spot_x(uint8_t i)
{
    switch (i) {
    case 0:  return -70;
    case 1:  return 50;
    case 2:  return 75;
    case 3:  return 0;
    default: return -85;
    }
}

static const int8_t *title_letter(uint8_t i)
{
    switch (i) {
    case 0:  return letter_R;
    case 1:  return letter_A;
    case 2:  return letter_I;
    case 3:  return letter_N;
    case 4:  return letter_B;
    case 5:  return letter_O;
    case 6:  return letter_W;
    case 7:  return letter_R;
    case 8:  return letter_U;
    default: return letter_N;
    }
}

static void title_init(void)
{
    uint8_t i;
    for (i = 0; i < GLINTS; i++) {
        glint_idx[i] = (uint8_t)(rnd() % GLINT_COUNT);
        glint_age[i] = i;
    }
    state      = STATE_TITLE;
    title_t    = 0;
    title_spot = 0;
    vanish_t   = 0;
    blink_t    = BLINK_IN;
    base_speed = lvl_speed[0];
    music_start();
}

static void update_title(void)
{
    uint8_t i;

    controller_check_buttons();
    if (controller_button_1_1_pressed()) {
        game_init();
        return;
    }

    if (++title_t >= TITLE_BLINK) {
        title_t      = 0;
        vanish_y     = title_spot_y(title_spot);
        vanish_x     = title_spot_x(title_spot);
        vanish_scale = 0x40;
        vanish_t     = VANISH_FRAMES;
        title_spot   = (uint8_t)((title_spot + 1 + (rnd() & 1)) % TITLE_SPOTS);
        blink_t      = BLINK_IN;
    }
    if (blink_t)  blink_t--;
    if (vanish_t) vanish_t--;

    for (i = 0; i < GLINTS; i++) {
        if (++glint_age[i] >= GLINTS) {
            glint_age[i] = 0;
            glint_idx[i] = (uint8_t)(rnd() % GLINT_COUNT);
        }
    }
}

static void draw_title(void)
{
    uint8_t i, w, v;
    int8_t  y, x;

    for (i = 0; i < 10; i++) {
        w = (uint8_t)((frame * 3 - i * 20) & 0x7F);
        v = (w < 64) ? w : (uint8_t)(127 - w);              /* 0..63 */
        y = (i < 7) ? 25 : -30;
        x = (i < 7) ? (int8_t)(-102 + TITLE_LETTER_W * (int16_t)i)
                    : (int8_t)(-42 + TITLE_LETTER_W * (int16_t)(i - 7));

        /* Never dim: 0x58 at the darkest, full brightness at the peak */
        intensity_a((uint8_t)(0x58 + (((uint16_t)v * 5) >> 3)));
        draw_at(y, x, TITLE_SCALE, title_letter(i));

        /* Bright half of the shine: drawn twice, so it glows */
        if (v > 32) {
            intensity_a(0x7F);
            draw_at(y, x, TITLE_SCALE, title_letter(i));
        }
        /* Right at the peak: a big glint on the top-right corner */
        if (v > 50)
            draw_at((int8_t)(y + 36), (int8_t)(x + 22),
                    (uint8_t)(0x30 + ((v - 50) << 2)), shape_glint);
    }

    for (i = 0; i < GLINTS; i++) {
        uint8_t a = glint_age[i];
        uint8_t k = (uint8_t)(glint_idx[i] << 1);
        intensity_a((uint8_t)(0x7F - a * 0x10));
        draw_at(glint_pts[k], glint_pts[k + 1],
                (uint8_t)(0x28 - a * 5), shape_glint);
    }

    if (vanish_t) {
        intensity_a((uint8_t)(0x0C * vanish_t + 0x07));
        draw_at(vanish_y, vanish_x,
                (uint8_t)(vanish_scale + 6 * (VANISH_FRAMES - vanish_t)),
                shape_star);
    }
    y = title_spot_y(title_spot);
    x = title_spot_x(title_spot);
    if (blink_t) {
        uint8_t k = (uint8_t)(BLINK_IN + 1 - blink_t);
        intensity_a(0x7F);
        draw_at(y, x, (uint8_t)(0x40 + 6 * blink_t), shape_star);
        intensity_a((uint8_t)(0x10 * k - 1));
        draw_at(y, x, (uint8_t)(((uint16_t)0x40 * k) >> 3), shape_unicorn);
    } else {
        intensity_a(0x7F);
        draw_at(y, x, 0x40, shape_unicorn);
    }

    if (frame & 32) {
        intensity_a(0x6F);
        reset0ref();
        set_scale(0x7F);
        set_text_size(-5, 0x28);
        print_str_c(-90, -78, (char *)"PRESS 1 TO RUN");
    }
}

/* ================================================================ */

int main(void)
{
    rng = 0xACE1;
    frame_ticks = 1;
    {
        uint8_t i;
        for (i = 0; i < TEXT_SLOTS; i++) text_cache[i].s = 0;
    }
    controller_enable_1_x();
    controller_enable_1_y();
    title_init();

    for (;;) {
        wait_recal();
        frame++;
        road_update();

        switch (state) {
        case STATE_TITLE:
            update_title();
            update_sparkles();
            break;
        case STATE_PLAY:
            update_input();
            update_player();
            update_sparkles();
            update_obstacles();
            break;
        case STATE_INTRO:
            /* The unicorn can already move around; no hazards yet */
            update_input();
            update_player();
            update_sparkles();
            if (timer_tick()) state = STATE_PLAY;
            break;
        case STATE_LEVELUP:
            /* Read the controller anyway, so presses made during the
               celebration don't fire the moment the next level starts */
            controller_check_joysticks();
            controller_check_buttons();
            update_player();
            update_sparkles();
            if (timer_tick()) {
                level++;
                start_level();
            }
            break;
        case STATE_WIN:
            controller_check_buttons();
            update_sparkles();
            /* the button skips to the title once the text has played */
            if (timer_tick()
                || (controller_button_1_1_pressed()
                    && state_timer < WIN_FRAMES - 150))
                title_init();
            break;
        default:                                    /* game over */
            controller_check_buttons();
            if (controller_button_1_1_pressed()) title_init();
            break;
        }

        draw_rainbow();
        draw_sparkles();
        if (state == STATE_TITLE) {
            draw_title();
        } else if (state == STATE_WIN) {
            draw_win();
        } else {
            draw_obstacles();
            draw_player();
            draw_hud();
            if (state == STATE_LEVELUP) draw_levelup();
            if (state == STATE_INTRO)   draw_intro();
        }

        music_frame();
    }
    return 0;
}
