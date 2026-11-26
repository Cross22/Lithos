#include <algorithm>
#include <array>
#include <cstdint>
#include <numeric>
#include <random>
#include <concepts>
#include <type_traits>

using namespace std;

namespace CONFIG
{
  constexpr int32_t WIDTH = 10;
  constexpr int32_t HEIGHT = 20;
  constexpr int32_t SIZE = WIDTH * HEIGHT;
};

struct State
{
  enum Value
  {
    TITLE = 0,
    PLAYING = 1,
    PAUSED = 2,
    GAME_OVER = 3,
  };
};

// Key codes routed directly from browser KeyboardEvent.code mappings
struct Key
{
  enum
  {
    ENTER = 13,
    ESC = 27,
    SPACE = 32,
    LEFT = 37,
    UP = 38,
    RIGHT = 39,
    DOWN = 40,
    A = 65,
    D = 68,
    P = 80,
    R = 82,
    S = 83,
    W = 87,
  };
};

// Event bitmask consumed by frontend Web Audio synthesizer
struct Event
{
  enum
  {
    MOVE = 1,
    ROTATE = 2,
    LOCK = 4,
    CLEAR = 8,
    QUAD = 16,
    GAME_OVER = 32,
  };
};

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

class Game
{
public:
  array<uint8_t, CONFIG::SIZE> board{};
  array<uint8_t, CONFIG::SIZE> render_grid{};
  array<uint8_t, 16> next_grid{};

  State::Value state = State::TITLE;
  int32_t score = 0;
  int32_t high_score = 0;
  int32_t level = 1;
  uint32_t lines = 0;
  uint32_t elapsed_ms = 0;

  int32_t cur_type = 0;
  int32_t cur_rot = 0;
  int32_t cur_x = 3;
  int32_t cur_y = 0;
  int32_t next_type = 1;

  array<uint8_t, 7> bag{};
  size_t bag_index = 7;
  mt19937 rng{0x9E3779B9u};

  uint32_t gravity_timer_ms = 0;
  uint32_t clear_timer_ms = 0;
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

Game g;

void refill_bag()
{
  ranges::iota(g.bag, 0);
  ranges::shuffle(g.bag, g.rng);
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
      if (bx < 0 || bx >= CONFIG::WIDTH || by >= CONFIG::HEIGHT)
      {
        return false;
      }
      if (by >= 0 && g.board[static_cast<size_t>(by * CONFIG::WIDTH + bx)] != 0)
      {
        return false;
      }
    }
  }
  return true;
}

void update_next_grid()
{
  ranges::fill(g.next_grid, 0);
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
  for (int32_t r = 0; r < CONFIG::HEIGHT; ++r)
  {
    const bool is_clearing =
        (g.clear_timer_ms > 0) && ((g.clearing_rows_mask & (1u << r)) != 0);
    for (int32_t c = 0; c < CONFIG::WIDTH; ++c)
    {
      const size_t idx = static_cast<size_t>(r * CONFIG::WIDTH + c);
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

  if (g.state == State::PLAYING && g.clear_timer_ms == 0)
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
        if (bx >= 0 && bx < CONFIG::WIDTH && gy >= 0 && gy < CONFIG::HEIGHT)
        {
          const size_t idx = static_cast<size_t>(gy * CONFIG::WIDTH + bx);
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
        if (bx >= 0 && bx < CONFIG::WIDTH && by >= 0 && by < CONFIG::HEIGHT)
        {
          g.render_grid[static_cast<size_t>(by * CONFIG::WIDTH + bx)] =
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
    g.state = State::GAME_OVER;
    g.pending_events |= Event::GAME_OVER;
  }
}

void collapse_cleared_rows()
{
  int32_t cleared = 0;
  int32_t write_row = CONFIG::HEIGHT - 1;
  for (int32_t r = CONFIG::HEIGHT - 1; r >= 0; --r)
  {
    if ((g.clearing_rows_mask & (1u << r)) != 0)
    {
      ++cleared;
    }
    else
    {
      if (write_row != r)
      {
        const auto src_begin = g.board.begin() + r * CONFIG::WIDTH;
        const auto dst_begin = g.board.begin() + write_row * CONFIG::WIDTH;
        copy(src_begin, src_begin + CONFIG::WIDTH, dst_begin);
      }
      --write_row;
    }
  }
  while (write_row >= 0)
  {
    const auto row_begin = g.board.begin() + write_row * CONFIG::WIDTH;
    fill(row_begin, row_begin + CONFIG::WIDTH, static_cast<uint8_t>(0));
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
    g.pending_events |= (cleared >= 4) ? Event::QUAD : Event::CLEAR;
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
      else if (bx >= 0 && bx < CONFIG::WIDTH && by < CONFIG::HEIGHT)
      {
        g.board[static_cast<size_t>(by * CONFIG::WIDTH + bx)] =
            static_cast<uint8_t>(g.cur_type + 1);
      }
    }
  }

  if (overflow)
  {
    g.state = State::GAME_OVER;
    g.pending_events |= Event::GAME_OVER;
    update_render_grid();
    return;
  }

  uint32_t mask = 0;
  int32_t count = 0;
  for (int32_t r = 0; r < CONFIG::HEIGHT; ++r)
  {
    const auto row_begin = g.board.begin() + r * CONFIG::WIDTH;
    const bool full = all_of(row_begin, row_begin + CONFIG::WIDTH, [](uint8_t cell)
                             { return cell != 0; });
    if (full)
    {
      mask |= (1u << r);
      ++count;
    }
  }

  g.pending_events |= Event::LOCK;
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
      g.pending_events |= Event::MOVE;
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
      g.pending_events |= Event::ROTATE;
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
  ranges::fill(g.board, 0);
  ranges::fill(g.render_grid, 0);

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
  g.state = State::PLAYING;
  spawn_next_piece();
  update_render_grid();
}