#include <pebble.h>

// ===========================================================================
//  WristWars - step 6: two-stage movement, reliable unit advance
// ===========================================================================

#define GRID_W    10
#define GRID_H    10
#define TILE      18
#define MAX_UNITS 16
#define MAX_REACH (GRID_W * GRID_H)
#define UNREACHABLE 255
#define AI_STEP_MS  450

// ---- Terrain --------------------------------------------------------------
//   .  = plains      F = forest
//   M  = mountain    w = water

static const char *MAP[GRID_H] = {
  ".......F..",
  "..........",
  "....M.F...",
  ".F.ww...F.",
  "..Mwww...F",
  "F...wwwM..",
  ".F...ww.F.",
  "...F.M....",
  "..........",
  "..F.......",
};

typedef enum { T_PLAINS = 0, T_FOREST, T_MOUNTAIN, T_WATER } Terrain;

static const char *TERRAIN_NAMES[] = { "Plains", "Forest", "Mountain", "Sea" };
static const int   TERRAIN_DEF[]   = { 0, 1, 2, 0 };

static Terrain terrain_at(int x, int y) {
  switch (MAP[y][x]) {
    case 'F': return T_FOREST;
    case 'M': return T_MOUNTAIN;
    case 'w': return T_WATER;
    default:  return T_PLAINS;
  }
}

static GColor terrain_color(Terrain t) {
  switch (t) {
    case T_FOREST:   return GColorDarkGreen;
    case T_MOUNTAIN: return GColorWindsorTan;
    case T_WATER:    return GColorBlueMoon;
    default:         return GColorMintGreen;
  }
}

// ---- Units ----------------------------------------------------------------

typedef enum { U_INFANTRY = 0, U_BAZOOKA, U_TANK, U_ARTILLERY } UnitType;

static const char *UNIT_NAMES[]   = { "Infantry", "Bazooka", "Tank", "Artillery" };
static const char *UNIT_LETTERS[] = { "I", "B", "T", "A" };
static const int   UNIT_MOVE[]    = {  3,   2,   6,   2  };
static const int   UNIT_VALUE[]   = {  1,   2,   3,   3  };

static const int DMG[4][4] = {
  //        Inf  Bzka Tank Arty
  /*Inf */ {  5,   5,   1,   6 },
  /*Bzka*/ {  6,   5,   7,   7 },
  /*Tank*/ {  7,   7,   5,   8 },
  /*Arty*/ {  7,   7,   6,   7 },
};

typedef struct {
  uint8_t type;
  uint8_t team;
  uint8_t x, y;
  int8_t  hp;
  bool    acted;
  bool    alive;
} Unit;

static Unit    s_units[MAX_UNITS];
static uint8_t s_unit_count = 0;

static bool is_indirect(UnitType t) { return t == U_ARTILLERY; }
static int  min_range(UnitType t)   { return is_indirect(t) ? 2 : 1; }
static int  max_range(UnitType t)   { return is_indirect(t) ? 3 : 1; }

static int abs_i(int v) { return v < 0 ? -v : v; }

static int dist_xy(int ax, int ay, int bx, int by) {
  return abs_i(ax - bx) + abs_i(ay - by);
}

static int dist_between(const Unit *a, const Unit *b) {
  return dist_xy(a->x, a->y, b->x, b->y);
}

static void add_unit(UnitType type, int team, int x, int y) {
  if (s_unit_count >= MAX_UNITS) return;
  Unit *u = &s_units[s_unit_count++];
  u->type = type; u->team = team;
  u->x = x; u->y = y;
  u->hp = 10; u->acted = false; u->alive = true;
}

static void setup_units(void) {
  s_unit_count = 0;
  add_unit(U_INFANTRY,  0, 0, 9);
  add_unit(U_INFANTRY,  0, 1, 9);
  add_unit(U_BAZOOKA,   0, 0, 8);
  add_unit(U_TANK,      0, 2, 8);
  add_unit(U_ARTILLERY, 0, 1, 7);

  add_unit(U_INFANTRY,  1, 9, 0);
  add_unit(U_INFANTRY,  1, 8, 0);
  add_unit(U_BAZOOKA,   1, 9, 1);
  add_unit(U_TANK,      1, 7, 1);
  add_unit(U_ARTILLERY, 1, 8, 2);
}

static int unit_at(int x, int y) {
  for (int i = 0; i < s_unit_count; i++) {
    if (s_units[i].alive && s_units[i].x == x && s_units[i].y == y) return i;
  }
  return -1;
}

static int count_alive(int team) {
  int n = 0;
  for (int i = 0; i < s_unit_count; i++) {
    if (s_units[i].alive && s_units[i].team == team) n++;
  }
  return n;
}

// ---- Combat ---------------------------------------------------------------

static int compute_damage(const Unit *att, const Unit *def) {
  int base = DMG[att->type][def->type];
  if (base <= 0) return 0;
  int dmg = base * att->hp / 10;
  int d   = TERRAIN_DEF[terrain_at(def->x, def->y)];
  dmg = dmg * (10 - 2 * d) / 10;
  if (dmg < 1) dmg = 1;
  return dmg;
}

static bool can_counter(const Unit *def, const Unit *att) {
  if (is_indirect(def->type)) return false;
  if (is_indirect(att->type)) return false;
  return dist_between(def, att) <= max_range(def->type);
}

static void resolve_attack(int ai, int di) {
  Unit *a = &s_units[ai];
  Unit *d = &s_units[di];

  int dmg = compute_damage(a, d);
  d->hp -= dmg;
  if (d->hp <= 0) { d->hp = 0; d->alive = false; return; }

  if (can_counter(d, a)) {
    int back = compute_damage(d, a);
    a->hp -= back;
    if (a->hp <= 0) { a->hp = 0; a->alive = false; }
  }
}

// ---- Movement -------------------------------------------------------------

static int move_cost(UnitType type, Terrain ter) {
  switch (ter) {
    case T_WATER:    return -1;
    case T_MOUNTAIN: return (type == U_INFANTRY || type == U_BAZOOKA) ? 3 : -1;
    case T_FOREST:   return 2;
    default:         return 1;
  }
}

static uint8_t s_cost[GRID_H][GRID_W];

static void compute_reach(int ui) {
  Unit *u = &s_units[ui];

  for (int y = 0; y < GRID_H; y++) {
    for (int x = 0; x < GRID_W; x++) s_cost[y][x] = UNREACHABLE;
  }
  s_cost[u->y][u->x] = 0;

  int budget = UNIT_MOVE[u->type];
  const int dx[4] = { 0, 0, -1, 1 };
  const int dy[4] = { -1, 1, 0, 0 };

  bool changed = true;
  while (changed) {
    changed = false;
    for (int y = 0; y < GRID_H; y++) {
      for (int x = 0; x < GRID_W; x++) {
        if (s_cost[y][x] == UNREACHABLE) continue;
        for (int d = 0; d < 4; d++) {
          int nx = x + dx[d], ny = y + dy[d];
          if (nx < 0 || nx >= GRID_W || ny < 0 || ny >= GRID_H) continue;
          int c = move_cost(u->type, terrain_at(nx, ny));
          if (c < 0) continue;
          int other = unit_at(nx, ny);
          if (other >= 0 && s_units[other].team != u->team) continue;
          int nc = s_cost[y][x] + c;
          if (nc <= budget && nc < s_cost[ny][nx]) {
            s_cost[ny][nx] = nc;
            changed = true;
          }
        }
      }
    }
  }
}

typedef struct { uint8_t x, y; } Tile;

static bool tile_free(int x, int y, int ui) {
  int occ = unit_at(x, y);
  return (occ < 0 || occ == ui);
}

static bool adjacent_to_enemy(int x, int y, int team) {
  const int dx[4] = { 0, 0, -1, 1 };
  const int dy[4] = { -1, 1, 0, 0 };
  for (int d = 0; d < 4; d++) {
    int i = unit_at(x + dx[d], y + dy[d]);
    if (i >= 0 && s_units[i].team != team) return true;
  }
  return false;
}

// ---- Two-stage destination picking ----------------------------------------
//  Stage 1 picks a group: firing positions, one of eight headings, or hold.
//  Stage 2 picks how far along that heading to stop.

#define GRP_ATTACK 0
#define GRP_STAY   9

static const char *GROUP_NAMES[] = {
  "Attack from",      // 0
  "North",            // 1
  "North-East",       // 2
  "East",             // 3
  "South-East",       // 4
  "South",            // 5
  "South-West",       // 6
  "West",             // 7
  "North-West",       // 8
  "Hold position",    // 9
};

// Which of the eight headings a tile lies in, relative to the unit.
static int heading_of(int dx, int dy) {
  if (dx == 0 && dy == 0) return GRP_STAY;
  int adx = abs_i(dx), ady = abs_i(dy);
  if (adx > 2 * ady) return (dx > 0) ? 3 : 7;          // East  / West
  if (ady > 2 * adx) return (dy < 0) ? 1 : 5;          // North / South
  if (dx > 0)        return (dy < 0) ? 2 : 4;          // NE    / SE
  return                    (dy < 0) ? 8 : 6;          // NW    / SW
}

static Tile    s_reach[MAX_REACH];
static uint8_t s_reach_grp[MAX_REACH];
static uint8_t s_reach_cost[MAX_REACH];
static int     s_reach_count = 0;

static uint8_t s_groups[10];      // group ids that actually have tiles
static int     s_group_count = 0;
static int     s_group = 0;       // index into s_groups

static Tile    s_dests[MAX_REACH];
static int     s_dest_count = 0;
static int     s_dest = 0;

static void classify_reach(int ui) {
  Unit *u = &s_units[ui];
  s_reach_count = 0;

  for (int y = 0; y < GRID_H; y++) {
    for (int x = 0; x < GRID_W; x++) {
      if (s_cost[y][x] == UNREACHABLE) continue;
      if (!tile_free(x, y, ui)) continue;

      int grp;
      if (!is_indirect(u->type) && adjacent_to_enemy(x, y, u->team)) {
        grp = GRP_ATTACK;                       // shortcut to the useful tiles
      } else {
        grp = heading_of(x - u->x, y - u->y);
      }

      s_reach[s_reach_count].x = x;
      s_reach[s_reach_count].y = y;
      s_reach_grp[s_reach_count] = grp;
      s_reach_cost[s_reach_count] = s_cost[y][x];
      s_reach_count++;
    }
  }

  // Build the ordered list of non-empty groups: attack, headings, hold.
  s_group_count = 0;
  for (int g = 0; g <= 9; g++) {
    for (int i = 0; i < s_reach_count; i++) {
      if (s_reach_grp[i] == g) {
        s_groups[s_group_count++] = g;
        break;
      }
    }
  }
  s_group = 0;
}

static int group_tile_count(int g) {
  int n = 0;
  for (int i = 0; i < s_reach_count; i++) if (s_reach_grp[i] == g) n++;
  return n;
}

// Farthest tile in a group - used as the stage 1 preview.
static int group_preview(int g) {
  int best = -1, best_cost = -1;
  for (int i = 0; i < s_reach_count; i++) {
    if (s_reach_grp[i] != g) continue;
    if (s_reach_cost[i] > best_cost) { best_cost = s_reach_cost[i]; best = i; }
  }
  return best;
}

// Tiles of one group, nearest first.
static void build_dests_for_group(int g) {
  s_dest_count = 0;
  for (int i = 0; i < s_reach_count; i++) {
    if (s_reach_grp[i] != g) continue;
    s_dests[s_dest_count] = s_reach[i];
    // insertion sort by cost as we go
    int j = s_dest_count - 1;
    uint8_t c = s_reach_cost[i];
    while (j >= 0) {
      int jc = s_cost[s_dests[j].y][s_dests[j].x];
      if (jc <= c) break;
      s_dests[j + 1] = s_dests[j];
      j--;
    }
    s_dests[j + 1] = s_reach[i];
    s_dest_count++;
  }
  s_dest = 0;
}

// ---- Targets --------------------------------------------------------------

static int     s_targets[MAX_UNITS];
static uint8_t s_target_count = 0;

static void build_targets(int ui, bool moved) {
  Unit *u = &s_units[ui];
  s_target_count = 0;
  if (is_indirect(u->type) && moved) return;

  for (int i = 0; i < s_unit_count; i++) {
    if (!s_units[i].alive || s_units[i].team == u->team) continue;
    int d = dist_between(u, &s_units[i]);
    if (d >= min_range(u->type) && d <= max_range(u->type)) {
      s_targets[s_target_count++] = i;
    }
  }
}

// ---- Game state -----------------------------------------------------------

typedef enum {
  PHASE_BROWSE = 0,
  PHASE_GROUP,
  PHASE_MOVE,
  PHASE_TARGET,
  PHASE_ENEMY,
  PHASE_OVER,
} Phase;

static Window   *s_window;
static Layer    *s_canvas;
static AppTimer *s_ai_timer = NULL;

static Phase s_phase    = PHASE_BROWSE;
static int   s_browse   = 0;
static int   s_target   = 0;
static int   s_selected = -1;
static bool  s_moved    = false;
static int   s_winner   = -1;

static void check_game_over(void) {
  if (count_alive(1) == 0)      { s_winner = 0; s_phase = PHASE_OVER; }
  else if (count_alive(0) == 0) { s_winner = 1; s_phase = PHASE_OVER; }
}

static bool all_player_used(void) {
  for (int i = 0; i < s_unit_count; i++) {
    if (s_units[i].alive && s_units[i].team == 0 && !s_units[i].acted) return false;
  }
  return true;
}

static void snap_browse_to_own(void) {
  for (int i = 0; i < s_unit_count; i++) {
    if (s_units[i].alive && s_units[i].team == 0) { s_browse = i; return; }
  }
}

// Jump to the next of your units that still has an action left.
static void focus_next_unacted(int from) {
  for (int step = 1; step <= s_unit_count; step++) {
    int i = (from + step + s_unit_count) % s_unit_count;
    if (s_units[i].alive && s_units[i].team == 0 && !s_units[i].acted) {
      s_browse = i;
      return;
    }
  }
  if (!(s_units[s_browse].alive && s_units[s_browse].team == 0)) snap_browse_to_own();
}

static void cycle_own_unit(int dir) {
  if (count_alive(0) == 0) return;
  for (int step = 1; step <= s_unit_count; step++) {
    int i = (s_browse + dir * step + s_unit_count * 8) % s_unit_count;
    if (s_units[i].alive && s_units[i].team == 0) { s_browse = i; return; }
  }
}

// ---- Enemy AI -------------------------------------------------------------

static Tile s_ai_tiles[MAX_REACH];
static int  s_ai_tile_count = 0;

static int nearest_foe_dist(int x, int y, int team) {
  int best = 999;
  for (int i = 0; i < s_unit_count; i++) {
    if (!s_units[i].alive || s_units[i].team == team) continue;
    int d = dist_xy(x, y, s_units[i].x, s_units[i].y);
    if (d < best) best = d;
  }
  return best;
}

static void ai_act(int ui) {
  Unit *u = &s_units[ui];
  int home_x = u->x, home_y = u->y;

  compute_reach(ui);

  s_ai_tile_count = 0;
  for (int y = 0; y < GRID_H; y++) {
    for (int x = 0; x < GRID_W; x++) {
      if (s_cost[y][x] == UNREACHABLE) continue;
      if (!tile_free(x, y, ui)) continue;
      s_ai_tiles[s_ai_tile_count].x = x;
      s_ai_tiles[s_ai_tile_count].y = y;
      s_ai_tile_count++;
    }
  }

  int best_score = -1000000;
  int best_x = home_x, best_y = home_y, best_target = -1;

  for (int t = 0; t < s_ai_tile_count; t++) {
    int x = s_ai_tiles[t].x;
    int y = s_ai_tiles[t].y;
    bool moved = (x != home_x || y != home_y);

    u->x = x; u->y = y;
    int def_bonus = TERRAIN_DEF[terrain_at(x, y)] * 15;

    if (!(is_indirect(u->type) && moved)) {
      for (int i = 0; i < s_unit_count; i++) {
        if (!s_units[i].alive || s_units[i].team == u->team) continue;
        int d = dist_between(u, &s_units[i]);
        if (d < min_range(u->type) || d > max_range(u->type)) continue;

        Unit *victim = &s_units[i];
        int dmg = compute_damage(u, victim);
        int score = 1000 + dmg * 10 + def_bonus;

        if (dmg >= victim->hp) score += 300 + UNIT_VALUE[victim->type] * 60;
        if (can_counter(victim, u)) {
          int back = compute_damage(victim, u);
          score -= back * 12;
          if (back >= u->hp) score -= 400;
        }

        if (score > best_score) {
          best_score = score; best_x = x; best_y = y; best_target = i;
        }
      }
    }

    int approach = -nearest_foe_dist(x, y, u->team) * 10 + def_bonus / 3;
    if (approach > best_score) {
      best_score = approach; best_x = x; best_y = y; best_target = -1;
    }
  }

  u->x = best_x; u->y = best_y;
  if (best_target >= 0) resolve_attack(ui, best_target);
  u->acted = true;
}

static void ai_step(void *data);

static void schedule_ai(void) {
  s_ai_timer = app_timer_register(AI_STEP_MS, ai_step, NULL);
}

static void begin_player_turn(void) {
  for (int i = 0; i < s_unit_count; i++) {
    if (s_units[i].team == 0) s_units[i].acted = false;
  }
  s_phase = PHASE_BROWSE;
  s_selected = -1;
  focus_next_unacted(s_unit_count - 1);
}

static void ai_step(void *data) {
  s_ai_timer = NULL;
  if (s_phase != PHASE_ENEMY) return;

  int next = -1;
  for (int i = 0; i < s_unit_count; i++) {
    if (s_units[i].alive && s_units[i].team == 1 && !s_units[i].acted) { next = i; break; }
  }

  if (next < 0) {
    begin_player_turn();
    layer_mark_dirty(s_canvas);
    return;
  }

  ai_act(next);
  check_game_over();
  layer_mark_dirty(s_canvas);
  if (s_phase == PHASE_ENEMY) schedule_ai();
}

static void begin_enemy_turn(void) {
  for (int i = 0; i < s_unit_count; i++) {
    if (s_units[i].team == 1) s_units[i].acted = false;
  }
  s_selected = -1;
  s_phase = PHASE_ENEMY;
  schedule_ai();
}

static void restart_game(void) {
  if (s_ai_timer) { app_timer_cancel(s_ai_timer); s_ai_timer = NULL; }
  setup_units();
  s_winner = -1;
  s_selected = -1;
  s_moved = false;
  s_phase = PHASE_BROWSE;
  focus_next_unacted(s_unit_count - 1);
}

// ---- Drawing --------------------------------------------------------------

static void draw_terrain_mark(GContext *ctx, Terrain ter, int px, int py) {
  int mid = TILE / 2;
  switch (ter) {
    case T_FOREST:
      graphics_context_set_fill_color(ctx, GColorMayGreen);
      graphics_fill_circle(ctx, GPoint(px + mid, py + mid - 2), 4);
      graphics_context_set_fill_color(ctx, GColorBlack);
      graphics_fill_rect(ctx, GRect(px + mid - 1, py + TILE - 7, 2, 4), 0, GCornerNone);
      break;
    case T_MOUNTAIN:
      graphics_context_set_stroke_color(ctx, GColorWhite);
      graphics_context_set_stroke_width(ctx, 2);
      graphics_draw_line(ctx, GPoint(px + 3, py + TILE - 4), GPoint(px + mid, py + 4));
      graphics_draw_line(ctx, GPoint(px + mid, py + 4), GPoint(px + TILE - 4, py + TILE - 4));
      graphics_context_set_stroke_width(ctx, 1);
      break;
    case T_WATER:
      graphics_context_set_stroke_color(ctx, GColorCeleste);
      graphics_draw_line(ctx, GPoint(px + 3, py + 6),  GPoint(px + TILE - 6, py + 6));
      graphics_draw_line(ctx, GPoint(px + 5, py + 11), GPoint(px + TILE - 4, py + 11));
      break;
    default:
      break;
  }
}

static void draw_unit(GContext *ctx, int ui, int ox, int oy) {
  Unit *u = &s_units[ui];
  int px = ox + u->x * TILE;
  int py = oy + u->y * TILE;
  GRect box = GRect(px + 2, py + 2, TILE - 4, TILE - 4);

  GColor fill, ink;
  if (u->team == 0) {
    fill = u->acted ? GColorLightGray : GColorWhite;
    ink  = GColorBlack;
  } else {
    fill = GColorRed;
    ink  = GColorWhite;
  }

  graphics_context_set_fill_color(ctx, fill);
  graphics_fill_rect(ctx, box, 3, GCornersAll);
  graphics_context_set_stroke_color(ctx, GColorBlack);
  graphics_draw_round_rect(ctx, box, 3);

  graphics_context_set_text_color(ctx, ink);
  graphics_draw_text(ctx, UNIT_LETTERS[u->type],
                     fonts_get_system_font(FONT_KEY_GOTHIC_14),
                     GRect(px, py - 2, TILE, TILE),
                     GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);

  if (u->hp < 10) {
    int w = (TILE - 8) * u->hp / 10;
    graphics_context_set_fill_color(ctx, GColorBlack);
    graphics_fill_rect(ctx, GRect(px + 4, py + TILE - 6, TILE - 8, 3), 0, GCornerNone);
    graphics_context_set_fill_color(ctx, GColorIcterine);
    graphics_fill_rect(ctx, GRect(px + 4, py + TILE - 6, w, 3), 0, GCornerNone);
  }
}

static void canvas_update(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);

  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);

  int ox = (bounds.size.w - (GRID_W * TILE)) / 2;
  int oy = 2;

  for (int y = 0; y < GRID_H; y++) {
    for (int x = 0; x < GRID_W; x++) {
      int px = ox + x * TILE;
      int py = oy + y * TILE;
      Terrain ter = terrain_at(x, y);
      graphics_context_set_fill_color(ctx, terrain_color(ter));
      graphics_fill_rect(ctx, GRect(px, py, TILE, TILE), 0, GCornerNone);
      draw_terrain_mark(ctx, ter, px, py);
      graphics_context_set_stroke_color(ctx, GColorBlack);
      graphics_draw_rect(ctx, GRect(px, py, TILE, TILE));
    }
  }

  // Stage 1: show the whole group. Stage 2: show that group's tiles.
  if (s_phase == PHASE_GROUP && s_group_count > 0) {
    int g = s_groups[s_group];
    graphics_context_set_fill_color(ctx, GColorBlack);
    for (int i = 0; i < s_reach_count; i++) {
      if (s_reach_grp[i] != g) continue;
      graphics_fill_circle(ctx, GPoint(ox + s_reach[i].x * TILE + TILE / 2,
                                       oy + s_reach[i].y * TILE + TILE / 2), 3);
    }
  } else if (s_phase == PHASE_MOVE) {
    graphics_context_set_fill_color(ctx, GColorBlack);
    for (int i = 0; i < s_dest_count; i++) {
      graphics_fill_circle(ctx, GPoint(ox + s_dests[i].x * TILE + TILE / 2,
                                       oy + s_dests[i].y * TILE + TILE / 2), 2);
    }
  }

  for (int i = 0; i < s_unit_count; i++) {
    if (s_units[i].alive) draw_unit(ctx, i, ox, oy);
  }

  if (s_selected >= 0) {
    graphics_context_set_stroke_color(ctx, GColorCyan);
    graphics_context_set_stroke_width(ctx, 2);
    graphics_draw_rect(ctx, GRect(ox + s_units[s_selected].x * TILE,
                                  oy + s_units[s_selected].y * TILE, TILE, TILE));
    graphics_context_set_stroke_width(ctx, 1);
  }

  int cx = 0, cy = 0;
  bool show_cursor = true;
  if (s_phase == PHASE_GROUP && s_group_count > 0) {
    int p = group_preview(s_groups[s_group]);
    if (p >= 0) { cx = s_reach[p].x; cy = s_reach[p].y; } else show_cursor = false;
  } else if (s_phase == PHASE_MOVE && s_dest_count > 0) {
    cx = s_dests[s_dest].x; cy = s_dests[s_dest].y;
  } else if (s_phase == PHASE_TARGET && s_target_count > 0) {
    cx = s_units[s_targets[s_target]].x; cy = s_units[s_targets[s_target]].y;
  } else if (s_phase == PHASE_BROWSE) {
    cx = s_units[s_browse].x; cy = s_units[s_browse].y;
  } else {
    show_cursor = false;
  }

  if (show_cursor) {
    graphics_context_set_stroke_color(ctx,
        s_phase == PHASE_TARGET ? GColorRed : GColorYellow);
    graphics_context_set_stroke_width(ctx, 3);
    graphics_draw_rect(ctx, GRect(ox + cx * TILE, oy + cy * TILE, TILE, TILE));
    graphics_context_set_stroke_width(ctx, 1);
  }

  static char s_line1[40];
  static char s_line2[40];
  int top = oy + GRID_H * TILE;

  if (s_phase == PHASE_OVER) {
    snprintf(s_line1, sizeof(s_line1), "%s", s_winner == 0 ? "You win!" : "Defeated");
    snprintf(s_line2, sizeof(s_line2), "Hold Select to restart");

  } else if (s_phase == PHASE_ENEMY) {
    snprintf(s_line1, sizeof(s_line1), "Enemy turn");
    snprintf(s_line2, sizeof(s_line2), "You %d  Enemy %d", count_alive(0), count_alive(1));

  } else if (s_phase == PHASE_TARGET) {
    Unit *a = &s_units[s_selected];
    Unit *d = &s_units[s_targets[s_target]];
    Terrain dt = terrain_at(d->x, d->y);
    int out  = compute_damage(a, d);
    int back = can_counter(d, a) ? compute_damage(d, a) : 0;
    snprintf(s_line1, sizeof(s_line1), "%s  -%d / -%d", UNIT_NAMES[d->type], out, back);
    snprintf(s_line2, sizeof(s_line2), "on %s +%d%%", TERRAIN_NAMES[dt], TERRAIN_DEF[dt] * 20);

  } else if (s_phase == PHASE_GROUP) {
    int g = s_group_count > 0 ? s_groups[s_group] : GRP_STAY;
    snprintf(s_line1, sizeof(s_line1), "%s", GROUP_NAMES[g]);
    snprintf(s_line2, sizeof(s_line2), "%d tiles   %d of %d",
             group_tile_count(g), s_group + 1, s_group_count);

  } else if (s_phase == PHASE_MOVE) {
    Terrain ct = terrain_at(cx, cy);
    snprintf(s_line1, sizeof(s_line1), "%s +%d%%", TERRAIN_NAMES[ct], TERRAIN_DEF[ct] * 20);
    snprintf(s_line2, sizeof(s_line2), "%d steps   %d of %d",
             s_cost[cy][cx], s_dest + 1, s_dest_count);

  } else {
    Unit *u = &s_units[s_browse];
    Terrain ut = terrain_at(u->x, u->y);
    snprintf(s_line1, sizeof(s_line1), "%s %d%s",
             UNIT_NAMES[u->type], u->hp, u->acted ? " used" : "");
    if (all_player_used()) {
      snprintf(s_line2, sizeof(s_line2), "All used - hold Select");
    } else {
      snprintf(s_line2, sizeof(s_line2), "%s +%d%%   %d v %d",
               TERRAIN_NAMES[ut], TERRAIN_DEF[ut] * 20, count_alive(0), count_alive(1));
    }
  }

  graphics_context_set_text_color(ctx, GColorWhite);
  graphics_draw_text(ctx, s_line1, fonts_get_system_font(FONT_KEY_GOTHIC_18),
                     GRect(0, top, bounds.size.w, 22),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);

  graphics_context_set_text_color(ctx, GColorLightGray);
  graphics_draw_text(ctx, s_line2, fonts_get_system_font(FONT_KEY_GOTHIC_14),
                     GRect(0, top + 21, bounds.size.w, 18),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
}

// ---- Buttons --------------------------------------------------------------

static void finish_action(void) {
  int just_acted = s_selected;
  s_units[just_acted].acted = true;
  s_selected = -1;
  s_phase = PHASE_BROWSE;
  focus_next_unacted(just_acted);
  check_game_over();
}

static void up_handler(ClickRecognizerRef r, void *context) {
  if (s_phase == PHASE_GROUP && s_group_count > 0) {
    s_group = (s_group + s_group_count - 1) % s_group_count;
  } else if (s_phase == PHASE_MOVE && s_dest_count > 0) {
    s_dest = (s_dest + s_dest_count - 1) % s_dest_count;
  } else if (s_phase == PHASE_TARGET && s_target_count > 0) {
    s_target = (s_target + s_target_count - 1) % s_target_count;
  } else if (s_phase == PHASE_BROWSE) {
    cycle_own_unit(-1);
  }
  layer_mark_dirty(s_canvas);
}

static void down_handler(ClickRecognizerRef r, void *context) {
  if (s_phase == PHASE_GROUP && s_group_count > 0) {
    s_group = (s_group + 1) % s_group_count;
  } else if (s_phase == PHASE_MOVE && s_dest_count > 0) {
    s_dest = (s_dest + 1) % s_dest_count;
  } else if (s_phase == PHASE_TARGET && s_target_count > 0) {
    s_target = (s_target + 1) % s_target_count;
  } else if (s_phase == PHASE_BROWSE) {
    cycle_own_unit(1);
  }
  layer_mark_dirty(s_canvas);
}

static void select_handler(ClickRecognizerRef r, void *context) {
  if (s_phase == PHASE_BROWSE) {
    if (s_units[s_browse].acted) return;
    s_selected = s_browse;
    compute_reach(s_selected);
    classify_reach(s_selected);
    s_phase = PHASE_GROUP;

  } else if (s_phase == PHASE_GROUP) {
    if (s_group_count == 0) { finish_action(); }
    else {
      build_dests_for_group(s_groups[s_group]);
      s_phase = PHASE_MOVE;
    }

  } else if (s_phase == PHASE_MOVE) {
    Unit *u = &s_units[s_selected];
    if (s_dest_count > 0) {
      s_moved = (u->x != s_dests[s_dest].x || u->y != s_dests[s_dest].y);
      u->x = s_dests[s_dest].x;
      u->y = s_dests[s_dest].y;
    } else {
      s_moved = false;
    }
    build_targets(s_selected, s_moved);
    if (s_target_count > 0) { s_target = 0; s_phase = PHASE_TARGET; }
    else                    { finish_action(); }

  } else if (s_phase == PHASE_TARGET) {
    resolve_attack(s_selected, s_targets[s_target]);
    finish_action();
  }
  layer_mark_dirty(s_canvas);
}

static void select_long_handler(ClickRecognizerRef r, void *context) {
  if (s_phase == PHASE_OVER) {
    restart_game();
    layer_mark_dirty(s_canvas);
  } else if (s_phase == PHASE_BROWSE) {
    begin_enemy_turn();
    layer_mark_dirty(s_canvas);
  }
}

static void back_handler(ClickRecognizerRef r, void *context) {
  if (s_phase == PHASE_GROUP) {
    s_selected = -1;
    s_phase = PHASE_BROWSE;
    layer_mark_dirty(s_canvas);
  } else if (s_phase == PHASE_MOVE) {
    s_phase = PHASE_GROUP;
    layer_mark_dirty(s_canvas);
  } else if (s_phase == PHASE_TARGET) {
    finish_action();
    layer_mark_dirty(s_canvas);
  } else if (s_phase == PHASE_ENEMY) {
    return;
  } else {
    window_stack_pop(true);
  }
}

static void click_config(void *context) {
  window_single_repeating_click_subscribe(BUTTON_ID_UP, 150, up_handler);
  window_single_repeating_click_subscribe(BUTTON_ID_DOWN, 150, down_handler);
  window_single_click_subscribe(BUTTON_ID_SELECT, select_handler);
  window_long_click_subscribe(BUTTON_ID_SELECT, 500, select_long_handler, NULL);
  window_single_click_subscribe(BUTTON_ID_BACK, back_handler);
}

// ---- App lifecycle --------------------------------------------------------

static void window_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  s_canvas = layer_create(layer_get_bounds(root));
  layer_set_update_proc(s_canvas, canvas_update);
  layer_add_child(root, s_canvas);
}

static void window_unload(Window *window) {
  if (s_ai_timer) { app_timer_cancel(s_ai_timer); s_ai_timer = NULL; }
  layer_destroy(s_canvas);
}

static void init(void) {
  restart_game();
  s_window = window_create();
  window_set_click_config_provider(s_window, click_config);
  window_set_window_handlers(s_window, (WindowHandlers) {
    .load   = window_load,
    .unload = window_unload,
  });
  window_stack_push(s_window, true);
}

static void deinit(void) {
  window_destroy(s_window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
