#include <pebble.h>
#include <string.h>

// ===========================================================================
//  WristWars - step 11: artillery rework, terrain-aware AI, denser maps
// ===========================================================================

#define GRID_W    10
#define GRID_H    10
#define TILE      18
#define MAX_UNITS 16
#define MAX_SITES 12
#define MAX_REACH (GRID_W * GRID_H)
#define UNREACHABLE 255
#define AI_STEP_MS  450
#define CAPTURE_GOAL 20

#define SAVE_VERSION     6
#define PERSIST_KEY_SAVE 1
#define PERSIST_KEY_PROG 2

typedef enum { U_INFANTRY = 0, U_BAZOOKA, U_TANK, U_ARTILLERY } UnitType;

typedef struct { uint8_t type, team, x, y; } Deploy;

typedef struct {
  const char  *name;
  const char  *rows[GRID_H];
  const Deploy *deploys;
  uint8_t      deploy_count;
  bool         campaign;
  bool         skirmish;
  const char  *hint;
} Level;

// ---- Maps -----------------------------------------------------------------
//   .  = plains     F = forest     M = mountain    w = water
//   c  = city       h = your HQ    e = enemy HQ

static const Deploy s_dep0[] = { { U_INFANTRY, 0, 1, 8 }, { U_INFANTRY, 0, 2, 8 }, { U_INFANTRY, 1, 7, 2 } };
static const Deploy s_dep1[] = { { U_INFANTRY, 0, 1, 8 }, { U_INFANTRY, 0, 2, 8 }, { U_BAZOOKA, 0, 1, 7 }, { U_INFANTRY, 1, 3, 2 }, { U_INFANTRY, 1, 4, 3 } };
static const Deploy s_dep2[] = { { U_BAZOOKA, 0, 1, 8 }, { U_INFANTRY, 0, 2, 8 }, { U_INFANTRY, 0, 1, 7 }, { U_TANK, 1, 7, 2 }, { U_INFANTRY, 1, 8, 3 } };
static const Deploy s_dep3[] = { { U_ARTILLERY, 0, 4, 7 }, { U_INFANTRY, 0, 2, 7 }, { U_BAZOOKA, 0, 6, 7 }, { U_INFANTRY, 1, 4, 2 }, { U_INFANTRY, 1, 6, 2 }, { U_TANK, 1, 2, 2 } };
static const Deploy s_dep4[] = { { U_INFANTRY, 0, 1, 8 }, { U_INFANTRY, 0, 2, 8 }, { U_BAZOOKA, 0, 1, 7 }, { U_TANK, 0, 3, 8 }, { U_INFANTRY, 1, 8, 1 }, { U_INFANTRY, 1, 7, 1 }, { U_BAZOOKA, 1, 8, 2 }, { U_TANK, 1, 6, 1 } };
static const Deploy s_dep5[] = { { U_INFANTRY, 0, 0, 9 }, { U_INFANTRY, 0, 1, 9 }, { U_BAZOOKA, 0, 0, 8 }, { U_TANK, 0, 2, 8 }, { U_ARTILLERY, 0, 1, 7 }, { U_INFANTRY, 1, 9, 0 }, { U_INFANTRY, 1, 8, 0 }, { U_BAZOOKA, 1, 9, 1 }, { U_TANK, 1, 7, 1 }, { U_ARTILLERY, 1, 8, 2 } };
static const Deploy s_dep6[] = { { U_INFANTRY, 0, 0, 9 }, { U_INFANTRY, 0, 1, 9 }, { U_BAZOOKA, 0, 2, 9 }, { U_TANK, 0, 0, 8 }, { U_ARTILLERY, 0, 2, 8 }, { U_INFANTRY, 1, 9, 0 }, { U_INFANTRY, 1, 8, 0 }, { U_BAZOOKA, 1, 7, 0 }, { U_TANK, 1, 9, 1 }, { U_ARTILLERY, 1, 7, 1 } };

static const Level LEVELS[] = {
  { "First Contact", {
      "..........",
      "...F......",
      "..FFF..F..",
      "...F......",
      "..F..FF...",
      "...FF..F..",
      "......F...",
      "..F...FF..",
      "....F.....",
      ".........." },
    s_dep0, ARRAY_LENGTH(s_dep0), true, false, "Forest blunts hits. Watch the numbers." },
  { "High Ground", {
      "....M.....",
      "..MMMM....",
      "..MM.MM...",
      "...MM..F..",
      "......F...",
      "..FF...MM.",
      "....F..MM.",
      "..........",
      "..........",
      ".........." },
    s_dep1, ARRAY_LENGTH(s_dep1), true, false, "Mountains cut damage 40%." },
  { "Armour", {
      "..........",
      "..MM......",
      "..MM....F.",
      "......F...",
      "..FF...MM.",
      ".....F.MM.",
      "..MM......",
      "....FF....",
      "..........",
      ".........." },
    s_dep2, ARRAY_LENGTH(s_dep2), true, false, "Bazookas shred armour. Hills stop it." },
  { "Shellfire", {
      "..........",
      "..........",
      "..........",
      "..FF..FF..",
      ".www..www.",
      "..........",
      "..F....F..",
      "..........",
      "....M.....",
      ".........." },
    s_dep3, ARRAY_LENGTH(s_dep3), true, false, "Artillery hits hardest standing still." },
  { "Take the City", {
      "..........",
      "..c....c..",
      "...F.F....",
      "..FF...MM.",
      "....FF....",
      "..MM...FF.",
      "....F.....",
      "..c....c..",
      "..........",
      ".........." },
    s_dep4, ARRAY_LENGTH(s_dep4), true, false, "Cities heal. Infantry capture." },
  { "Headquarters", {
      ".......F..",
      "..c.....e.",
      "...cM.F...",
      ".F.ww...F.",
      "..Mwww.c.F",
      "F.c.wwwM..",
      ".F...ww.F.",
      "...F.Mc...",
      ".h.....c..",
      "..F......." },
    s_dep5, ARRAY_LENGTH(s_dep5), true, true, "Take their HQ, or wipe them out." },
  { "Crossroads", {
      "..........",
      ".....F..e.",
      ".F...c....",
      "..cw...F..",
      "M..wM.w.c.",
      ".c.w.Mw..M",
      "..F...wc..",
      "....c...F.",
      ".h..F.....",
      ".........." },
    s_dep6, ARRAY_LENGTH(s_dep6), false, true, "Three lanes. Pick one." },
};

#define LEVEL_COUNT ((int)ARRAY_LENGTH(LEVELS))

static int  s_level_idx = 5;
static bool s_campaign   = false;

static const Level *cur(void) { return &LEVELS[s_level_idx]; }

// ---- Terrain --------------------------------------------------------------

typedef enum {
  T_PLAINS = 0, T_FOREST, T_MOUNTAIN, T_WATER, T_CITY, T_HQ
} Terrain;

static const char *TERRAIN_NAMES[] = {
  "Plains", "Forest", "Mountain", "Sea", "City", "HQ"
};
static const int TERRAIN_DEF[] = { 0, 1, 2, 0, 1, 2 };

static Terrain terrain_at(int x, int y) {
  switch (cur()->rows[y][x]) {
    case 'F': return T_FOREST;
    case 'M': return T_MOUNTAIN;
    case 'w': return T_WATER;
    case 'c': return T_CITY;
    case 'h': case 'e': return T_HQ;
    default:  return T_PLAINS;
  }
}

static GColor terrain_color(Terrain t) {
  switch (t) {
    case T_FOREST:   return GColorDarkGreen;
    case T_MOUNTAIN: return GColorWindsorTan;
    case T_WATER:    return GColorBlueMoon;
    case T_CITY:     return GColorLightGray;
    case T_HQ:       return GColorDarkGray;
    default:         return GColorMintGreen;
  }
}

// ---- Capturable sites -----------------------------------------------------

#define SITE_CITY 0
#define SITE_HQ   1
#define OWNER_NEUTRAL 2

typedef struct {
  uint8_t x, y, kind, owner, progress, cap_team, home;
} Site;

static Site s_sites[MAX_SITES];
static int  s_site_count = 0;

static void init_sites(void) {
  s_site_count = 0;
  for (int y = 0; y < GRID_H; y++) {
    for (int x = 0; x < GRID_W; x++) {
      char c = cur()->rows[y][x];
      if (c != 'c' && c != 'h' && c != 'e') continue;
      if (s_site_count >= MAX_SITES) continue;

      Site *s = &s_sites[s_site_count++];
      s->x = x; s->y = y;
      s->progress = 0;
      s->cap_team = OWNER_NEUTRAL;
      if (c == 'c') {
        s->kind = SITE_CITY; s->owner = OWNER_NEUTRAL; s->home = OWNER_NEUTRAL;
      } else {
        s->kind = SITE_HQ;
        s->owner = (c == 'h') ? 0 : 1;
        s->home  = s->owner;
      }
    }
  }
}

static int site_at(int x, int y) {
  for (int i = 0; i < s_site_count; i++) {
    if (s_sites[i].x == x && s_sites[i].y == y) return i;
  }
  return -1;
}

// ---- Units ----------------------------------------------------------------

static const char *UNIT_NAMES[] = { "Infantry", "Bazooka", "Tank", "Artillery" };
static const int   UNIT_MOVE[]  = {  3,   2,   6,   2  };
static const int   UNIT_VALUE[] = {  1,   2,   3,   3  };

static const int DMG[4][4] = {
  //        Inf  Bzka Tank Arty
  /*Inf */ {  5,   5,   1,   6 },
  /*Bzka*/ {  6,   5,   7,   7 },
  /*Tank*/ {  7,   7,   5,   8 },
  /*Arty*/ {  8,   8,   8,   8 },
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

static int s_turn        = 1;
static int s_lost_player = 0;
static int s_lost_enemy  = 0;

static bool is_indirect(UnitType t) { return t == U_ARTILLERY; }
static int  min_range(UnitType t)   { return is_indirect(t) ? 2 : 1; }
static int  max_range(UnitType t)   { return is_indirect(t) ? 4 : 1; }

static int abs_i(int v) { return v < 0 ? -v : v; }

static int dist_xy(int ax, int ay, int bx, int by) {
  return abs_i(ax - bx) + abs_i(ay - by);
}

static int dist_between(const Unit *a, const Unit *b) {
  return dist_xy(a->x, a->y, b->x, b->y);
}

static void setup_units_from_level(void) {
  const Level *lv = cur();
  s_unit_count = 0;
  for (int i = 0; i < lv->deploy_count && s_unit_count < MAX_UNITS; i++) {
    const Deploy *d = &lv->deploys[i];
    Unit *u = &s_units[s_unit_count++];
    u->type = d->type; u->team = d->team;
    u->x = d->x; u->y = d->y;
    u->hp = 10; u->acted = false; u->alive = true;
  }
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

static bool can_capture_here(int ui) {
  Unit *u = &s_units[ui];
  if (u->type != U_INFANTRY) return false;
  int si = site_at(u->x, u->y);
  return (si >= 0 && s_sites[si].owner != u->team);
}

// ---- Combat ---------------------------------------------------------------

// Indirect fire loses 40% if the gun moved this turn. Everything else
// ignores the flag, so the rest of the game is unchanged.
static int damage_from(const Unit *att, const Unit *def, bool moved) {
  int base = DMG[att->type][def->type];
  if (base <= 0) return 0;
  int dmg = base * att->hp / 10;
  int d   = TERRAIN_DEF[terrain_at(def->x, def->y)];
  dmg = dmg * (10 - 2 * d) / 10;
  if (moved && is_indirect((UnitType)att->type)) dmg = dmg * 6 / 10;
  if (dmg < 1) dmg = 1;
  return dmg;
}

static int compute_damage(const Unit *att, const Unit *def) {
  return damage_from(att, def, false);
}

static bool can_counter(const Unit *def, const Unit *att) {
  if (is_indirect(def->type)) return false;
  if (is_indirect(att->type)) return false;
  return dist_between(def, att) <= max_range(def->type);
}

static void clear_capture_at(int x, int y) {
  int si = site_at(x, y);
  if (si >= 0) { s_sites[si].progress = 0; s_sites[si].cap_team = OWNER_NEUTRAL; }
}

static void kill_unit(Unit *u) {
  u->hp = 0;
  u->alive = false;
  clear_capture_at(u->x, u->y);
  if (u->team == 0) s_lost_player++;
  else              s_lost_enemy++;
}

static void resolve_attack(int ai, int di, bool moved) {
  Unit *a = &s_units[ai];
  Unit *d = &s_units[di];
  bool died = false, player_hurt = false;

  int dmg = damage_from(a, d, moved);
  d->hp -= dmg;
  if (d->team == 0) player_hurt = true;
  if (d->hp <= 0) { kill_unit(d); died = true; }

  if (!died && can_counter(d, a)) {
    int back = compute_damage(d, a);
    a->hp -= back;
    if (a->team == 0) player_hurt = true;
    if (a->hp <= 0) { kill_unit(a); died = true; }
  }

  if (died)             vibes_double_pulse();
  else if (player_hurt) vibes_short_pulse();
}

static void do_capture(int ui) {
  Unit *u = &s_units[ui];
  int si = site_at(u->x, u->y);
  if (si < 0) return;
  Site *s = &s_sites[si];

  if (s->cap_team != u->team) { s->progress = 0; s->cap_team = u->team; }
  s->progress += u->hp;

  if (s->progress >= CAPTURE_GOAL) {
    s->owner = u->team;
    s->progress = 0;
    s->cap_team = OWNER_NEUTRAL;
    vibes_double_pulse();
  } else {
    vibes_short_pulse();
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
static uint8_t s_scratch[GRID_H][GRID_W];

static void compute_reach_into(int ui, uint8_t out[GRID_H][GRID_W]) {
  Unit *u = &s_units[ui];

  for (int y = 0; y < GRID_H; y++) {
    for (int x = 0; x < GRID_W; x++) out[y][x] = UNREACHABLE;
  }
  out[u->y][u->x] = 0;

  int budget = UNIT_MOVE[u->type];
  const int dx[4] = { 0, 0, -1, 1 };
  const int dy[4] = { -1, 1, 0, 0 };

  bool changed = true;
  while (changed) {
    changed = false;
    for (int y = 0; y < GRID_H; y++) {
      for (int x = 0; x < GRID_W; x++) {
        if (out[y][x] == UNREACHABLE) continue;
        for (int d = 0; d < 4; d++) {
          int nx = x + dx[d], ny = y + dy[d];
          if (nx < 0 || nx >= GRID_W || ny < 0 || ny >= GRID_H) continue;
          int c = move_cost(u->type, terrain_at(nx, ny));
          if (c < 0) continue;
          int other = unit_at(nx, ny);
          if (other >= 0 && s_units[other].team != u->team) continue;
          int nc = out[y][x] + c;
          if (nc <= budget && nc < out[ny][nx]) {
            out[ny][nx] = nc;
            changed = true;
          }
        }
      }
    }
  }
}

static void compute_reach(int ui) { compute_reach_into(ui, s_cost); }

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

// True if an indirect unit sitting on (x,y) has anything in its range band.
static bool can_shell_from(int ui, int x, int y) {
  Unit *u = &s_units[ui];
  for (int i = 0; i < s_unit_count; i++) {
    if (!s_units[i].alive || s_units[i].team == u->team) continue;
    int d = dist_xy(x, y, s_units[i].x, s_units[i].y);
    if (d >= min_range((UnitType)u->type) && d <= max_range((UnitType)u->type)) return true;
  }
  return false;
}

// ---- Threat map -----------------------------------------------------------

static bool s_threat[GRID_H][GRID_W];
static bool s_show_threat = false;

static void recompute_threat(void) {
  for (int y = 0; y < GRID_H; y++) {
    for (int x = 0; x < GRID_W; x++) s_threat[y][x] = false;
  }

  for (int i = 0; i < s_unit_count; i++) {
    if (!s_units[i].alive || s_units[i].team != 1) continue;
    Unit *e = &s_units[i];

    if (is_indirect(e->type)) {
      for (int y = 0; y < GRID_H; y++) {
        for (int x = 0; x < GRID_W; x++) {
          int d = dist_xy(x, y, e->x, e->y);
          if (d >= min_range(e->type) && d <= max_range(e->type)) s_threat[y][x] = true;
        }
      }
      continue;
    }

    compute_reach_into(i, s_scratch);
    const int dx[4] = { 0, 0, -1, 1 };
    const int dy[4] = { -1, 1, 0, 0 };
    for (int y = 0; y < GRID_H; y++) {
      for (int x = 0; x < GRID_W; x++) {
        if (s_scratch[y][x] == UNREACHABLE) continue;
        for (int d = 0; d < 4; d++) {
          int nx = x + dx[d], ny = y + dy[d];
          if (nx < 0 || nx >= GRID_W || ny < 0 || ny >= GRID_H) continue;
          s_threat[ny][nx] = true;
        }
      }
    }
  }
}

// ---- Two-stage destination picking ----------------------------------------

#define GRP_ATTACK 0
#define GRP_STAY   9

static const char *GROUP_NAMES[] = {
  "Attack from", "North", "North-East", "East", "South-East",
  "South", "South-West", "West", "North-West", "Hold position",
};

static int heading_of(int dx, int dy) {
  if (dx == 0 && dy == 0) return GRP_STAY;
  int adx = abs_i(dx), ady = abs_i(dy);
  if (adx > 2 * ady) return (dx > 0) ? 3 : 7;
  if (ady > 2 * adx) return (dy < 0) ? 1 : 5;
  if (dx > 0)        return (dy < 0) ? 2 : 4;
  return                    (dy < 0) ? 8 : 6;
}

static Tile    s_reach[MAX_REACH];
static uint8_t s_reach_grp[MAX_REACH];
static uint8_t s_reach_cost[MAX_REACH];
static int     s_reach_count = 0;

static uint8_t s_groups[10];
static int     s_group_count = 0;
static int     s_group = 0;

static Tile s_dests[MAX_REACH];
static int  s_dest_count = 0;
static int  s_dest = 0;

static void classify_reach(int ui) {
  Unit *u = &s_units[ui];
  s_reach_count = 0;

  for (int y = 0; y < GRID_H; y++) {
    for (int x = 0; x < GRID_W; x++) {
      if (s_cost[y][x] == UNREACHABLE) continue;
      if (!tile_free(x, y, ui)) continue;

      int grp;
      if (is_indirect(u->type)) {
        // Group 0 for a gun means "don't move, fire at full power".
        bool home = (x == u->x && y == u->y);
        grp = (home && can_shell_from(ui, x, y)) ? GRP_ATTACK
                                                 : heading_of(x - u->x, y - u->y);
      } else if (adjacent_to_enemy(x, y, u->team)) {
        grp = GRP_ATTACK;
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

  s_group_count = 0;
  for (int g = 0; g <= 9; g++) {
    for (int i = 0; i < s_reach_count; i++) {
      if (s_reach_grp[i] == g) { s_groups[s_group_count++] = g; break; }
    }
  }
  s_group = 0;
}

static int group_tile_count(int g) {
  int n = 0;
  for (int i = 0; i < s_reach_count; i++) if (s_reach_grp[i] == g) n++;
  return n;
}

static int group_preview(int g) {
  int best = -1, best_cost = -1;
  for (int i = 0; i < s_reach_count; i++) {
    if (s_reach_grp[i] != g) continue;
    if (s_reach_cost[i] > best_cost) { best_cost = s_reach_cost[i]; best = i; }
  }
  return best;
}

static void build_dests_for_group(int g) {
  s_dest_count = 0;
  for (int i = 0; i < s_reach_count; i++) {
    if (s_reach_grp[i] != g) continue;
    int c = s_reach_cost[i];
    int j = s_dest_count - 1;
    while (j >= 0 && s_cost[s_dests[j].y][s_dests[j].x] < c) {
      s_dests[j + 1] = s_dests[j];
      j--;
    }
    s_dests[j + 1] = s_reach[i];
    s_dest_count++;
  }
  s_dest = 0;
}

// ---- Targets and actions --------------------------------------------------

static int     s_targets[MAX_UNITS];
static uint8_t s_target_count = 0;
static int     s_target = 0;

#define ACT_ATTACK  0
#define ACT_CAPTURE 1
#define ACT_WAIT    2

static const char *ACTION_NAMES[] = { "Attack", "Capture", "Wait" };

static uint8_t s_actions[3];
static int     s_action_count = 0;
static int     s_action = 0;

static void build_targets(int ui, bool moved) {
  Unit *u = &s_units[ui];
  s_target_count = 0;
  (void)moved;                 // guns may now fire after moving, at reduced power

  for (int i = 0; i < s_unit_count; i++) {
    if (!s_units[i].alive || s_units[i].team == u->team) continue;
    int d = dist_between(u, &s_units[i]);
    if (d >= min_range(u->type) && d <= max_range(u->type)) {
      s_targets[s_target_count++] = i;
    }
  }
}

static void build_actions(int ui, bool moved) {
  build_targets(ui, moved);
  s_action_count = 0;
  if (s_target_count > 0)   s_actions[s_action_count++] = ACT_ATTACK;
  if (can_capture_here(ui)) s_actions[s_action_count++] = ACT_CAPTURE;
  s_actions[s_action_count++] = ACT_WAIT;
  s_action = 0;
}

// ---- Campaign progress ----------------------------------------------------

static uint32_t s_progress = 0;
static uint8_t  s_camp[LEVEL_COUNT];
static int      s_camp_count = 0;

static void build_campaign_list(void) {
  s_camp_count = 0;
  for (int i = 0; i < LEVEL_COUNT; i++) {
    if (LEVELS[i].campaign) s_camp[s_camp_count++] = (uint8_t)i;
  }
}

static void load_progress(void) {
  if (persist_exists(PERSIST_KEY_PROG)) {
    s_progress = (uint32_t)persist_read_int(PERSIST_KEY_PROG);
  }
}

static void save_progress(void) {
  persist_write_int(PERSIST_KEY_PROG, (int)s_progress);
}

static bool level_cleared(int idx) { return (s_progress >> idx) & 1u; }

static bool camp_unlocked(int ci) {
  if (ci <= 0) return true;
  return level_cleared(s_camp[ci - 1]);
}

// ---- Game state -----------------------------------------------------------

typedef enum {
  PHASE_MAIN = 0, PHASE_LEVELS, PHASE_PAUSE,
  PHASE_BROWSE, PHASE_GROUP, PHASE_MOVE, PHASE_ACTION,
  PHASE_TARGET, PHASE_ENEMY, PHASE_OVER,
} Phase;

static Window   *s_window;
static Layer    *s_canvas;
static AppTimer *s_ai_timer = NULL;

static Phase s_phase       = PHASE_MAIN;
static bool  s_confirm_end = false;
static int   s_browse      = 0;
static int   s_selected    = -1;
static bool  s_moved       = false;
static int   s_winner      = -1;

// Front-end menus
#define MM_CONTINUE 0
#define MM_CAMPAIGN 1
#define MM_SKIRMISH 2
#define MM_EXIT     3

static char    s_mm_label[4][14];
static uint8_t s_mm_act[4];
static int     s_mm_count = 0;
static int     s_mm_sel   = 0;
static bool    s_have_save = false;

static char s_lv_label[LEVEL_COUNT][22];
static int  s_lv_sel = 0;

static const char *PAUSE_ITEMS[] = { "Resume", "Restart", "Main menu" };
#define PAUSE_COUNT 3
static int s_pause_sel = 0;

static Unit s_undo_units[MAX_UNITS];
static Site s_undo_sites[MAX_SITES];
static int  s_undo_browse = 0;
static int  s_undo_lost_player = 0;
static int  s_undo_lost_enemy  = 0;
static bool s_undo_ok = false;

static void snapshot_for_undo(void) {
  memcpy(s_undo_units, s_units, sizeof(s_units));
  memcpy(s_undo_sites, s_sites, sizeof(s_sites));
  s_undo_browse      = s_browse;
  s_undo_lost_player = s_lost_player;
  s_undo_lost_enemy  = s_lost_enemy;
  s_undo_ok = true;
}

static void build_main_menu(void) {
  s_mm_count = 0;
  if (s_have_save) {
    strncpy(s_mm_label[s_mm_count], "Continue", sizeof(s_mm_label[0]) - 1);
    s_mm_act[s_mm_count++] = MM_CONTINUE;
  }
  strncpy(s_mm_label[s_mm_count], "Campaign", sizeof(s_mm_label[0]) - 1);
  s_mm_act[s_mm_count++] = MM_CAMPAIGN;
  strncpy(s_mm_label[s_mm_count], "Skirmish", sizeof(s_mm_label[0]) - 1);
  s_mm_act[s_mm_count++] = MM_SKIRMISH;
  strncpy(s_mm_label[s_mm_count], "Exit", sizeof(s_mm_label[0]) - 1);
  s_mm_act[s_mm_count++] = MM_EXIT;
  if (s_mm_sel >= s_mm_count) s_mm_sel = 0;
}

static void build_level_menu(void) {
  for (int ci = 0; ci < s_camp_count; ci++) {
    int idx = s_camp[ci];
    const char *mark = "";
    if (level_cleared(idx))      mark = " done";
    else if (!camp_unlocked(ci)) mark = " lock";
    snprintf(s_lv_label[ci], sizeof(s_lv_label[0]), "%d %s%s",
             ci + 1, LEVELS[idx].name, mark);
  }
  if (s_lv_sel >= s_camp_count) s_lv_sel = 0;
}

static void check_game_over(void) {
  for (int i = 0; i < s_site_count; i++) {
    if (s_sites[i].kind != SITE_HQ) continue;
    if (s_sites[i].owner != s_sites[i].home) {
      s_winner = s_sites[i].owner;
      s_phase = PHASE_OVER;
      break;
    }
  }
  if (s_phase != PHASE_OVER) {
    if (count_alive(1) == 0)      { s_winner = 0; s_phase = PHASE_OVER; }
    else if (count_alive(0) == 0) { s_winner = 1; s_phase = PHASE_OVER; }
  }

  if (s_phase == PHASE_OVER && s_winner == 0 && s_campaign &&
      !level_cleared(s_level_idx)) {
    s_progress |= (1u << s_level_idx);
    save_progress();
    build_level_menu();
  }
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

static void turn_upkeep(int team) {
  for (int i = 0; i < s_site_count; i++) {
    Site *s = &s_sites[i];
    if (s->progress == 0) continue;
    int ui = unit_at(s->x, s->y);
    if (ui < 0 || s_units[ui].team != s->cap_team) {
      s->progress = 0;
      s->cap_team = OWNER_NEUTRAL;
    }
  }
  for (int i = 0; i < s_unit_count; i++) {
    Unit *u = &s_units[i];
    if (!u->alive || u->team != team || u->hp >= 10) continue;
    int si = site_at(u->x, u->y);
    if (si >= 0 && s_sites[si].owner == team) {
      u->hp += 3;
      if (u->hp > 10) u->hp = 10;
    }
  }
}

// ---- Save / restore -------------------------------------------------------

typedef struct {
  uint8_t  version;
  uint8_t  unit_count;
  uint8_t  site_count;
  int8_t   winner;
  uint16_t turn;
  uint8_t  lost_player;
  uint8_t  lost_enemy;
  uint8_t  level;
  uint8_t  campaign;
  uint8_t  pad0, pad1;
  Unit     units[MAX_UNITS];
  Site     sites[MAX_SITES];
} SaveBlob;

static void save_game(void) {
  if (s_phase == PHASE_ENEMY) return;

  SaveBlob b;
  memset(&b, 0, sizeof(b));
  b.version     = SAVE_VERSION;
  b.unit_count  = s_unit_count;
  b.site_count  = (uint8_t)s_site_count;
  b.winner      = (int8_t)s_winner;
  b.turn        = (uint16_t)s_turn;
  b.lost_player = (uint8_t)s_lost_player;
  b.lost_enemy  = (uint8_t)s_lost_enemy;
  b.level       = (uint8_t)s_level_idx;
  b.campaign    = s_campaign ? 1 : 0;
  memcpy(b.units, s_units, sizeof(s_units));
  memcpy(b.sites, s_sites, sizeof(s_sites));
  persist_write_data(PERSIST_KEY_SAVE, &b, sizeof(b));
}

static bool load_game(void) {
  if (!persist_exists(PERSIST_KEY_SAVE)) return false;

  SaveBlob b;
  int n = persist_read_data(PERSIST_KEY_SAVE, &b, sizeof(b));
  if (n != (int)sizeof(b)) return false;
  if (b.version != SAVE_VERSION) return false;
  if (b.unit_count == 0 || b.unit_count > MAX_UNITS) return false;
  if (b.site_count > MAX_SITES) return false;
  if (b.level >= LEVEL_COUNT) return false;

  s_level_idx   = b.level;
  s_campaign    = (b.campaign != 0);
  s_unit_count  = b.unit_count;
  s_site_count  = b.site_count;
  memcpy(s_units, b.units, sizeof(s_units));
  memcpy(s_sites, b.sites, sizeof(s_sites));
  s_winner      = b.winner;
  s_turn        = b.turn > 0 ? b.turn : 1;
  s_lost_player = b.lost_player;
  s_lost_enemy  = b.lost_enemy;
  return true;
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
  bool best_capture = false;
  bool best_moved   = false;

  for (int t = 0; t < s_ai_tile_count; t++) {
    int x = s_ai_tiles[t].x;
    int y = s_ai_tiles[t].y;
    bool moved = (x != home_x || y != home_y);

    u->x = x; u->y = y;
    int def_bonus = TERRAIN_DEF[terrain_at(x, y)] * 20;

    {
      for (int i = 0; i < s_unit_count; i++) {
        if (!s_units[i].alive || s_units[i].team == u->team) continue;
        int d = dist_between(u, &s_units[i]);
        if (d < min_range(u->type) || d > max_range(u->type)) continue;

        Unit *victim = &s_units[i];
        int dmg = damage_from(u, victim, moved);
        int score = 1000 + dmg * 10 + def_bonus;

        if (dmg >= victim->hp) score += 300 + UNIT_VALUE[victim->type] * 60;
        if (can_counter(victim, u)) {
          int back = compute_damage(victim, u);
          score -= back * 12;
          if (back >= u->hp) score -= 400;
        }

        if (score > best_score) {
          best_score = score; best_x = x; best_y = y;
          best_target = i; best_capture = false; best_moved = moved;
        }
      }
    }

    int si = site_at(x, y);
    if (si >= 0 && u->type == U_INFANTRY && s_sites[si].owner != u->team) {
      Site *s = &s_sites[si];
      int score;
      if (s->kind == SITE_HQ) {
        bool finishes = (s->cap_team == u->team && s->progress + u->hp >= CAPTURE_GOAL);
        score = finishes ? 6000 : 2600;
      } else {
        score = 850 + s->progress * 8;
      }
      score += def_bonus;
      if (score > best_score) {
        best_score = score; best_x = x; best_y = y;
        best_target = -1; best_capture = true; best_moved = moved;
      }
    }

    // No shot from here: walk toward the fight, but pay real attention to cover.
    // Guns instead hold a standoff distance in the middle of their range band.
    int cover = TERRAIN_DEF[terrain_at(x, y)] * 12;
    int approach;
    if (is_indirect(u->type)) {
      int ideal = (min_range(u->type) + max_range(u->type)) / 2;
      approach = -abs_i(nearest_foe_dist(x, y, u->team) - ideal) * 12 + cover;
    } else {
      approach = -nearest_foe_dist(x, y, u->team) * 10 + cover;
    }
    if (approach > best_score) {
      best_score = approach; best_x = x; best_y = y;
      best_target = -1; best_capture = false; best_moved = moved;
    }
  }

  u->x = best_x; u->y = best_y;
  if (best_target >= 0)  resolve_attack(ui, best_target, best_moved);
  else if (best_capture) do_capture(ui);
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
  s_turn++;
  turn_upkeep(0);
  s_phase = PHASE_BROWSE;
  s_selected = -1;
  s_confirm_end = false;
  s_undo_ok = false;
  focus_next_unacted(s_unit_count - 1);
  recompute_threat();
  save_game();
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
    vibes_short_pulse();
    layer_mark_dirty(s_canvas);
    return;
  }

  ai_act(next);
  check_game_over();
  recompute_threat();
  layer_mark_dirty(s_canvas);
  if (s_phase == PHASE_ENEMY) schedule_ai();
  else                        save_game();
}

static void begin_enemy_turn(void) {
  for (int i = 0; i < s_unit_count; i++) {
    if (s_units[i].team == 1) s_units[i].acted = false;
  }
  turn_upkeep(1);
  s_selected = -1;
  s_confirm_end = false;
  s_undo_ok = false;
  s_phase = PHASE_ENEMY;
  schedule_ai();
}

static void start_level(int idx, bool campaign) {
  if (s_ai_timer) { app_timer_cancel(s_ai_timer); s_ai_timer = NULL; }
  s_level_idx = idx;
  s_campaign  = campaign;
  init_sites();
  setup_units_from_level();
  s_turn = 1;
  s_lost_player = 0;
  s_lost_enemy = 0;
  s_winner = -1;
  s_selected = -1;
  s_moved = false;
  s_confirm_end = false;
  s_undo_ok = false;
  s_phase = PHASE_BROWSE;
  focus_next_unacted(s_unit_count - 1);
  recompute_threat();
}

static void start_skirmish(void) {
  uint8_t pool[LEVEL_COUNT];
  int n = 0;
  for (int i = 0; i < LEVEL_COUNT; i++) if (LEVELS[i].skirmish) pool[n++] = (uint8_t)i;
  if (n == 0) { start_level(0, false); return; }
  int pick = pool[(int)(time(NULL) % (unsigned)n)];
  start_level(pick, false);
}

// ---- Drawing --------------------------------------------------------------

static GColor owner_color(int owner) {
  if (owner == 0) return GColorWhite;
  if (owner == 1) return GColorRed;
  return GColorBlack;
}

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

static void draw_site_flag(GContext *ctx, const Site *s, int px, int py) {
  int pole = px + 4;
  graphics_context_set_stroke_color(ctx, GColorBlack);
  graphics_draw_line(ctx, GPoint(pole, py + 3), GPoint(pole, py + TILE - 4));

  graphics_context_set_fill_color(ctx, owner_color(s->owner));
  graphics_fill_rect(ctx, GRect(pole + 1, py + 3, 7, 5), 0, GCornerNone);
  graphics_context_set_stroke_color(ctx, GColorBlack);
  graphics_draw_rect(ctx, GRect(pole + 1, py + 3, 7, 5));

  if (s->kind == SITE_HQ) {
    graphics_context_set_fill_color(ctx, owner_color(s->owner));
    graphics_fill_rect(ctx, GRect(pole + 1, py + 9, 5, 3), 0, GCornerNone);
    graphics_context_set_stroke_color(ctx, GColorBlack);
    graphics_draw_rect(ctx, GRect(pole + 1, py + 9, 5, 3));
  }
}

// Silhouettes instead of letters. Kept inside py+3..py+11 so the health bar
// underneath stays clear.
static void draw_unit_icon(GContext *ctx, UnitType t, int px, int py, GColor ink) {
  int cx = px + TILE / 2;
  graphics_context_set_fill_color(ctx, ink);
  graphics_context_set_stroke_color(ctx, ink);

  switch (t) {
    case U_INFANTRY:                                  // helmet and shoulders
      graphics_fill_circle(ctx, GPoint(cx, py + 5), 2);
      graphics_fill_rect(ctx, GRect(cx - 3, py + 8, 7, 3), 1, GCornersAll);
      break;

    case U_BAZOOKA:                                   // same, with a tube
      graphics_fill_circle(ctx, GPoint(cx - 1, py + 5), 2);
      graphics_fill_rect(ctx, GRect(cx - 4, py + 8, 7, 3), 1, GCornersAll);
      graphics_context_set_stroke_width(ctx, 2);
      graphics_draw_line(ctx, GPoint(cx - 4, py + 9), GPoint(cx + 4, py + 3));
      graphics_context_set_stroke_width(ctx, 1);
      break;

    case U_TANK:                                      // hull, turret, barrel
      graphics_fill_rect(ctx, GRect(cx - 5, py + 7, 10, 4), 1, GCornersAll);
      graphics_fill_rect(ctx, GRect(cx - 2, py + 4, 4, 3), 1, GCornersAll);
      graphics_context_set_stroke_width(ctx, 2);
      graphics_draw_line(ctx, GPoint(cx + 2, py + 5), GPoint(cx + 5, py + 5));
      graphics_context_set_stroke_width(ctx, 1);
      break;

    case U_ARTILLERY:                                 // carriage and long gun
      graphics_fill_rect(ctx, GRect(cx - 5, py + 8, 10, 3), 1, GCornersAll);
      graphics_context_set_stroke_width(ctx, 2);
      graphics_draw_line(ctx, GPoint(cx - 3, py + 9), GPoint(cx + 4, py + 3));
      graphics_context_set_stroke_width(ctx, 1);
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

  draw_unit_icon(ctx, (UnitType)u->type, px, py, ink);

  if (u->hp < 10) {
    int w = (TILE - 8) * u->hp / 10;
    graphics_context_set_fill_color(ctx, GColorBlack);
    graphics_fill_rect(ctx, GRect(px + 4, py + TILE - 6, TILE - 8, 3), 0, GCornerNone);
    graphics_context_set_fill_color(ctx, GColorIcterine);
    graphics_fill_rect(ctx, GRect(px + 4, py + TILE - 6, w, 3), 0, GCornerNone);
  }
}

static void tile_label(int x, int y, char *buf, int len) {
  Terrain t = terrain_at(x, y);
  int si = site_at(x, y);
  if (si < 0) {
    snprintf(buf, len, "%s +%d%%", TERRAIN_NAMES[t], TERRAIN_DEF[t] * 20);
    return;
  }
  const char *who = "open";
  if (s_sites[si].owner == 0)      who = "yours";
  else if (s_sites[si].owner == 1) who = "enemy";
  snprintf(buf, len, "%s %s +%d%%", TERRAIN_NAMES[t], who, TERRAIN_DEF[t] * 20);
}

// A vertical list used by all three menus.
static void draw_menu_list(GContext *ctx, GRect area, int row_h,
                           const char *items, int stride, int count, int sel,
                           const char *font_key) {
  for (int i = 0; i < count; i++) {
    GRect row = GRect(area.origin.x, area.origin.y + i * row_h, area.size.w, row_h - 2);
    if (i == sel) {
      graphics_context_set_fill_color(ctx, GColorYellow);
      graphics_fill_rect(ctx, row, 3, GCornersAll);
      graphics_context_set_text_color(ctx, GColorBlack);
    } else {
      graphics_context_set_text_color(ctx, GColorWhite);
    }
    graphics_draw_text(ctx, items + i * stride, fonts_get_system_font(font_key),
                       GRect(row.origin.x + 4, row.origin.y, row.size.w - 8, row_h - 2),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
  }
}

static void draw_board(GContext *ctx, GRect bounds, int ox, int oy) {
  for (int y = 0; y < GRID_H; y++) {
    for (int x = 0; x < GRID_W; x++) {
      int px = ox + x * TILE;
      int py = oy + y * TILE;
      Terrain ter = terrain_at(x, y);
      graphics_context_set_fill_color(ctx, terrain_color(ter));
      graphics_fill_rect(ctx, GRect(px, py, TILE, TILE), 0, GCornerNone);
      draw_terrain_mark(ctx, ter, px, py);

      int si = site_at(x, y);
      if (si >= 0) draw_site_flag(ctx, &s_sites[si], px, py);

      if (s_show_threat && s_threat[y][x]) {
        graphics_context_set_fill_color(ctx, GColorRed);
        graphics_fill_rect(ctx, GRect(px + TILE - 6, py + 2, 4, 4), 0, GCornerNone);
      }

      graphics_context_set_stroke_color(ctx, GColorBlack);
      graphics_draw_rect(ctx, GRect(px, py, TILE, TILE));
    }
  }
  (void)bounds;
}

static void canvas_update(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);

  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);

  // ---- Front-end screens ----
  if (s_phase == PHASE_MAIN) {
    graphics_context_set_text_color(ctx, GColorYellow);
    graphics_draw_text(ctx, "WRISTWARS", fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD),
                       GRect(0, 14, bounds.size.w, 30),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
    draw_menu_list(ctx, GRect(24, 64, bounds.size.w - 48, 0), 32,
                   (const char *)s_mm_label, (int)sizeof(s_mm_label[0]),
                   s_mm_count, s_mm_sel, FONT_KEY_GOTHIC_18);
    return;
  }

  if (s_phase == PHASE_LEVELS) {
    graphics_context_set_text_color(ctx, GColorYellow);
    graphics_draw_text(ctx, "Campaign", fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
                       GRect(0, 6, bounds.size.w, 22),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
    draw_menu_list(ctx, GRect(8, 32, bounds.size.w - 16, 0), 27,
                   (const char *)s_lv_label, (int)sizeof(s_lv_label[0]),
                   s_camp_count, s_lv_sel, FONT_KEY_GOTHIC_18);

    graphics_context_set_text_color(ctx, GColorLightGray);
    graphics_draw_text(ctx, LEVELS[s_camp[s_lv_sel]].hint,
                       fonts_get_system_font(FONT_KEY_GOTHIC_14),
                       GRect(6, bounds.size.h - 40, bounds.size.w - 12, 36),
                       GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
    return;
  }

  // ---- The board ----
  int ox = (bounds.size.w - (GRID_W * TILE)) / 2;
  int oy = 2;
  draw_board(ctx, bounds, ox, oy);

  if (s_phase == PHASE_GROUP && s_group_count > 0) {
    int g = s_groups[s_group];
    for (int i = 0; i < s_reach_count; i++) {
      if (s_reach_grp[i] != g) continue;
      graphics_context_set_fill_color(ctx,
          s_threat[s_reach[i].y][s_reach[i].x] ? GColorRed : GColorBlack);
      graphics_fill_circle(ctx, GPoint(ox + s_reach[i].x * TILE + TILE / 2,
                                       oy + s_reach[i].y * TILE + TILE / 2), 3);
    }
  } else if (s_phase == PHASE_MOVE) {
    for (int i = 0; i < s_dest_count; i++) {
      graphics_context_set_fill_color(ctx,
          s_threat[s_dests[i].y][s_dests[i].x] ? GColorRed : GColorBlack);
      graphics_fill_circle(ctx, GPoint(ox + s_dests[i].x * TILE + TILE / 2,
                                       oy + s_dests[i].y * TILE + TILE / 2), 2);
    }
  }

  for (int i = 0; i < s_unit_count; i++) {
    if (s_units[i].alive) draw_unit(ctx, i, ox, oy);
  }

  for (int i = 0; i < s_site_count; i++) {
    Site *s = &s_sites[i];
    if (s->progress == 0) continue;
    int px = ox + s->x * TILE;
    int py = oy + s->y * TILE;
    int w = (TILE - 4) * s->progress / CAPTURE_GOAL;
    graphics_context_set_fill_color(ctx, GColorBlack);
    graphics_fill_rect(ctx, GRect(px + 2, py + 1, TILE - 4, 3), 0, GCornerNone);
    graphics_context_set_fill_color(ctx, owner_color(s->cap_team));
    graphics_fill_rect(ctx, GRect(px + 2, py + 1, w, 3), 0, GCornerNone);
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
  } else if (s_phase == PHASE_BROWSE || s_phase == PHASE_ACTION) {
    int who = (s_phase == PHASE_ACTION && s_selected >= 0) ? s_selected : s_browse;
    cx = s_units[who].x; cy = s_units[who].y;
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

  static char s_line1[44];
  static char s_line2[44];
  static char s_tile[28];
  int top = oy + GRID_H * TILE;

  if (s_phase == PHASE_OVER) {
    snprintf(s_line1, sizeof(s_line1), "%s", s_winner == 0 ? "You win!" : "Defeated");
    snprintf(s_line2, sizeof(s_line2), "T%d  lost %d  killed %d",
             s_turn, s_lost_player, s_lost_enemy);

  } else if (s_phase == PHASE_ENEMY) {
    snprintf(s_line1, sizeof(s_line1), "Enemy turn");
    snprintf(s_line2, sizeof(s_line2), "T%d   You %d  Enemy %d",
             s_turn, count_alive(0), count_alive(1));

  } else if (s_phase == PHASE_ACTION) {
    snprintf(s_line1, sizeof(s_line1), "%s", ACTION_NAMES[s_actions[s_action]]);
    if (s_actions[s_action] == ACT_CAPTURE) {
      int si = site_at(s_units[s_selected].x, s_units[s_selected].y);
      int have = (si >= 0 && s_sites[si].cap_team == 0) ? s_sites[si].progress : 0;
      snprintf(s_line2, sizeof(s_line2), "%d/%d after this",
               have + s_units[s_selected].hp, CAPTURE_GOAL);
    } else {
      snprintf(s_line2, sizeof(s_line2), "%d of %d", s_action + 1, s_action_count);
    }

  } else if (s_phase == PHASE_TARGET) {
    Unit *a = &s_units[s_selected];
    Unit *d = &s_units[s_targets[s_target]];
    bool weak = (s_moved && is_indirect((UnitType)a->type));
    int out  = damage_from(a, d, s_moved);
    int back = can_counter(d, a) ? compute_damage(d, a) : 0;
    tile_label(d->x, d->y, s_tile, sizeof(s_tile));
    snprintf(s_line1, sizeof(s_line1), "%s  -%d / -%d", UNIT_NAMES[d->type], out, back);
    snprintf(s_line2, sizeof(s_line2), "%s%s", s_tile, weak ? "  moved -40%" : "");

  } else if (s_phase == PHASE_GROUP) {
    int g = s_group_count > 0 ? s_groups[s_group] : GRP_STAY;
    bool gun = (s_selected >= 0 && is_indirect((UnitType)s_units[s_selected].type));

    if (gun && g == GRP_ATTACK) {
      snprintf(s_line1, sizeof(s_line1), "Fire from here");
      snprintf(s_line2, sizeof(s_line2), "full power   %d of %d",
               s_group + 1, s_group_count);
    } else {
      snprintf(s_line1, sizeof(s_line1), "%s", GROUP_NAMES[g]);
      if (gun) {
        snprintf(s_line2, sizeof(s_line2), "fire after moving: -40%%   %d of %d",
                 s_group + 1, s_group_count);
      } else {
        snprintf(s_line2, sizeof(s_line2), "%d tiles   %d of %d",
                 group_tile_count(g), s_group + 1, s_group_count);
      }
    }

  } else if (s_phase == PHASE_MOVE) {
    tile_label(cx, cy, s_tile, sizeof(s_tile));
    snprintf(s_line1, sizeof(s_line1), "%s%s", s_tile, s_threat[cy][cx] ? " RISK" : "");
    snprintf(s_line2, sizeof(s_line2), "%d steps   %d of %d",
             s_cost[cy][cx], s_dest + 1, s_dest_count);

  } else {
    Unit *u = &s_units[s_browse];
    if (is_indirect((UnitType)u->type)) {
      snprintf(s_line1, sizeof(s_line1), "%s %d R%d-%d%s",
               UNIT_NAMES[u->type], u->hp,
               min_range((UnitType)u->type), max_range((UnitType)u->type),
               u->acted ? " used" : "");
    } else {
      snprintf(s_line1, sizeof(s_line1), "%s %d%s",
               UNIT_NAMES[u->type], u->hp, u->acted ? " used" : "");
    }
    if (s_confirm_end) {
      snprintf(s_line2, sizeof(s_line2), "Hold Select again to end");
    } else if (s_turn == 1) {
      snprintf(s_line2, sizeof(s_line2), "%s", cur()->hint);
    } else if (all_player_used()) {
      snprintf(s_line2, sizeof(s_line2), "T%d  all used: hold Select", s_turn);
    } else {
      tile_label(u->x, u->y, s_tile, sizeof(s_tile));
      snprintf(s_line2, sizeof(s_line2), "T%d  %s", s_turn, s_tile);
    }
  }

  graphics_context_set_text_color(ctx, GColorWhite);
  graphics_draw_text(ctx, s_line1, fonts_get_system_font(FONT_KEY_GOTHIC_18),
                     GRect(0, top, bounds.size.w, 22),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);

  graphics_context_set_text_color(ctx, s_confirm_end ? GColorIcterine : GColorLightGray);
  graphics_draw_text(ctx, s_line2, fonts_get_system_font(FONT_KEY_GOTHIC_14),
                     GRect(0, top + 21, bounds.size.w, 18),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);

  if (s_phase == PHASE_PAUSE) {
    GRect panel = GRect(22, 52, bounds.size.w - 44, 94);
    graphics_context_set_fill_color(ctx, GColorBlack);
    graphics_fill_rect(ctx, panel, 5, GCornersAll);
    graphics_context_set_stroke_color(ctx, GColorWhite);
    graphics_draw_round_rect(ctx, panel, 5);

    for (int i = 0; i < PAUSE_COUNT; i++) {
      GRect row = GRect(panel.origin.x + 5, panel.origin.y + 7 + i * 27,
                        panel.size.w - 10, 24);
      if (i == s_pause_sel) {
        graphics_context_set_fill_color(ctx, GColorYellow);
        graphics_fill_rect(ctx, row, 3, GCornersAll);
        graphics_context_set_text_color(ctx, GColorBlack);
      } else {
        graphics_context_set_text_color(ctx, GColorWhite);
      }
      graphics_draw_text(ctx, PAUSE_ITEMS[i], fonts_get_system_font(FONT_KEY_GOTHIC_18),
                         row, GTextOverflowModeTrailingEllipsis,
                         GTextAlignmentCenter, NULL);
    }
  }
}

// ---- Buttons --------------------------------------------------------------

static void finish_action(void) {
  int just_acted = s_selected;
  s_units[just_acted].acted = true;
  s_selected = -1;
  s_phase = PHASE_BROWSE;
  focus_next_unacted(just_acted);
  check_game_over();
  recompute_threat();
  save_game();
}

static void go_main_menu(void) {
  if (s_ai_timer) { app_timer_cancel(s_ai_timer); s_ai_timer = NULL; }
  s_have_save = (s_winner < 0);
  build_main_menu();
  s_phase = PHASE_MAIN;
}

static void up_handler(ClickRecognizerRef r, void *context) {
  s_confirm_end = false;
  switch (s_phase) {
    case PHASE_MAIN:   s_mm_sel = (s_mm_sel + s_mm_count - 1) % s_mm_count; break;
    case PHASE_LEVELS: s_lv_sel = (s_lv_sel + s_camp_count - 1) % s_camp_count; break;
    case PHASE_PAUSE:  s_pause_sel = (s_pause_sel + PAUSE_COUNT - 1) % PAUSE_COUNT; break;
    case PHASE_GROUP:
      if (s_group_count > 0) s_group = (s_group + s_group_count - 1) % s_group_count;
      break;
    case PHASE_MOVE:
      if (s_dest_count > 0) s_dest = (s_dest + s_dest_count - 1) % s_dest_count;
      break;
    case PHASE_ACTION: s_action = (s_action + s_action_count - 1) % s_action_count; break;
    case PHASE_TARGET:
      if (s_target_count > 0) s_target = (s_target + s_target_count - 1) % s_target_count;
      break;
    case PHASE_BROWSE: cycle_own_unit(-1); break;
    default: break;
  }
  layer_mark_dirty(s_canvas);
}

static void down_handler(ClickRecognizerRef r, void *context) {
  s_confirm_end = false;
  switch (s_phase) {
    case PHASE_MAIN:   s_mm_sel = (s_mm_sel + 1) % s_mm_count; break;
    case PHASE_LEVELS: s_lv_sel = (s_lv_sel + 1) % s_camp_count; break;
    case PHASE_PAUSE:  s_pause_sel = (s_pause_sel + 1) % PAUSE_COUNT; break;
    case PHASE_GROUP:
      if (s_group_count > 0) s_group = (s_group + 1) % s_group_count;
      break;
    case PHASE_MOVE:
      if (s_dest_count > 0) s_dest = (s_dest + 1) % s_dest_count;
      break;
    case PHASE_ACTION: s_action = (s_action + 1) % s_action_count; break;
    case PHASE_TARGET:
      if (s_target_count > 0) s_target = (s_target + 1) % s_target_count;
      break;
    case PHASE_BROWSE: cycle_own_unit(1); break;
    default: break;
  }
  layer_mark_dirty(s_canvas);
}

static void up_long_handler(ClickRecognizerRef r, void *context) {
  if (s_phase != PHASE_BROWSE && s_phase != PHASE_GROUP &&
      s_phase != PHASE_MOVE   && s_phase != PHASE_TARGET) return;
  s_show_threat = !s_show_threat;
  if (s_show_threat) recompute_threat();
  layer_mark_dirty(s_canvas);
}

static void down_long_handler(ClickRecognizerRef r, void *context) {
  if (s_phase != PHASE_BROWSE || !s_undo_ok) return;

  memcpy(s_units, s_undo_units, sizeof(s_units));
  memcpy(s_sites, s_undo_sites, sizeof(s_sites));
  s_browse      = s_undo_browse;
  s_lost_player = s_undo_lost_player;
  s_lost_enemy  = s_undo_lost_enemy;
  s_undo_ok     = false;
  s_selected    = -1;
  s_confirm_end = false;
  recompute_threat();
  save_game();
  vibes_short_pulse();
  layer_mark_dirty(s_canvas);
}

static void select_handler(ClickRecognizerRef r, void *context) {
  if (s_phase == PHASE_MAIN) {
    switch (s_mm_act[s_mm_sel]) {
      case MM_CONTINUE:
        s_phase = (s_winner >= 0) ? PHASE_OVER : PHASE_BROWSE;
        break;
      case MM_CAMPAIGN:
        build_level_menu();
        s_phase = PHASE_LEVELS;
        break;
      case MM_SKIRMISH:
        start_skirmish();
        save_game();
        break;
      case MM_EXIT:
        window_stack_pop(true);
        return;
    }
    layer_mark_dirty(s_canvas);
    return;
  }

  if (s_phase == PHASE_LEVELS) {
    if (!camp_unlocked(s_lv_sel)) {
      vibes_short_pulse();                 // still locked
    } else {
      start_level(s_camp[s_lv_sel], true);
      save_game();
    }
    layer_mark_dirty(s_canvas);
    return;
  }

  if (s_phase == PHASE_PAUSE) {
    if (s_pause_sel == 0) {
      s_phase = (s_winner >= 0) ? PHASE_OVER : PHASE_BROWSE;
    } else if (s_pause_sel == 1) {
      start_level(s_level_idx, s_campaign);
      save_game();
    } else {
      save_game();
      go_main_menu();
    }
    layer_mark_dirty(s_canvas);
    return;
  }

  if (s_phase == PHASE_OVER) {
    if (s_campaign) {
      build_level_menu();
      s_phase = PHASE_LEVELS;
    } else {
      start_skirmish();
      save_game();
    }
    layer_mark_dirty(s_canvas);
    return;
  }

  s_confirm_end = false;

  if (s_phase == PHASE_BROWSE) {
    if (s_units[s_browse].acted) return;
    snapshot_for_undo();
    s_selected = s_browse;
    compute_reach(s_selected);
    classify_reach(s_selected);
    s_phase = PHASE_GROUP;

  } else if (s_phase == PHASE_GROUP) {
    if (s_group_count == 0) {
      finish_action();
    } else if (is_indirect((UnitType)s_units[s_selected].type) &&
               s_groups[s_group] == GRP_ATTACK) {
      // "Fire from here" - skip the distance list entirely.
      s_moved = false;
      build_actions(s_selected, false);
      if (s_action_count == 1)              finish_action();
      else if (s_actions[0] == ACT_ATTACK) { s_target = 0; s_phase = PHASE_TARGET; }
      else                                  s_phase = PHASE_ACTION;
    } else {
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
    build_actions(s_selected, s_moved);

    if (s_action_count == 1) {
      finish_action();
    } else if (s_action_count == 2 && s_actions[0] == ACT_ATTACK) {
      s_target = 0;
      s_phase = PHASE_TARGET;
    } else {
      s_phase = PHASE_ACTION;
    }

  } else if (s_phase == PHASE_ACTION) {
    int act = s_actions[s_action];
    if (act == ACT_ATTACK) {
      s_target = 0;
      s_phase = PHASE_TARGET;
    } else if (act == ACT_CAPTURE) {
      do_capture(s_selected);
      finish_action();
    } else {
      finish_action();
    }

  } else if (s_phase == PHASE_TARGET) {
    resolve_attack(s_selected, s_targets[s_target], s_moved);
    finish_action();
  }
  layer_mark_dirty(s_canvas);
}

static void select_long_handler(ClickRecognizerRef r, void *context) {
  if (s_phase == PHASE_BROWSE) {
    if (!all_player_used() && !s_confirm_end) s_confirm_end = true;
    else                                      begin_enemy_turn();
    layer_mark_dirty(s_canvas);
  }
}

static void back_handler(ClickRecognizerRef r, void *context) {
  s_confirm_end = false;

  switch (s_phase) {
    case PHASE_MAIN:
      window_stack_pop(true);
      return;
    case PHASE_LEVELS:
      go_main_menu();
      break;
    case PHASE_PAUSE:
      s_phase = (s_winner >= 0) ? PHASE_OVER : PHASE_BROWSE;
      break;
    case PHASE_GROUP:
      s_selected = -1;
      s_phase = PHASE_BROWSE;
      break;
    case PHASE_MOVE:
      s_phase = PHASE_GROUP;
      break;
    case PHASE_TARGET:
      // Backing out of a shot returns to the action list instead of burning
      // the unit's turn.
      if (s_action_count > 1) s_phase = PHASE_ACTION;
      else                    finish_action();
      break;
    case PHASE_ACTION:
      finish_action();
      break;
    case PHASE_ENEMY:
      return;
    default:                       // browse or game over
      s_pause_sel = 0;
      s_phase = PHASE_PAUSE;
      break;
  }
  layer_mark_dirty(s_canvas);
}

static void click_config(void *context) {
  window_single_click_subscribe(BUTTON_ID_UP, up_handler);
  window_long_click_subscribe(BUTTON_ID_UP, 500, up_long_handler, NULL);
  window_single_click_subscribe(BUTTON_ID_DOWN, down_handler);
  window_long_click_subscribe(BUTTON_ID_DOWN, 500, down_long_handler, NULL);
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
  load_progress();
  build_campaign_list();
  build_level_menu();

  if (load_game()) {
    s_selected = -1;
    s_moved = false;
    s_confirm_end = false;
    s_undo_ok = false;
    s_have_save = (s_winner < 0);
    if (s_winner < 0) focus_next_unacted(s_unit_count - 1);
    else              snap_browse_to_own();
    recompute_threat();
  } else {
    s_have_save = false;
    start_level(5, false);
  }

  build_main_menu();
  s_phase = PHASE_MAIN;

  s_window = window_create();
  window_set_click_config_provider(s_window, click_config);
  window_set_window_handlers(s_window, (WindowHandlers) {
    .load   = window_load,
    .unload = window_unload,
  });
  window_stack_push(s_window, true);
}

static void deinit(void) {
  if (s_phase != PHASE_MAIN && s_phase != PHASE_LEVELS) save_game();
  window_destroy(s_window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
