#include <pebble.h>

// ---- Map ------------------------------------------------------------------
// Edit these rows to redesign the map. Keep them all the same length.
//   .  = plains      F = forest
//   M  = mountain    w = water

#define GRID_W 10
#define GRID_H 10
#define TILE   18

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
  TERRAIN_PLAINS = 0,
  TERRAIN_FOREST,
  TERRAIN_MOUNTAIN,
  TERRAIN_WATER,
} Terrain;

static const char *TERRAIN_NAMES[] = { "Plains", "Forest", "Mountain", "Sea" };

static Terrain terrain_at(int x, int y) {
  switch (MAP[y][x]) {
    case 'F': return TERRAIN_FOREST;
    case 'M': return TERRAIN_MOUNTAIN;
    case 'w': return TERRAIN_WATER;
    default:  return TERRAIN_PLAINS;
  }
}

static GColor terrain_color(Terrain t) {
  switch (t) {
    case TERRAIN_FOREST:   return GColorDarkGreen;
    case TERRAIN_MOUNTAIN: return GColorWindsorTan;
    case TERRAIN_WATER:    return GColorBlueMoon;
    default:               return GColorMintGreen;
  }
}

// ---- State ----------------------------------------------------------------

static Window *s_window;
static Layer  *s_canvas;

static int s_cursor_x = 0;
static int s_cursor_y = 0;

// ---- Drawing --------------------------------------------------------------

static void canvas_update(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);

  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);

  int origin_x = (bounds.size.w - (GRID_W * TILE)) / 2;
  int origin_y = 4;

  // Draw every tile
  for (int y = 0; y < GRID_H; y++) {
    for (int x = 0; x < GRID_W; x++) {
      GRect tile = GRect(origin_x + (x * TILE), origin_y + (y * TILE), TILE, TILE);

      graphics_context_set_fill_color(ctx, terrain_color(terrain_at(x, y)));
      graphics_fill_rect(ctx, tile, 0, GCornerNone);

      graphics_context_set_stroke_color(ctx, GColorBlack);
      graphics_draw_rect(ctx, tile);
    }
  }

  // Draw the cursor on top
  GRect cursor = GRect(origin_x + (s_cursor_x * TILE),
                       origin_y + (s_cursor_y * TILE),
                       TILE, TILE);
  graphics_context_set_stroke_color(ctx, GColorWhite);
  graphics_context_set_stroke_width(ctx, 3);
  graphics_draw_rect(ctx, cursor);
  graphics_context_set_stroke_width(ctx, 1);

  // Status line underneath the map
  static char s_status[32];
  snprintf(s_status, sizeof(s_status), "%s  %d,%d",
           TERRAIN_NAMES[terrain_at(s_cursor_x, s_cursor_y)],
           s_cursor_x, s_cursor_y);

  graphics_context_set_text_color(ctx, GColorWhite);
  graphics_draw_text(ctx, s_status,
                     fonts_get_system_font(FONT_KEY_GOTHIC_18),
                     GRect(0, origin_y + (GRID_H * TILE) + 2, bounds.size.w, 24),
                     GTextOverflowModeTrailingEllipsis,
                     GTextAlignmentCenter,
                     NULL);
}

// ---- Buttons --------------------------------------------------------------
// UP / DOWN move vertically.
// SELECT tap moves right, SELECT hold moves left.
// (Temporary scheme — we'll switch to the touchscreen later.)

static void move_cursor(int dx, int dy) {
  s_cursor_x += dx;
  s_cursor_y += dy;

  if (s_cursor_x < 0)       s_cursor_x = 0;
  if (s_cursor_x > GRID_W - 1) s_cursor_x = GRID_W - 1;
  if (s_cursor_y < 0)       s_cursor_y = 0;
  if (s_cursor_y > GRID_H - 1) s_cursor_y = GRID_H - 1;

  layer_mark_dirty(s_canvas);
}

static void up_handler(ClickRecognizerRef r, void *context)     { move_cursor(0, -1); }
static void down_handler(ClickRecognizerRef r, void *context)   { move_cursor(0,  1); }
static void right_handler(ClickRecognizerRef r, void *context)  { move_cursor(1,  0); }
static void left_handler(ClickRecognizerRef r, void *context)   { move_cursor(-1, 0); }

static void click_config(void *context) {
  window_single_repeating_click_subscribe(BUTTON_ID_UP, 120, up_handler);
  window_single_repeating_click_subscribe(BUTTON_ID_DOWN, 120, down_handler);
  window_single_click_subscribe(BUTTON_ID_SELECT, right_handler);
  window_long_click_subscribe(BUTTON_ID_SELECT, 400, left_handler, NULL);
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
