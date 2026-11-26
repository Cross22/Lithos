/**
 * C++17 WebAssembly Game Engine for Lithos 10x20.
 *
 * Uses the C++ Standard Library (<algorithm>, <array>, <cstdint>, <numeric>,
 * <random>) compiled via wasi-sdk to a standalone WebAssembly reactor module.
 */

#include <algorithm>
#include <array>
#include <cstdint>
#include <numeric>
#include <random>

#define WASM_EXPORT(name) __attribute__((export_name(#name)))

using namespace std;

constexpr int32_t BOARD_W = 10;
constexpr int32_t BOARD_H = 20;
constexpr int32_t BOARD_SIZE = BOARD_W * BOARD_H;

constexpr int32_t STATE_TITLE = 0;
constexpr int32_t STATE_PLAYING = 1;
constexpr int32_t STATE_PAUSED = 2;
constexpr int32_t STATE_GAME_OVER = 3;

// Key codes routed directly from browser KeyboardEvent.code mappings
constexpr int32_t KEY_ENTER = 13;
constexpr int32_t KEY_ESC = 27;
constexpr int32_t KEY_SPACE = 32;
constexpr int32_t KEY_LEFT = 37;
constexpr int32_t KEY_UP = 38;
constexpr int32_t KEY_RIGHT = 39;
constexpr int32_t KEY_DOWN = 40;
constexpr int32_t KEY_A = 65;
constexpr int32_t KEY_D = 68;
constexpr int32_t KEY_P = 80;
constexpr int32_t KEY_R = 82;
constexpr int32_t KEY_S = 83;
constexpr int32_t KEY_W = 87;

// Event bitmask consumed by frontend Web Audio synthesizer
constexpr uint32_t EVT_MOVE = 1u << 0;
constexpr uint32_t EVT_ROTATE = 1u << 1;
constexpr uint32_t EVT_LOCK = 1u << 2;
constexpr uint32_t EVT_CLEAR = 1u << 3;
constexpr uint32_t EVT_QUAD = 1u << 4;
constexpr uint32_t EVT_GAME_OVER = 1u << 5;

// 7 tetrominoes (0..6 -> tile values 1..7),
// 4 rotations, 4x4 bitmask packed in 16 bits
constexpr array<array<uint16_t, 4>, 7> SHAPES = {{
    {{0x0F00, 0x2222, 0x00F0, 0x4444}}, // 0: I
    {{0x6600, 0x6600, 0x6600, 0x6600}}, // 1: O
    {{0x4E00, 0x4640, 0x0E40, 0x4C40}}, // 2: T
    {{0x6C00, 0x4620, 0x06C0, 0x8C40}}, // 3: S
    {{0xC600, 0x2640, 0x0C60, 0x4C80}}, // 4: Z
    {{0x8E00, 0x6440, 0x0E20, 0x44C0}}, // 5: J
    {{0x2E00, 0x4460, 0x0E80, 0xC440}}, // 6: L
}};

// Wall-kick candidate offsets (dx, dy)
constexpr array<array<int32_t, 2>, 8> KICKS = {{
    {{0, 0}},
    {{-1, 0}},
    {{1, 0}},
    {{0, -1}},
    {{-2, 0}},
    {{2, 0}},
    {{-1, -1}},
    {{1, -1}},
}};

struct GameEngine
{
  array<uint8_t, BOARD_SIZE> board{};
  array<uint8_t, BOARD_SIZE> render_grid{};
  array<uint8_t, 16> next_grid{};

  int32_t state = STATE_TITLE;
  int32_t score = 0;
  int32_t high_score = 0;
  int32_t lines = 0;
  int32_t level = 1;
  int32_t elapsed_ms = 0;

  int32_t cur_type = 0;
  int32_t cur_rot = 0;
  int32_t cur_x = 3;
  int32_t cur_y = 0;
  int32_t next_type = 1;

  array<uint8_t, 7> bag{};
  size_t bag_index = 7;
  mt19937 rng{0x9E3779B9u};

  int32_t gravity_timer_ms = 0;
  int32_t clear_timer_ms = 0;
  uint32_t clearing_rows_mask = 0;

  // Input state for smooth DAS / ARR
  bool hold_left = false;
  bool hold_right = false;
  bool hold_down = false;
  int32_t das_dir = 0;
  int32_t das_timer_ms = 0;
  int32_t arr_timer_ms = 0;
  int32_t soft_drop_timer_ms = 0;

  uint32_t pending_events = 0;
};

GameEngine g;

void refill_bag()
{
  iota(g.bag.begin(), g.bag.end(), static_cast<uint8_t>(0));
  shuffle(g.bag.begin(), g.bag.end(), g.rng);
  g.bag_index = 0;
}

int32_t draw_piece_from_bag()
{
  if (g.bag_index >= g.bag.size())
  {
    refill_bag();
  }
  return static_cast<int32_t>(g.bag[g.bag_index++]);
}

bool shape_cell(int32_t type, int32_t rot, int32_t r, int32_t c)
{
  const uint16_t mask = SHAPES[static_cast<size_t>(type)][static_cast<size_t>(rot & 3)];
  const int32_t bit = 15 - (r * 4 + c);
  return ((mask >> bit) & 1u) != 0;
}

bool fits(int32_t type, int32_t rot, int32_t px, int32_t py)
{
  for (int32_t r = 0; r < 4; ++r)
  {
    for (int32_t c = 0; c < 4; ++c)
    {
      if (!shape_cell(type, rot, r, c))
        continue;
      const int32_t bx = px + c;
      const int32_t by = py + r;
      if (bx < 0 || bx >= BOARD_W || by >= BOARD_H)
      {
        return false;
      }
      if (by >= 0 && g.board[static_cast<size_t>(by * BOARD_W + bx)] != 0)
      {
        return false;
      }
    }
  }
  return true;
}

void update_next_grid()
{
  fill(g.next_grid.begin(), g.next_grid.end(), static_cast<uint8_t>(0));
  for (int32_t r = 0; r < 4; ++r)
  {
    for (int32_t c = 0; c < 4; ++c)
    {
      if (shape_cell(g.next_type, 0, r, c))
      {
        g.next_grid[static_cast<size_t>(r * 4 + c)] = static_cast<uint8_t>(g.next_type + 1);
      }
    }
  }
}

void update_render_grid()
{
  for (int32_t r = 0; r < BOARD_H; ++r)
  {
    const bool is_clearing =
        (g.clear_timer_ms > 0) && ((g.clearing_rows_mask & (1u << r)) != 0);
    for (int32_t c = 0; c < BOARD_W; ++c)
    {
      const size_t idx = static_cast<size_t>(r * BOARD_W + c);
      const uint8_t cell = g.board[idx];
      if (is_clearing && cell != 0)
      {
        g.render_grid[idx] = static_cast<uint8_t>(32 + cell);
      }
      else
      {
        g.render_grid[idx] = cell;
      }
    }
  }

  if (g.state == STATE_PLAYING && g.clear_timer_ms == 0)
  {
    // Project ghost landing position
    int32_t ghost_y = g.cur_y;
    while (fits(g.cur_type, g.cur_rot, g.cur_x, ghost_y + 1))
    {
      ++ghost_y;
    }

    for (int32_t r = 0; r < 4; ++r)
    {
      for (int32_t c = 0; c < 4; ++c)
      {
        if (!shape_cell(g.cur_type, g.cur_rot, r, c))
          continue;
        const int32_t bx = g.cur_x + c;
        const int32_t gy = ghost_y + r;
        if (bx >= 0 && bx < BOARD_W && gy >= 0 && gy < BOARD_H)
        {
          const size_t idx = static_cast<size_t>(gy * BOARD_W + bx);
          if (g.render_grid[idx] == 0)
          {
            g.render_grid[idx] = static_cast<uint8_t>(17 + g.cur_type);
          }
        }
      }
    }

    // Active falling piece overwrites ghost
    for (int32_t r = 0; r < 4; ++r)
    {
      for (int32_t c = 0; c < 4; ++c)
      {
        if (!shape_cell(g.cur_type, g.cur_rot, r, c))
          continue;
        const int32_t bx = g.cur_x + c;
        const int32_t by = g.cur_y + r;
        if (bx >= 0 && bx < BOARD_W && by >= 0 && by < BOARD_H)
        {
          g.render_grid[static_cast<size_t>(by * BOARD_W + bx)] =
              static_cast<uint8_t>(g.cur_type + 1);
        }
      }
    }
  }
}

void spawn_next_piece()
{
  g.cur_type = g.next_type;
  g.next_type = draw_piece_from_bag();
  g.cur_rot = 0;
  g.cur_x = 3;
  g.cur_y = 0;
  g.gravity_timer_ms = 0;
  update_next_grid();

  if (!fits(g.cur_type, g.cur_rot, g.cur_x, g.cur_y))
  {
    g.state = STATE_GAME_OVER;
    g.pending_events |= EVT_GAME_OVER;
  }
}

void collapse_cleared_rows()
{
  int32_t cleared = 0;
  int32_t write_row = BOARD_H - 1;
  for (int32_t r = BOARD_H - 1; r >= 0; --r)
  {
    if ((g.clearing_rows_mask & (1u << r)) != 0)
    {
      ++cleared;
    }
    else
    {
      if (write_row != r)
      {
        const auto src_begin = g.board.begin() + r * BOARD_W;
        const auto dst_begin = g.board.begin() + write_row * BOARD_W;
        copy(src_begin, src_begin + BOARD_W, dst_begin);
      }
      --write_row;
    }
  }
  while (write_row >= 0)
  {
    const auto row_begin = g.board.begin() + write_row * BOARD_W;
    fill(row_begin, row_begin + BOARD_W, static_cast<uint8_t>(0));
    --write_row;
  }

  if (cleared > 0)
  {
    static constexpr array<int32_t, 5> LINE_SCORES = {0, 100, 300, 500, 800};
    const int32_t pts = LINE_SCORES[static_cast<size_t>(min(cleared, 4))] * g.level;
    g.score += pts;
    g.high_score = max(g.high_score, g.score);
    g.lines += cleared;
    g.level = 1 + (g.lines / 10);
    g.pending_events |= (cleared >= 4) ? EVT_QUAD : EVT_CLEAR;
  }

  g.clearing_rows_mask = 0;
  g.clear_timer_ms = 0;
  spawn_next_piece();
}

void lock_current_piece()
{
  bool overflow = false;
  for (int32_t r = 0; r < 4; ++r)
  {
    for (int32_t c = 0; c < 4; ++c)
    {
      if (!shape_cell(g.cur_type, g.cur_rot, r, c))
        continue;
      const int32_t bx = g.cur_x + c;
      const int32_t by = g.cur_y + r;
      if (by < 0)
      {
        overflow = true;
      }
      else if (bx >= 0 && bx < BOARD_W && by < BOARD_H)
      {
        g.board[static_cast<size_t>(by * BOARD_W + bx)] =
            static_cast<uint8_t>(g.cur_type + 1);
      }
    }
  }

  if (overflow)
  {
    g.state = STATE_GAME_OVER;
    g.pending_events |= EVT_GAME_OVER;
    update_render_grid();
    return;
  }

  uint32_t mask = 0;
  int32_t count = 0;
  for (int32_t r = 0; r < BOARD_H; ++r)
  {
    const auto row_begin = g.board.begin() + r * BOARD_W;
    const bool full = all_of(row_begin, row_begin + BOARD_W, [](uint8_t cell)
                             { return cell != 0; });
    if (full)
    {
      mask |= (1u << r);
      ++count;
    }
  }

  g.pending_events |= EVT_LOCK;
  if (count > 0)
  {
    g.clearing_rows_mask = mask;
    g.clear_timer_ms = 130;
  }
  else
  {
    spawn_next_piece();
  }
}

bool try_move(int32_t dx, int32_t dy)
{
  if (fits(g.cur_type, g.cur_rot, g.cur_x + dx, g.cur_y + dy))
  {
    g.cur_x += dx;
    g.cur_y += dy;
    if (dx != 0)
    {
      g.pending_events |= EVT_MOVE;
    }
    return true;
  }
  return false;
}

bool try_rotate_cw()
{
  const int32_t next_rot = (g.cur_rot + 1) & 3;
  for (const auto &kick : KICKS)
  {
    const int32_t kx = g.cur_x + kick[0];
    const int32_t ky = g.cur_y + kick[1];
    if (fits(g.cur_type, next_rot, kx, ky))
    {
      g.cur_rot = next_rot;
      g.cur_x = kx;
      g.cur_y = ky;
      g.pending_events |= EVT_ROTATE;
      return true;
    }
  }
  return false;
}

void hard_drop()
{
  int32_t dropped = 0;
  while (fits(g.cur_type, g.cur_rot, g.cur_x, g.cur_y + 1))
  {
    ++g.cur_y;
    ++dropped;
  }
  g.score += dropped * 2;
  g.high_score = max(g.high_score, g.score);
  lock_current_piece();
}

int32_t gravity_interval_ms()
{
  return max(75, 820 - (g.level - 1) * 70);
}

void reset_board_and_start(uint32_t seed)
{
  if (seed != 0)
  {
    g.rng.seed(seed);
  }
  fill(g.board.begin(), g.board.end(), static_cast<uint8_t>(0));
  fill(g.render_grid.begin(), g.render_grid.end(), static_cast<uint8_t>(0));

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
  g.bag_index = g.bag.size();

  g.next_type = draw_piece_from_bag();
  g.state = STATE_PLAYING;
  spawn_next_piece();
  update_render_grid();
}

extern "C"
{

  WASM_EXPORT(init_game)
  void init_game(uint32_t seed)
  {
    if (seed != 0)
    {
      g.rng.seed(seed);
    }
    fill(g.board.begin(), g.board.end(), static_cast<uint8_t>(0));
    fill(g.render_grid.begin(), g.render_grid.end(), static_cast<uint8_t>(0));

    g.state = STATE_TITLE;
    g.score = 0;
    g.lines = 0;
    g.level = 1;
    g.elapsed_ms = 0;
    g.bag_index = g.bag.size();
    g.next_type = draw_piece_from_bag();
    update_next_grid();
    update_render_grid();
  }

  WASM_EXPORT(start_game)
  void start_game(uint32_t seed)
  {
    reset_board_and_start(seed);
  }

  WASM_EXPORT(toggle_pause)
  void toggle_pause()
  {
    if (g.state == STATE_PLAYING)
    {
      g.state = STATE_PAUSED;
      g.hold_left = false;
      g.hold_right = false;
      g.hold_down = false;
      g.das_dir = 0;
    }
    else if (g.state == STATE_PAUSED)
    {
      g.state = STATE_PLAYING;
    }
    update_render_grid();
  }

  WASM_EXPORT(on_key_event)
  void on_key_event(int32_t key_code, int32_t is_down)
  {
    const bool down = is_down != 0;

    if (down)
    {
      if (key_code == KEY_P || key_code == KEY_ESC)
      {
        if (g.state == STATE_PLAYING || g.state == STATE_PAUSED)
        {
          toggle_pause();
        }
        return;
      }
      if (key_code == KEY_R)
      {
        reset_board_and_start(static_cast<uint32_t>(g.rng()) ^
                              static_cast<uint32_t>(g.elapsed_ms + 1));
        return;
      }
      if (key_code == KEY_ENTER && (g.state == STATE_TITLE || g.state == STATE_GAME_OVER))
      {
        reset_board_and_start(static_cast<uint32_t>(g.rng()) ^ 0xA5A5A5A5u);
        return;
      }
    }

    const bool is_left = (key_code == KEY_LEFT || key_code == KEY_A);
    const bool is_right = (key_code == KEY_RIGHT || key_code == KEY_D);
    const bool is_down_key = (key_code == KEY_DOWN || key_code == KEY_S);
    const bool is_rotate = (key_code == KEY_UP || key_code == KEY_W);
    const bool is_hard_drop = (key_code == KEY_SPACE);

    if (is_left)
    {
      if (down && !g.hold_left)
      {
        g.hold_left = true;
        g.das_dir = -1;
        g.das_timer_ms = 0;
        g.arr_timer_ms = 0;
        if (g.state == STATE_PLAYING && g.clear_timer_ms == 0)
        {
          try_move(-1, 0);
        }
      }
      else if (!down)
      {
        g.hold_left = false;
        if (g.hold_right)
        {
          g.das_dir = 1;
          g.das_timer_ms = 0;
          g.arr_timer_ms = 0;
        }
        else if (g.das_dir == -1)
        {
          g.das_dir = 0;
        }
      }
    }
    else if (is_right)
    {
      if (down && !g.hold_right)
      {
        g.hold_right = true;
        g.das_dir = 1;
        g.das_timer_ms = 0;
        g.arr_timer_ms = 0;
        if (g.state == STATE_PLAYING && g.clear_timer_ms == 0)
        {
          try_move(1, 0);
        }
      }
      else if (!down)
      {
        g.hold_right = false;
        if (g.hold_left)
        {
          g.das_dir = -1;
          g.das_timer_ms = 0;
          g.arr_timer_ms = 0;
        }
        else if (g.das_dir == 1)
        {
          g.das_dir = 0;
        }
      }
    }
    else if (is_down_key)
    {
      if (down && !g.hold_down)
      {
        g.hold_down = true;
        g.soft_drop_timer_ms = 0;
        if (g.state == STATE_PLAYING && g.clear_timer_ms == 0)
        {
          if (try_move(0, 1))
          {
            g.score += 1;
            g.high_score = max(g.high_score, g.score);
            g.gravity_timer_ms = 0;
          }
        }
      }
      else if (!down)
      {
        g.hold_down = false;
      }
    }
    else if (is_rotate && down)
    {
      if (g.state == STATE_PLAYING && g.clear_timer_ms == 0)
      {
        try_rotate_cw();
      }
    }
    else if (is_hard_drop && down)
    {
      if (g.state == STATE_PLAYING && g.clear_timer_ms == 0)
      {
        hard_drop();
      }
      else if (g.state == STATE_TITLE || g.state == STATE_GAME_OVER)
      {
        reset_board_and_start(static_cast<uint32_t>(g.rng()) ^ 0x5A5A5A5Au);
      }
    }

    update_render_grid();
  }

  WASM_EXPORT(tick)
  void tick(int32_t dt_ms)
  {
    dt_ms = clamp(dt_ms, 0, 100);

    if (g.state != STATE_PLAYING)
    {
      update_render_grid();
      return;
    }

    g.elapsed_ms += dt_ms;

    if (g.clear_timer_ms > 0)
    {
      g.clear_timer_ms -= dt_ms;
      if (g.clear_timer_ms <= 0)
      {
        collapse_cleared_rows();
      }
      update_render_grid();
      return;
    }

    if (g.das_dir != 0)
    {
      g.das_timer_ms += dt_ms;
      if (g.das_timer_ms >= 145)
      {
        g.arr_timer_ms += dt_ms;
        while (g.arr_timer_ms >= 38)
        {
          g.arr_timer_ms -= 38;
          try_move(g.das_dir, 0);
        }
      }
    }

    if (g.hold_down)
    {
      g.soft_drop_timer_ms += dt_ms;
      while (g.soft_drop_timer_ms >= 42)
      {
        g.soft_drop_timer_ms -= 42;
        if (try_move(0, 1))
        {
          g.score += 1;
          g.high_score = max(g.high_score, g.score);
          g.gravity_timer_ms = 0;
        }
        else
        {
          lock_current_piece();
          update_render_grid();
          return;
        }
      }
    }

    g.gravity_timer_ms += dt_ms;
    const int32_t interval = gravity_interval_ms();
    if (g.gravity_timer_ms >= interval)
    {
      g.gravity_timer_ms -= interval;
      if (!try_move(0, 1))
      {
        lock_current_piece();
      }
    }

    update_render_grid();
  }

  WASM_EXPORT(get_render_grid)
  const uint8_t *get_render_grid() { return g.render_grid.data(); }

  WASM_EXPORT(get_next_grid)
  const uint8_t *get_next_grid() { return g.next_grid.data(); }

  WASM_EXPORT(get_board_width)
  int32_t get_board_width() { return BOARD_W; }

  WASM_EXPORT(get_board_height)
  int32_t get_board_height() { return BOARD_H; }

  WASM_EXPORT(get_score)
  int32_t get_score() { return g.score; }

  WASM_EXPORT(get_high_score)
  int32_t get_high_score() { return g.high_score; }

  WASM_EXPORT(get_lines)
  int32_t get_lines() { return g.lines; }

  WASM_EXPORT(get_level)
  int32_t get_level() { return g.level; }

  WASM_EXPORT(get_elapsed_ms)
  int32_t get_elapsed_ms() { return g.elapsed_ms; }

  WASM_EXPORT(get_state)
  int32_t get_state() { return g.state; }

  WASM_EXPORT(consume_events)
  uint32_t consume_events()
  {
    const uint32_t ev = g.pending_events;
    g.pending_events = 0;
    return ev;
  }

} // extern "C"