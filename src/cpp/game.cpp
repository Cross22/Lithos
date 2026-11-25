// Freestanding C++17 WebAssembly game engine for a 10x20 tile-based block puzzle.
// Compiled without libc via clang++ --target=wasm32 -nostdlib.

using u8 = unsigned char;
using i32 = int;
using u32 = unsigned int;
using usize = unsigned long;

extern "C" void* memset(void* dst, int val, usize len) {
  u8* p = static_cast<u8*>(dst);
  u8 v = static_cast<u8>(val);
  for (usize i = 0; i < len; ++i) {
    p[i] = v;
  }
  return dst;
}

extern "C" void* memcpy(void* dst, const void* src, usize len) {
  u8* d = static_cast<u8*>(dst);
  const u8* s = static_cast<const u8*>(src);
  for (usize i = 0; i < len; ++i) {
    d[i] = s[i];
  }
  return dst;
}

namespace {

constexpr i32 BOARD_W = 10;
constexpr i32 BOARD_H = 20;
constexpr i32 BOARD_SIZE = BOARD_W * BOARD_H;

constexpr i32 STATE_TITLE = 0;
constexpr i32 STATE_PLAYING = 1;
constexpr i32 STATE_PAUSED = 2;
constexpr i32 STATE_GAME_OVER = 3;

// Key codes routed directly from browser KeyboardEvent.keyCode / mapped codes
constexpr i32 KEY_ENTER = 13;
constexpr i32 KEY_ESC = 27;
constexpr i32 KEY_SPACE = 32;
constexpr i32 KEY_LEFT = 37;
constexpr i32 KEY_UP = 38;
constexpr i32 KEY_RIGHT = 39;
constexpr i32 KEY_DOWN = 40;
constexpr i32 KEY_A = 65;
constexpr i32 KEY_D = 68;
constexpr i32 KEY_P = 80;
constexpr i32 KEY_R = 82;
constexpr i32 KEY_S = 83;
constexpr i32 KEY_W = 87;

// Event bitmask for Web Audio feedback
constexpr u32 EVT_MOVE = 1u << 0;
constexpr u32 EVT_ROTATE = 1u << 1;
constexpr u32 EVT_LOCK = 1u << 2;
constexpr u32 EVT_CLEAR = 1u << 3;
constexpr u32 EVT_QUAD = 1u << 4;
constexpr u32 EVT_GAME_OVER = 1u << 5;

// 7 tetrominoes (0..6 -> tile values 1..7), 4 rotations, 4x4 bitmask packed in 16 bits
// Bit index (row * 4 + col), MSB at (0,0) = bit 15
constexpr unsigned short SHAPES[7][4] = {
    // 0: I
    {0x0F00, 0x2222, 0x00F0, 0x4444},
    // 1: O
    {0x6600, 0x6600, 0x6600, 0x6600},
    // 2: T
    {0x4E00, 0x4640, 0x0E40, 0x4C40},
    // 3: S
    {0x6C00, 0x4620, 0x06C0, 0x8C40},
    // 4: Z
    {0xC600, 0x2640, 0x0C60, 0x4C80},
    // 5: J
    {0x8E00, 0x6440, 0x0E20, 0x44C0},
    // 6: L
    {0x2E00, 0x4460, 0x0E80, 0xC440},
};

// Wall-kick candidate offsets (dx, dy)
constexpr i32 KICKS[8][2] = {
    {0, 0},
    {-1, 0},
    {1, 0},
    {0, -1},
    {-2, 0},
    {2, 0},
    {-1, -1},
    {1, -1},
};

struct GameEngine {
  u8 board[BOARD_SIZE];
  u8 render_grid[BOARD_SIZE];
  u8 next_grid[16];

  i32 state = STATE_TITLE;
  i32 score = 0;
  i32 high_score = 0;
  i32 lines = 0;
  i32 level = 1;
  i32 elapsed_ms = 0;

  i32 cur_type = 0;
  i32 cur_rot = 0;
  i32 cur_x = 3;
  i32 cur_y = 0;
  i32 next_type = 1;

  u8 bag[7];
  i32 bag_index = 7;
  u32 rng_state = 0x9E3779B9u;

  i32 gravity_timer_ms = 0;
  i32 clear_timer_ms = 0;
  u32 clearing_rows_mask = 0;

  // Input state for smooth DAS / ARR
  bool hold_left = false;
  bool hold_right = false;
  bool hold_down = false;
  i32 das_dir = 0;
  i32 das_timer_ms = 0;
  i32 arr_timer_ms = 0;
  i32 soft_drop_timer_ms = 0;

  u32 pending_events = 0;
};

GameEngine g;

u32 next_rand() {
  u32 x = g.rng_state;
  if (x == 0) x = 0x9E3779B9u;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  g.rng_state = x;
  return x;
}

void refill_bag() {
  for (i32 i = 0; i < 7; ++i) {
    g.bag[i] = static_cast<u8>(i);
  }
  for (i32 i = 6; i > 0; --i) {
    i32 j = static_cast<i32>(next_rand() % static_cast<u32>(i + 1));
    u8 tmp = g.bag[i];
    g.bag[i] = g.bag[j];
    g.bag[j] = tmp;
  }
  g.bag_index = 0;
}

i32 draw_piece_from_bag() {
  if (g.bag_index >= 7) {
    refill_bag();
  }
  return static_cast<i32>(g.bag[g.bag_index++]);
}

bool shape_cell(i32 type, i32 rot, i32 r, i32 c) {
  unsigned short mask = SHAPES[type][rot & 3];
  i32 bit = 15 - (r * 4 + c);
  return ((mask >> bit) & 1) != 0;
}

bool fits(i32 type, i32 rot, i32 px, i32 py) {
  for (i32 r = 0; r < 4; ++r) {
    for (i32 c = 0; c < 4; ++c) {
      if (!shape_cell(type, rot, r, c)) continue;
      i32 bx = px + c;
      i32 by = py + r;
      if (bx < 0 || bx >= BOARD_W || by >= BOARD_H) {
        return false;
      }
      if (by >= 0 && g.board[by * BOARD_W + bx] != 0) {
        return false;
      }
    }
  }
  return true;
}

void update_next_grid() {
  for (i32 i = 0; i < 16; ++i) {
    g.next_grid[i] = 0;
  }
  for (i32 r = 0; r < 4; ++r) {
    for (i32 c = 0; c < 4; ++c) {
      if (shape_cell(g.next_type, 0, r, c)) {
        g.next_grid[r * 4 + c] = static_cast<u8>(g.next_type + 1);
      }
    }
  }
}

void update_render_grid() {
  for (i32 r = 0; r < BOARD_H; ++r) {
    bool is_clearing = (g.clear_timer_ms > 0) && ((g.clearing_rows_mask & (1u << r)) != 0);
    for (i32 c = 0; c < BOARD_W; ++c) {
      u8 cell = g.board[r * BOARD_W + c];
      if (is_clearing && cell != 0) {
        g.render_grid[r * BOARD_W + c] = static_cast<u8>(32 + cell);
      } else {
        g.render_grid[r * BOARD_W + c] = cell;
      }
    }
  }

  if (g.state == STATE_PLAYING && g.clear_timer_ms == 0) {
    // Compute ghost piece projection
    i32 ghost_y = g.cur_y;
    while (fits(g.cur_type, g.cur_rot, g.cur_x, ghost_y + 1)) {
      ++ghost_y;
    }

    for (i32 r = 0; r < 4; ++r) {
      for (i32 c = 0; c < 4; ++c) {
        if (!shape_cell(g.cur_type, g.cur_rot, r, c)) continue;
        i32 bx = g.cur_x + c;
        i32 gy = ghost_y + r;
        if (bx >= 0 && bx < BOARD_W && gy >= 0 && gy < BOARD_H) {
          if (g.render_grid[gy * BOARD_W + bx] == 0) {
            g.render_grid[gy * BOARD_W + bx] = static_cast<u8>(17 + g.cur_type);
          }
        }
      }
    }

    // Active piece overwrites ghost
    for (i32 r = 0; r < 4; ++r) {
      for (i32 c = 0; c < 4; ++c) {
        if (!shape_cell(g.cur_type, g.cur_rot, r, c)) continue;
        i32 bx = g.cur_x + c;
        i32 by = g.cur_y + r;
        if (bx >= 0 && bx < BOARD_W && by >= 0 && by < BOARD_H) {
          g.render_grid[by * BOARD_W + bx] = static_cast<u8>(g.cur_type + 1);
        }
      }
    }
  }
}

void spawn_next_piece() {
  g.cur_type = g.next_type;
  g.next_type = draw_piece_from_bag();
  g.cur_rot = 0;
  g.cur_x = 3;
  g.cur_y = 0;
  g.gravity_timer_ms = 0;
  update_next_grid();

  if (!fits(g.cur_type, g.cur_rot, g.cur_x, g.cur_y)) {
    g.state = STATE_GAME_OVER;
    g.pending_events |= EVT_GAME_OVER;
  }
}

void collapse_cleared_rows() {
  i32 cleared = 0;
  i32 write_row = BOARD_H - 1;
  for (i32 r = BOARD_H - 1; r >= 0; --r) {
    if ((g.clearing_rows_mask & (1u << r)) != 0) {
      ++cleared;
    } else {
      if (write_row != r) {
        for (i32 c = 0; c < BOARD_W; ++c) {
          g.board[write_row * BOARD_W + c] = g.board[r * BOARD_W + c];
        }
      }
      --write_row;
    }
  }
  while (write_row >= 0) {
    for (i32 c = 0; c < BOARD_W; ++c) {
      g.board[write_row * BOARD_W + c] = 0;
    }
    --write_row;
  }

  if (cleared > 0) {
    static constexpr i32 LINE_SCORES[5] = {0, 100, 300, 500, 800};
    i32 pts = LINE_SCORES[cleared <= 4 ? cleared : 4] * g.level;
    g.score += pts;
    if (g.score > g.high_score) {
      g.high_score = g.score;
    }
    g.lines += cleared;
    g.level = 1 + (g.lines / 10);
    g.pending_events |= (cleared >= 4) ? EVT_QUAD : EVT_CLEAR;
  }

  g.clearing_rows_mask = 0;
  g.clear_timer_ms = 0;
  spawn_next_piece();
}

void lock_current_piece() {
  bool overflow = false;
  for (i32 r = 0; r < 4; ++r) {
    for (i32 c = 0; c < 4; ++c) {
      if (!shape_cell(g.cur_type, g.cur_rot, r, c)) continue;
      i32 bx = g.cur_x + c;
      i32 by = g.cur_y + r;
      if (by < 0) {
        overflow = true;
      } else if (bx >= 0 && bx < BOARD_W && by < BOARD_H) {
        g.board[by * BOARD_W + bx] = static_cast<u8>(g.cur_type + 1);
      }
    }
  }

  if (overflow) {
    g.state = STATE_GAME_OVER;
    g.pending_events |= EVT_GAME_OVER;
    update_render_grid();
    return;
  }

  // Check for completed rows
  u32 mask = 0;
  i32 count = 0;
  for (i32 r = 0; r < BOARD_H; ++r) {
    bool full = true;
    for (i32 c = 0; c < BOARD_W; ++c) {
      if (g.board[r * BOARD_W + c] == 0) {
        full = false;
        break;
      }
    }
    if (full) {
      mask |= (1u << r);
      ++count;
    }
  }

  g.pending_events |= EVT_LOCK;
  if (count > 0) {
    g.clearing_rows_mask = mask;
    g.clear_timer_ms = 130;
  } else {
    spawn_next_piece();
  }
}

bool try_move(i32 dx, i32 dy) {
  if (fits(g.cur_type, g.cur_rot, g.cur_x + dx, g.cur_y + dy)) {
    g.cur_x += dx;
    g.cur_y += dy;
    if (dx != 0) {
      g.pending_events |= EVT_MOVE;
    }
    return true;
  }
  return false;
}

bool try_rotate_cw() {
  i32 next_rot = (g.cur_rot + 1) & 3;
  for (i32 i = 0; i < 8; ++i) {
    i32 kx = g.cur_x + KICKS[i][0];
    i32 ky = g.cur_y + KICKS[i][1];
    if (fits(g.cur_type, next_rot, kx, ky)) {
      g.cur_rot = next_rot;
      g.cur_x = kx;
      g.cur_y = ky;
      g.pending_events |= EVT_ROTATE;
      return true;
    }
  }
  return false;
}

void hard_drop() {
  i32 dropped = 0;
  while (fits(g.cur_type, g.cur_rot, g.cur_x, g.cur_y + 1)) {
    ++g.cur_y;
    ++dropped;
  }
  g.score += dropped * 2;
  if (g.score > g.high_score) {
    g.high_score = g.score;
  }
  lock_current_piece();
}

i32 gravity_interval_ms() {
  i32 ms = 820 - (g.level - 1) * 70;
  return ms < 75 ? 75 : ms;
}

void reset_board_and_start(u32 seed) {
  if (seed != 0) {
    g.rng_state = seed;
  }
  for (i32 i = 0; i < BOARD_SIZE; ++i) {
    g.board[i] = 0;
    g.render_grid[i] = 0;
  }
  g.score = 0;
  g.lines = 0;
  g.level = 1;
  g.elapsed_ms = 0;
  g.gravity_timer_ms = 0;
  g.clear_timer_ms = 0;
  g.clearing_rows_mask = 0;
  g.hold_left = false;
  g.hold_right = false;
  g.hold_down = false;
  g.das_dir = 0;
  g.das_timer_ms = 0;
  g.arr_timer_ms = 0;
  g.soft_drop_timer_ms = 0;
  g.pending_events = 0;
  g.bag_index = 7;

  g.next_type = draw_piece_from_bag();
  g.state = STATE_PLAYING;
  spawn_next_piece();
  update_render_grid();
}

}  // namespace

extern "C" {

void init_game(u32 seed) {
  if (seed != 0) {
    g.rng_state = seed;
  }
  for (i32 i = 0; i < BOARD_SIZE; ++i) {
    g.board[i] = 0;
    g.render_grid[i] = 0;
  }
  g.state = STATE_TITLE;
  g.score = 0;
  g.lines = 0;
  g.level = 1;
  g.elapsed_ms = 0;
  g.bag_index = 7;
  g.next_type = draw_piece_from_bag();
  update_next_grid();
  update_render_grid();
}

void start_game(u32 seed) {
  reset_board_and_start(seed);
}

void toggle_pause() {
  if (g.state == STATE_PLAYING) {
    g.state = STATE_PAUSED;
    g.hold_left = false;
    g.hold_right = false;
    g.hold_down = false;
    g.das_dir = 0;
  } else if (g.state == STATE_PAUSED) {
    g.state = STATE_PLAYING;
  }
  update_render_grid();
}

// Routes raw browser keyboard input (Arrow keys, WASD, Space, P/Esc, R, Enter) directly to C++.
void on_key_event(i32 key_code, i32 is_down) {
  bool down = is_down != 0;

  // Global state shortcuts on key down
  if (down) {
    if (key_code == KEY_P || key_code == KEY_ESC) {
      if (g.state == STATE_PLAYING || g.state == STATE_PAUSED) {
        toggle_pause();
      }
      return;
    }
    if (key_code == KEY_R) {
      reset_board_and_start(g.rng_state ^ static_cast<u32>(g.elapsed_ms + 1));
      return;
    }
    if (key_code == KEY_ENTER && (g.state == STATE_TITLE || g.state == STATE_GAME_OVER)) {
      reset_board_and_start(g.rng_state ^ 0xA5A5A5A5u);
      return;
    }
  }

  // Movement & rotation keys (Arrow keys + WASD)
  bool is_left = (key_code == KEY_LEFT || key_code == KEY_A);
  bool is_right = (key_code == KEY_RIGHT || key_code == KEY_D);
  bool is_down_key = (key_code == KEY_DOWN || key_code == KEY_S);
  bool is_rotate = (key_code == KEY_UP || key_code == KEY_W);
  bool is_hard_drop = (key_code == KEY_SPACE);

  if (is_left) {
    if (down && !g.hold_left) {
      g.hold_left = true;
      g.das_dir = -1;
      g.das_timer_ms = 0;
      g.arr_timer_ms = 0;
      if (g.state == STATE_PLAYING && g.clear_timer_ms == 0) {
        try_move(-1, 0);
      }
    } else if (!down) {
      g.hold_left = false;
      if (g.hold_right) {
        g.das_dir = 1;
        g.das_timer_ms = 0;
        g.arr_timer_ms = 0;
      } else if (g.das_dir == -1) {
        g.das_dir = 0;
      }
    }
  } else if (is_right) {
    if (down && !g.hold_right) {
      g.hold_right = true;
      g.das_dir = 1;
      g.das_timer_ms = 0;
      g.arr_timer_ms = 0;
      if (g.state == STATE_PLAYING && g.clear_timer_ms == 0) {
        try_move(1, 0);
      }
    } else if (!down) {
      g.hold_right = false;
      if (g.hold_left) {
        g.das_dir = -1;
        g.das_timer_ms = 0;
        g.arr_timer_ms = 0;
      } else if (g.das_dir == 1) {
        g.das_dir = 0;
      }
    }
  } else if (is_down_key) {
    if (down && !g.hold_down) {
      g.hold_down = true;
      g.soft_drop_timer_ms = 0;
      if (g.state == STATE_PLAYING && g.clear_timer_ms == 0) {
        if (try_move(0, 1)) {
          g.score += 1;
          if (g.score > g.high_score) g.high_score = g.score;
          g.gravity_timer_ms = 0;
        }
      }
    } else if (!down) {
      g.hold_down = false;
    }
  } else if (is_rotate && down) {
    if (g.state == STATE_PLAYING && g.clear_timer_ms == 0) {
      try_rotate_cw();
    }
  } else if (is_hard_drop && down) {
    if (g.state == STATE_PLAYING && g.clear_timer_ms == 0) {
      hard_drop();
    } else if (g.state == STATE_TITLE || g.state == STATE_GAME_OVER) {
      reset_board_and_start(g.rng_state ^ 0x5A5A5A5Au);
    }
  }

  update_render_grid();
}

void tick(i32 dt_ms) {
  if (dt_ms < 0) dt_ms = 0;
  if (dt_ms > 100) dt_ms = 100;

  if (g.state != STATE_PLAYING) {
    update_render_grid();
    return;
  }

  g.elapsed_ms += dt_ms;

  if (g.clear_timer_ms > 0) {
    g.clear_timer_ms -= dt_ms;
    if (g.clear_timer_ms <= 0) {
      collapse_cleared_rows();
    }
    update_render_grid();
    return;
  }

  // Horizontal DAS / ARR
  if (g.das_dir != 0) {
    g.das_timer_ms += dt_ms;
    if (g.das_timer_ms >= 145) {
      g.arr_timer_ms += dt_ms;
      while (g.arr_timer_ms >= 38) {
        g.arr_timer_ms -= 38;
        try_move(g.das_dir, 0);
      }
    }
  }

  // Soft drop repeat
  if (g.hold_down) {
    g.soft_drop_timer_ms += dt_ms;
    while (g.soft_drop_timer_ms >= 42) {
      g.soft_drop_timer_ms -= 42;
      if (try_move(0, 1)) {
        g.score += 1;
        if (g.score > g.high_score) g.high_score = g.score;
        g.gravity_timer_ms = 0;
      } else {
        lock_current_piece();
        update_render_grid();
        return;
      }
    }
  }

  // Natural gravity
  g.gravity_timer_ms += dt_ms;
  i32 interval = gravity_interval_ms();
  if (g.gravity_timer_ms >= interval) {
    g.gravity_timer_ms -= interval;
    if (!try_move(0, 1)) {
      lock_current_piece();
    }
  }

  update_render_grid();
}

const u8* get_render_grid() { return g.render_grid; }
const u8* get_next_grid() { return g.next_grid; }
i32 get_board_width() { return BOARD_W; }
i32 get_board_height() { return BOARD_H; }
i32 get_score() { return g.score; }
i32 get_high_score() { return g.high_score; }
i32 get_lines() { return g.lines; }
i32 get_level() { return g.level; }
i32 get_elapsed_ms() { return g.elapsed_ms; }
i32 get_state() { return g.state; }

u32 consume_events() {
  u32 ev = g.pending_events;
  g.pending_events = 0;
  return ev;
}

}  // extern "C"
