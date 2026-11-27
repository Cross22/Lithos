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

  static bool isUp(int32_t key) { return key == UP || key == W; }
  static bool isLeft(int32_t key) { return key == LEFT || key == A; }
  static bool isDown(int32_t key) { return key == DOWN || key == S; }
  static bool isRight(int32_t key) { return key == RIGHT || key == D; }
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

template <typename T, size_t X, size_t Y>
using array2d = array<array<T, X>, Y>;

// 7 tetrominoes (0..6 -> tile values 1..7),
// 4 rotations, 4x4 bitmask packed in 16 bits
// constexpr array<array<uint16_t, 4>, 7> SHAPES = {{
constexpr array2d<uint16_t, 4, 7> SHAPES = {{
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

class Bag
{
public:
  Bag(uint32_t seed = 0x9E3779B9u) : rng(seed), index(bag.size()) {}
  int32_t draw_piece()
  {
    if (index >= bag.size())
    {
      refill();
    }
    return static_cast<int32_t>(bag[index++]);
  }

protected:
  void refill()
  {
    // Set bag[0..7] to [0..7], then shuffle those values
    ranges::iota(bag, 0);
    ranges::shuffle(bag, rng);
    index = 0;
  }

protected:
  array<uint8_t, 7> bag;
  size_t index = 7;
  mt19937 rng;
};

class Game
{
public:
  array<uint8_t, CONFIG::SIZE> board{};
  array<uint8_t, CONFIG::SIZE> render_grid{};
  array<uint8_t, 16> next_grid{};

  Bag bag;
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

  uint32_t gravity_timer_ms = 0;
  uint32_t clear_timer_ms = 0;
  uint32_t clearing_rows_mask = 0;

  // Input state for smooth DAS / ARR
  // DAS: Delayed Auto Start
  // ARR: Auto Repeat Rate
  // DAS is initial delay for key repeat to start after holding down a key and
  // ARR is delay between subsequent key repeats
  bool hold_left = false;
  bool hold_right = false;
  bool hold_down = false;
  int32_t das_dir = 0;
  int32_t das_timer_ms = 0;
  int32_t arr_timer_ms = 0;
  int32_t soft_drop_timer_ms = 0;

  uint32_t pending_events = 0;

  void init(uint32_t seed)
  {
    ranges::fill(board, 0);
    ranges::fill(render_grid, 0);

    state = State::TITLE;
    score = 0;
    lines = 0;
    level = 1;
    elapsed_ms = 0;
    clear_timer_ms = 0;
    clearing_rows_mask = 0;
    hold_left = false;
    hold_right = false;
    hold_down = false;
    das_dir = 0;
    das_timer_ms = 0;
    arr_timer_ms = 0;
    soft_drop_timer_ms = 0;
    pending_events = 0;

    bag = Bag(seed);
    next_type = bag.draw_piece();
    update_next_grid();
    update_render_grid();
  }

  void start(uint32_t seed)
  {
    init(seed);
    state = State::PLAYING;
    spawn_next_piece();
    update_render_grid();
  }

  void togglePause()
  {
    if (state == State::PLAYING)
    {
      state = State::PAUSED;
      hold_left = false;
      hold_right = false;
      hold_down = false;
      das_dir = 0;
      update_render_grid();
    }
    if (state == State::PAUSED)
    {
      state = State::PLAYING;
      update_render_grid();
    }
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
        if (by >= 0 && board[static_cast<size_t>(by * CONFIG::WIDTH + bx)] != 0)
        {
          return false;
        }
      }
    }
    return true;
  }

  void update_next_grid()
  {
    ranges::fill(next_grid, 0);
    for (int32_t r = 0; r < 4; ++r)
    {
      for (int32_t c = 0; c < 4; ++c)
      {
        if (shape_cell(next_type, 0, r, c))
        {
          next_grid[static_cast<size_t>(r * 4 + c)] = static_cast<uint8_t>(next_type + 1);
        }
      }
    }
  }

  void update_render_grid()
  {
    for (int32_t r = 0; r < CONFIG::HEIGHT; ++r)
    {
      const bool is_clearing =
          (clear_timer_ms > 0) && ((clearing_rows_mask & (1u << r)) != 0);
      for (int32_t c = 0; c < CONFIG::WIDTH; ++c)
      {
        const size_t idx = static_cast<size_t>(r * CONFIG::WIDTH + c);
        const uint8_t cell = board[idx];
        if (is_clearing && cell != 0)
        {
          render_grid[idx] = static_cast<uint8_t>(32 + cell);
        }
        else
        {
          render_grid[idx] = cell;
        }
      }
    }

    if (state == State::PLAYING && clear_timer_ms == 0)
    {
      // Project ghost landing position
      int32_t ghost_y = cur_y;
      while (fits(cur_type, cur_rot, cur_x, ghost_y + 1))
      {
        ++ghost_y;
      }

      for (int32_t r = 0; r < 4; ++r)
      {
        for (int32_t c = 0; c < 4; ++c)
        {
          if (!shape_cell(cur_type, cur_rot, r, c))
            continue;
          const int32_t bx = cur_x + c;
          const int32_t gy = ghost_y + r;
          if (bx >= 0 && bx < CONFIG::WIDTH && gy >= 0 && gy < CONFIG::HEIGHT)
          {
            const size_t idx = static_cast<size_t>(gy * CONFIG::WIDTH + bx);
            if (render_grid[idx] == 0)
            {
              render_grid[idx] = static_cast<uint8_t>(17 + cur_type);
            }
          }
        }
      }

      // Active falling piece overwrites ghost
      for (int32_t r = 0; r < 4; ++r)
      {
        for (int32_t c = 0; c < 4; ++c)
        {
          if (!shape_cell(cur_type, cur_rot, r, c))
            continue;
          const int32_t bx = cur_x + c;
          const int32_t by = cur_y + r;
          if (bx >= 0 && bx < CONFIG::WIDTH && by >= 0 && by < CONFIG::HEIGHT)
          {
            render_grid[static_cast<size_t>(by * CONFIG::WIDTH + bx)] =
                static_cast<uint8_t>(cur_type + 1);
          }
        }
      }
    }
  }

  void spawn_next_piece()
  {
    cur_type = next_type;
    next_type = bag.draw_piece();
    cur_rot = 0;
    cur_x = 3;
    cur_y = 0;
    gravity_timer_ms = 0;
    update_next_grid();

    if (!fits(cur_type, cur_rot, cur_x, cur_y))
    {
      state = State::GAME_OVER;
      pending_events |= Event::GAME_OVER;
    }
  }

  void collapse_cleared_rows()
  {
    int32_t cleared = 0;
    int32_t write_row = CONFIG::HEIGHT - 1;
    for (int32_t r = CONFIG::HEIGHT - 1; r >= 0; --r)
    {
      if ((clearing_rows_mask & (1u << r)) != 0)
      {
        ++cleared;
      }
      else
      {
        if (write_row != r)
        {
          const auto src_begin = board.begin() + r * CONFIG::WIDTH;
          const auto dst_begin = board.begin() + write_row * CONFIG::WIDTH;
          copy(src_begin, src_begin + CONFIG::WIDTH, dst_begin);
        }
        --write_row;
      }
    }
    while (write_row >= 0)
    {
      const auto row_begin = board.begin() + write_row * CONFIG::WIDTH;
      fill(row_begin, row_begin + CONFIG::WIDTH, static_cast<uint8_t>(0));
      --write_row;
    }

    if (cleared > 0)
    {
      static constexpr array<int32_t, 5> LINE_SCORES = {0, 100, 300, 500, 800};
      const int32_t pts = LINE_SCORES[static_cast<size_t>(min(cleared, 4))] * level;
      score += pts;
      high_score = max(high_score, score);
      lines += cleared;
      level = 1 + (lines / 10);
      pending_events |= (cleared >= 4) ? Event::QUAD : Event::CLEAR;
    }

    clearing_rows_mask = 0;
    clear_timer_ms = 0;
    spawn_next_piece();
  }

  void lock_current_piece()
  {
    bool overflow = false;
    for (int32_t r = 0; r < 4; ++r)
    {
      for (int32_t c = 0; c < 4; ++c)
      {
        if (!shape_cell(cur_type, cur_rot, r, c))
          continue;
        const int32_t bx = cur_x + c;
        const int32_t by = cur_y + r;
        if (by < 0)
        {
          overflow = true;
        }
        else if (bx >= 0 && bx < CONFIG::WIDTH && by < CONFIG::HEIGHT)
        {
          board[static_cast<size_t>(by * CONFIG::WIDTH + bx)] =
              static_cast<uint8_t>(cur_type + 1);
        }
      }
    }

    if (overflow)
    {
      state = State::GAME_OVER;
      pending_events |= Event::GAME_OVER;
      update_render_grid();
      return;
    }

    uint32_t mask = 0;
    int32_t count = 0;
    for (int32_t r = 0; r < CONFIG::HEIGHT; ++r)
    {
      const auto row_begin = board.begin() + r * CONFIG::WIDTH;
      const bool full = all_of(row_begin, row_begin + CONFIG::WIDTH, [](uint8_t cell)
                               { return cell != 0; });
      if (full)
      {
        mask |= (1u << r);
        ++count;
      }
    }

    pending_events |= Event::LOCK;
    if (count > 0)
    {
      clearing_rows_mask = mask;
      clear_timer_ms = 130;
    }
    else
    {
      spawn_next_piece();
    }
  }

  bool try_move(int32_t dx, int32_t dy)
  {
    if (fits(cur_type, cur_rot, cur_x + dx, cur_y + dy))
    {
      cur_x += dx;
      cur_y += dy;
      if (dx != 0)
      {
        pending_events |= Event::MOVE;
      }
      return true;
    }
    return false;
  }

  bool try_rotate_cw()
  {
    const int32_t next_rot = (cur_rot + 1) & 3;
    for (const auto &kick : KICKS)
    {
      const int32_t kx = cur_x + kick[0];
      const int32_t ky = cur_y + kick[1];
      if (fits(cur_type, next_rot, kx, ky))
      {
        cur_rot = next_rot;
        cur_x = kx;
        cur_y = ky;
        pending_events |= Event::ROTATE;
        return true;
      }
    }
    return false;
  }

  void hard_drop()
  {
    int32_t dropped = 0;
    while (fits(cur_type, cur_rot, cur_x, cur_y + 1))
    {
      ++cur_y;
      ++dropped;
    }
    score += dropped * 2;
    high_score = max(high_score, score);
    lock_current_piece();
  }

  int32_t gravity_interval_ms()
  {
    return max(75, 820 - (level - 1) * 70);
  }

  void onKeyPressed(int32_t key_code)
  {
    if (key_code == Key::P || key_code == Key::ESC)
    {
      togglePause();
      return;
    }
    if (key_code == Key::R)
    {
      start(elapsed_ms);
      return;
    }

    if (state == State::TITLE || state == State::GAME_OVER)
    {
      if (key_code == Key::ENTER || key_code == Key::SPACE)
      {
        start(elapsed_ms);
        return;
      }
    }

    const bool is_left = Key::isLeft(key_code);
    const bool is_right = Key::isRight(key_code);
    const bool is_down_key = Key::isDown(key_code);
    const bool is_rotate = Key::isUp(key_code);
    const bool is_hard_drop = (key_code == Key::SPACE);

    if (is_left)
    {
      if (!hold_left)
      {
        hold_left = true;
        das_dir = -1;
        das_timer_ms = 0;
        arr_timer_ms = 0;
        if (state == State::PLAYING && clear_timer_ms == 0)
        {
          try_move(-1, 0);
        }
      }
    }
    else if (is_right)
    {
      if (!hold_right)
      {
        hold_right = true;
        das_dir = 1;
        das_timer_ms = 0;
        arr_timer_ms = 0;
        if (state == State::PLAYING && clear_timer_ms == 0)
        {
          try_move(1, 0);
        }
      }
    }
    else if (is_down_key)
    {
      if (!hold_down)
      {
        hold_down = true;
        soft_drop_timer_ms = 0;
        if (state == State::PLAYING && clear_timer_ms == 0)
        {
          if (try_move(0, 1))
          {
            score += 1;
            high_score = max(high_score, score);
            gravity_timer_ms = 0;
          }
        }
      }
    }
    else if (is_rotate)
    {
      if (state == State::PLAYING && clear_timer_ms == 0)
      {
        try_rotate_cw();
      }
    }
    else if (is_hard_drop)
    {
      if (state == State::PLAYING && clear_timer_ms == 0)
      {
        hard_drop();
      }
    }

    update_render_grid();
  }

  void onKeyReleased(int32_t key_code)
  {
    const bool is_left = Key::isLeft(key_code);
    const bool is_right = Key::isRight(key_code);
    const bool is_down_key = Key::isDown(key_code);
    const bool is_rotate = Key::isUp(key_code);
    const bool is_hard_drop = (key_code == Key::SPACE);

    if (is_left)
    {

      hold_left = false;
      if (hold_right)
      {
        das_dir = 1;
        das_timer_ms = 0;
        arr_timer_ms = 0;
      }
      else if (das_dir == -1)
      {
        das_dir = 0;
      }
    }
    else if (is_right)
    {
      hold_right = false;
      if (hold_left)
      {
        das_dir = -1;
        das_timer_ms = 0;
        arr_timer_ms = 0;
      }
      else if (das_dir == 1)
      {
        das_dir = 0;
      }
    }
    else if (is_down_key)
    {
      hold_down = false;
    }

    update_render_grid();
  }
};
