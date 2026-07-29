#include <pebble.h>

// ===========================================================================
//  WristWars - step 2: units, selection, movement
// ===========================================================================

#define GRID_W    10
#define GRID_H    10
#define TILE      18
#define MAX_UNITS 16
#define UNREACHABLE 255

// ---- Terrain --------------------------------------------------------------
// Edit these rows to redesign the map. Keep them all the same length.
//   .  = plains      F = forest
//   M  = mountain    w = water

static const char *MAP[GRID_H] = {
  "..........",
  "..FF......",
  "..FF...MM.",
  ".......MM.",
  "...wwww...",
  "...wwww...",
  ".MM.......",
  ".MM....FF.",
  ".......FF.",
  "..........",
};

typedef enum {
  T_PLAINS = 0,
  T_FOREST,
  T_MOUNTAIN,
  T_WATER,
} Terrain;

static const char *TERRAIN_NAMES[] = { "Plains", "Forest", "Mtn", "Sea" };

// Defence bonus in "steps" - each step will be 20% damage reduction later.
static const int TERRAIN_DEF[] = { 0, 1, 2, 0 };

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

typedef enum {
  U_INFANTRY = 0,
  U_BAZOOKA,
  U_TANK,
  U_ARTILLERY,
} UnitType;

static const char *UNIT_NAMES[]   = { "Inf", "Bazooka", "Tank", "Arty" };
static const char *UNIT_LETTERS[] = { "I", "B", "T", "A" };
static const int   UNIT_MOVE[]    = {  3,   2,   6,   2  };

typedef struct {
  uint8_t type;
  uint8_t team;   // 0 = you, 1 = enemy
  uint8_t x, y;
  uint8_t hp;     // 1..10
  bool    acted;  // already used this turn
  bool    alive;
} Unit;

static Unit    s_units[MAX_UNITS];
static uint8_t s_unit_count = 0;

static void add_unit(UnitType type, int team, int x, int y) {
  if (s_unit_count >= MAX_UNITS) return;
  Unit *u = &s_units[s_unit_count++];
  u->type = type;
  u->team = team;
  u->x = x;
  u->y = y;
  u->hp = 10;
  u->acted = false;
  u->alive = true;
}

static void setup_units(void) {
  s_unit_count = 0;
  // Your army, bottom-left
  add_unit(U_INFANTRY,  0, 0, 9);
  add_unit(U_INFANTRY,  0, 1, 9);
  add_unit(U_BAZOOKA,   0, 0, 8);
  add_unit(U_TANK,      0, 2, 8);
  add_unit(U_ARTILLERY, 0, 1, 7);
  // Enemy army, top-right
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

// ---- Movement -------------------------------------------------------------

// Cost to enter a tile. -1 means impassable for this unit type.
static int move_cost(UnitType type, Terrain ter) {
  switch (ter) {
    case T_WATER:
      return -1;
    case T_MOUNTAIN:
      // Only foot units climb.
      return (type == U_INFANTRY || type == U_BAZOOKA) ? 3 : -1;
    case T_FOREST:
      return 2;
    default:
      return 1;
  }
}

static uint8_t s_cost[GRID_H][GRID_W];

// Flood-fill outward from the unit, tracking cheapest cost to each tile.
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
          int nx = x + dx[d];
          int ny = y + dy[d];
          if (nx < 0 || nx >= GRID_W || ny < 0 || ny >= GRID_H) continue;

          int c = move_cost(u->type, terrain_at(nx, ny));
          if (c < 0) continue;

          // Enemies block movement; friendlies can be passed through.
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

typedef struct {
  uint8_t x, y;
  int16_t score;
} Dest;

static Dest    s_dests[GRID_W * GRID_H];
static uint8_t s_dest_count = 0;

static bool adjacent_to_enemy(int x, int y, int team) {
  const int dx[4] = { 0, 0, -1, 1 };
  const int dy[4] = { -1, 1, 0, 0 };
  for (int d = 0; d < 4; d++) {
    int i = unit_at(x + dx[d], y + dy[d]);
    if (i >= 0 && s_units[i].team != team) return true;
  }
  return false;
}

// Build the destination list, best options first, so button-cycling is short.
static void build_dests(int ui) {
  Unit *u = &s_units[ui];
  s_dest_count = 0;

  for (int y = 0; y < GRID_H; y++) {
    for (int x = 0; x < GRID_W; x++) {
      if (s_cost[y][x] == UNREACHABLE) continue;

      int occupant = unit_at(x, y);
      if (occupant >= 0 && occupant != ui) continue;

      Dest *d = &s_dests[s_dest_count++];
      d->x = x;
      d->y = y;
      d->score = 0;
      if (adjacent_to_enemy(x, y, u->team)) d->score += 100;
      d->score += TERRAIN_DEF[terrain_at(x, y)] * 10;
      d->score -= s_cost[y][x];
    }
  }

  // Insertion sort, highest score first.
  for (int i = 1; i < s_dest_count; i++) {
    Dest key = s_dests[i];
    int j = i - 1;
    while (j >= 0 && s_dests[j].score < key.score) {
      s_dests[j + 1] = s_dests[j];
      j--;
    }
    s_dests[j + 1] = key;
  }
}

// ---- Game state -----------------------------------------------------------

typedef enum {
  PHASE_BROWSE = 0,   // cycling through your units
  PHASE_MOVE,         // cycling through destinations
} Phase;

static Window *s_window;
static Layer  *s_canvas;

static Phase   s_phase = PHASE_BROWSE;
static int     s_browse = 0;      // index into s_units
static int     s_dest = 0;        // index into s_dests
static int     s_selected = -1;   // unit being moved
static int     s_turn = 1;

static void select_first_own_unit(void) {
  for (int i = 0; i < s_unit_count; i++) {
    if (s_units[i].alive && s_units[i].team == 0) { s_browse = i; return; }
  }
}

static void cycle_own_unit(int dir) {
  for (int step = 1; step <= s_unit_count; step++) {
    int i = (s_browse + dir * step + s_unit_count * 4) % s_unit_count;
    if (s_units[i].alive && s_units[i].team == 0) { s_browse = i; return; }
  }
}

static void end_turn(void) {
  for (int i = 0; i < s_unit_count; i++) s_units[i].acted = false;
  s_turn++;
  s_phase = PHASE_BROWSE;
  s_selected = -1;
  select_first_own_unit();
}

// ---- Drawing --------------------------------------------------------------

static void draw_unit(GContext *ctx, int ui, int ox, int oy) {
  Unit *u = &s_units[ui];
  GRect box = GRect(ox + u->x * TILE + 2, oy + u->y * TILE + 2, TILE - 4, TILE - 4);

  GColor fill;
  GColor ink;
  if (u->team == 0) {
    fill = u->acted ? GColorLightGray : GColorWhite;
    ink  = GColorBlack;
  } else {
    fill = GColorRed;
    ink  = GColorWhite;
  }

  graphics_context_set_fill_color(ctx, fill);
  graphics_fill_rect(ctx, box, 2, GCornersAll);

  graphics_context_set_text_color(ctx, ink);
  graphics_draw_text(ctx, UNIT_LETTERS[u->type],
                     fonts_get_system_font(FONT_KEY_GOTHIC_14),
                     GRect(ox + u->x * TILE, oy + u->y * TILE - 1, TILE, TILE),
                     GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
}

static void canvas_update(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);

  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);

  int ox = (bounds.size.w - (GRID_W * TILE)) / 2;
  int oy = 2;

  // Terrain
  for (int y = 0; y < GRID_H; y++) {
    for (int x = 0; x < GRID_W; x++) {
      GRect tile = GRect(ox + x * TILE, oy + y * TILE, TILE, TILE);
      graphics_context_set_fill_color(ctx, terrain_color(terrain_at(x, y)));
      graphics_fill_rect(ctx, tile, 0, GCornerNone);
      graphics_context_set_stroke_color(ctx, GColorBlack);
      graphics_draw_rect(ctx, tile);
    }
  }

  // Reachable tiles, while moving
  if (s_phase == PHASE_MOVE) {
    graphics_context_set_fill_color(ctx, GColorBlack);
    for (int i = 0; i < s_dest_count; i++) {
      graphics_fill_circle(ctx,
                           GPoint(ox + s_dests[i].x * TILE + TILE / 2,
                                  oy + s_dests[i].y * TILE + TILE / 2),
                           2);
    }
  }

  // Units
  for (int i = 0; i < s_unit_count; i++) {
    if (s_units[i].alive) draw_unit(ctx, i, ox, oy);
  }

  // Cursor
  int cx, cy;
  if (s_phase == PHASE_MOVE && s_dest_count > 0) {
    cx = s_dests[s_dest].x;
    cy = s_dests[s_dest].y;
  } else {
    cx = s_units[s_browse].x;
    cy = s_units[s_browse].y;
  }
  graphics_context_set_stroke_color(ctx, GColorYellow);
  graphics_context_set_stroke_width(ctx, 3);
  graphics_draw_rect(ctx, GRect(ox + cx * TILE, oy + cy * TILE, TILE, TILE));
  graphics_context_set_stroke_width(ctx, 1);

  // Status line
  static char s_status[40];
  if (s_phase == PHASE_MOVE) {
    snprintf(s_status, sizeof(s_status), "> %s  %d/%d",
             TERRAIN_NAMES[terrain_at(cx, cy)],
             s_dest + 1, s_dest_count);
  } else {
    Unit *u = &s_units[s_browse];
    snprintf(s_status, sizeof(s_status), "%s %d  T%d%s",
             UNIT_NAMES[u->type], u->hp, s_turn,
             u->acted ? " used" : "");
  }

  graphics_context_set_text_color(ctx, GColorWhite);
  graphics_draw_text(ctx, s_status,
                     fonts_get_system_font(FONT_KEY_GOTHIC_18),
                     GRect(0, oy + GRID_H * TILE + 1, bounds.size.w, 24),
                     GTextOverflowModeTrailingEllipsis,
                     GTextAlignmentCenter, NULL);
}

// ---- Buttons --------------------------------------------------------------

static void up_handler(ClickRecognizerRef r, void *context) {
  if (s_phase == PHASE_MOVE) {
    if (s_dest_count > 0) s_dest = (s_dest + s_dest_count - 1) % s_dest_count;
  } else {
    cycle_own_unit(-1);
  }
  layer_mark_dirty(s_canvas);
}

static void down_handler(ClickRecognizerRef r, void *context) {
  if (s_phase == PHASE_MOVE) {
    if (s_dest_count > 0) s_dest = (s_dest + 1) % s_dest_count;
  } else {
    cycle_own_unit(1);
  }
  layer_mark_dirty(s_canvas);
}

static void select_handler(ClickRecognizerRef r, void *context) {
  if (s_phase == PHASE_BROWSE) {
    Unit *u = &s_units[s_browse];
    if (u->acted) return;
    s_selected = s_browse;
    compute_reach(s_selected);
    build_dests(s_selected);
    s_dest = 0;
    s_phase = PHASE_MOVE;
  } else {
    if (s_dest_count > 0) {
      Unit *u = &s_units[s_selected];
      u->x = s_dests[s_dest].x;
      u->y = s_dests[s_dest].y;
      u->acted = true;
    }
    s_phase = PHASE_BROWSE;
    s_selected = -1;
  }
  layer_mark_dirty(s_canvas);
}

static void select_long_handler(ClickRecognizerRef r, void *context) {
  if (s_phase == PHASE_BROWSE) {
    end_turn();
    layer_mark_dirty(s_canvas);
  }
}

static void back_handler(ClickRecognizerRef r, void *context) {
  if (s_phase == PHASE_MOVE) {
    s_phase = PHASE_BROWSE;
    s_selected = -1;
    layer_mark_dirty(s_canvas);
  } else {
    window_stack_pop(true);   // leave the app
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
  layer_destroy(s_canvas);
}

static void init(void) {
  setup_units();
  select_first_own_unit();

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
