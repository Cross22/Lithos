#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <numeric>
#include <random>
#include <concepts>
#include <type_traits>
#include "helpers.h"

using namespace std;

class Game
{
public:
  static constexpr int32_t WIDTH = 10;
  static constexpr int32_t HEIGHT = 20;
  static constexpr int32_t SIZE = WIDTH * HEIGHT;

  array<uint8_t, SIZE> board{};
  array<uint8_t, SIZE> render_grid{};
  array<uint8_t, 16> next_grid{};

  Bag bag;
  State::Value state = State::TITLE;
  int32_t score = 0;
  int32_t high_score = 0;
  int32_t level = 1;
  uint32_t lines = 0;
  uint32_t elapsed_ms = 0;

  Shape nextShape;
  Shape currShape;
  int32_t cur_x = 3;
  int32_t cur_y = 0;

  int32_t gravity_timer_ms = 0;
  int32_t clear_timer_ms = 0;

  // Each bit corresponds to one row and is 1 if that row is about to be cleared
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

  void init()
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
    soft_drop_timer_ms = 0;
    pending_events = 0;

    hold_left = false;
    hold_right = false;
    hold_down = false;
    resetKeyRepeat();

    nextShape = bag.draw_piece();
    updateShapePreview();
    update_render_grid();
  }

  // Initial (DAS) and repeat (ARR) delays
  void resetKeyRepeat(int32_t xDirection = 0)
  {
    das_dir = xDirection;
    das_timer_ms = 0;
    arr_timer_ms = 0;
  }

  void start(uint32_t seed)
  {
    init();
    bag = Bag(seed);
    nextShape = bag.draw_piece();
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
      return;
    }

    if (state == State::PAUSED)
    {
      state = State::PLAYING;
      update_render_grid();
    }
  }

  // Checks if shape typ with rotation rot can be placed at top-left board coordinates px/py
  // without overlaps
  bool fits(const Shape &shape, int32_t px, int32_t py)
  {
    for (int32_t r = 0; r < 4; ++r)
    {
      const int32_t by = py + r;
      for (int32_t c = 0; c < 4; ++c)
      {
        if (!shape.isPixelSet(r, c))
          continue;
        const int32_t bx = px + c;
        if (bx < 0 || bx >= WIDTH || by >= HEIGHT)
        {
          return false;
        }
        if (by >= 0 && board[(by * WIDTH + bx)] != 0)
        {
          return false;
        }
      }
    }
    return true;
  }

  // Update top-right preview window by setting its cell value to next_type+1
  void updateShapePreview()
  {
    // UI layer uses 0 as empty marker, so shape type becomes 1-based instead of 0-based
    uint8_t shapeValue = nextShape.getGridValue();

    ranges::fill(next_grid, 0);

    for (int32_t r = 0; r < 4; ++r)
    {
      for (int32_t c = 0; c < 4; ++c)
      {
        if (nextShape.isPixelSet(r, c))
        {
          auto index = (size_t)(r * 4 + c);
          next_grid[index] = shapeValue;
        }
      }
    }
  }

  // Active and not animating row clearing
  bool waitingForInput() const
  {
    return (state == State::PLAYING && clear_timer_ms == 0);
  }

  bool isClearingRow(int32_t row) const
  {
    return ((clearing_rows_mask & (1u << row)) != 0);
  }

  constexpr uint8_t clearingValue(uint8_t cellValue) const
  {
    return (uint8_t)(32 + cellValue);
  }

  void update_render_grid()
  {
    for (int32_t r = 0; r < HEIGHT; ++r)
    {
      const bool isClearing = (clear_timer_ms > 0) && isClearingRow(r);
      for (int32_t c = 0; c < WIDTH; ++c)
      {
        const size_t idx = (size_t)(r * WIDTH + c);
        const uint8_t cellContent = board[idx];
        render_grid[idx] = (isClearing && cellContent != 0) ? clearingValue(cellContent) : cellContent;
      }
    }

    if (!waitingForInput())
    {
      return;
    }

    // Project ghost landing position
    int32_t ghost_y = cur_y;
    while (fits(currShape, cur_x, ghost_y + 1))
    {
      ++ghost_y;
    }

    for (int32_t r = 0; r < 4; ++r)
    {
      const int32_t gy = ghost_y + r;
      if (gy < 0 || gy >= HEIGHT)
        continue;

      for (int32_t c = 0; c < 4; ++c)
      {
        if (!currShape.isPixelSet(r, c))
          continue;

        const int32_t bx = cur_x + c;
        if (bx >= 0 && bx < WIDTH)
        {
          const size_t idx = (size_t)(gy * WIDTH + bx);
          if (render_grid[idx] == 0)
          {
            render_grid[idx] = currShape.getGridGhostValue();
          }
        }
      }
    }

    // Active falling piece overwrites ghost
    for (int32_t r = 0; r < 4; ++r)
    {
      for (int32_t c = 0; c < 4; ++c)
      {
        if (!currShape.isPixelSet(r, c))
          continue;
        const int32_t bx = cur_x + c;
        const int32_t by = cur_y + r;
        if (bx >= 0 && bx < WIDTH && by >= 0 && by < HEIGHT)
        {
          render_grid[(by * WIDTH + bx)] = currShape.getGridValue();
        }
      }
    }
  }

  void spawn_next_piece()
  {
    currShape = std::move(nextShape);
    nextShape = bag.draw_piece();
    cur_x = 3;
    cur_y = 0;
    gravity_timer_ms = 0;
    updateShapePreview();

    if (!fits(currShape, cur_x, cur_y))
    {
      state = State::GAME_OVER;
      pending_events |= Event::GAME_OVER;
    }
  }

  void collapse_cleared_rows()
  {
    int32_t cleared = 0;
    int32_t write_row = HEIGHT - 1;
    for (int32_t r = HEIGHT - 1; r >= 0; --r)
    {
      if ((clearing_rows_mask & (1u << r)) != 0)
      {
        ++cleared;
      }
      else
      {
        if (write_row != r)
        {
          const auto src_begin = board.begin() + r * WIDTH;
          const auto dst_begin = board.begin() + write_row * WIDTH;
          copy(src_begin, src_begin + WIDTH, dst_begin);
        }
        --write_row;
      }
    }
    while (write_row >= 0)
    {
      const auto row_begin = board.begin() + write_row * WIDTH;
      fill(row_begin, row_begin + WIDTH, (uint8_t)(0));
      --write_row;
    }

    if (cleared > 0)
    {
      static constexpr array<int32_t, 5> LINE_SCORES = {0, 100, 300, 500, 800};
      const int32_t pts = LINE_SCORES[(size_t)(min(cleared, 4))] * level;
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
      const int32_t by = cur_y + r;
      if (by >= HEIGHT)
        continue;

      for (int32_t c = 0; c < 4; ++c)
      {
        if (!currShape.isPixelSet(r, c))
          continue;
        const int32_t bx = cur_x + c;
        if (by < 0)
        {
          overflow = true;
        }
        else if (bx >= 0 && bx < WIDTH)
        {
          board[(size_t)(by * WIDTH + bx)] = currShape.getGridValue();
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
    for (int32_t r = 0; r < HEIGHT; ++r)
    {
      const auto row_begin = board.begin() + r * WIDTH;
      const bool full = all_of(row_begin, row_begin + WIDTH, [](uint8_t cell)
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
    if (!waitingForInput())
    {
      return false;
    }

    if (fits(currShape, cur_x + dx, cur_y + dy))
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
    if (!waitingForInput())
    {
      return false;
    }

    Shape rotatedShape = currShape.makeRotatedShape();
    for (const auto &kick : KICKS)
    {
      const int32_t kx = cur_x + kick[0];
      const int32_t ky = cur_y + kick[1];
      if (fits(rotatedShape, kx, ky))
      {
        currShape = std::move(rotatedShape);
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
    if (!waitingForInput())
      return;

    int32_t dropped = 0;
    while (fits(currShape, cur_x, cur_y + 1))
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
    key_code = Key::normalize(key_code);

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

    const bool is_hard_drop = (key_code == Key::SPACE);
    if (key_code == Key::LEFT)
    {
      if (!hold_left)
      {
        hold_left = true;
        resetKeyRepeat(-1);
        try_move(-1, 0);
      }
    }
    else if (key_code == Key::RIGHT)
    {
      if (!hold_right)
      {
        hold_right = true;
        resetKeyRepeat(1);
        try_move(1, 0);
      }
    }
    else if (key_code == Key::DOWN)
    {
      if (!hold_down)
      {
        hold_down = true;
        soft_drop_timer_ms = 0;
        if (try_move(0, 1))
        {
          score += 1;
          high_score = max(high_score, score);
          gravity_timer_ms = 0;
        }
      }
    }
    else if (key_code == Key::UP)
    {
      try_rotate_cw();
    }
    else if (is_hard_drop)
    {
      hard_drop();
    }

    update_render_grid();
  }

  void onKeyReleased(int32_t key_code)
  {
    key_code = Key::normalize(key_code);

    if (key_code == Key::LEFT)
    {
      hold_left = false;
      if (hold_right)
      {
        resetKeyRepeat(1);
      }
      else if (das_dir == -1)
      {
        das_dir = 0;
      }
    }
    else if (key_code == Key::RIGHT)
    {
      hold_right = false;
      if (hold_left)
      {
        resetKeyRepeat(-1);
      }
      else if (das_dir == 1)
      {
        das_dir = 0;
      }
    }
    else if (key_code == Key::DOWN)
    {
      hold_down = false;
    }

    update_render_grid();
  }

  void tick(int32_t dt_ms)
  {
    dt_ms = clamp(dt_ms, 0, 100);

    if (state != State::PLAYING)
    {
      update_render_grid();
      return;
    }

    elapsed_ms += dt_ms;

    if (clear_timer_ms > 0)
    {
      clear_timer_ms -= dt_ms;
      if (clear_timer_ms <= 0)
      {
        collapse_cleared_rows();
      }
      update_render_grid();
      return;
    }

    constexpr int32_t INITIAL_DELAY = 145;
    constexpr int32_t REPEAT_DELAY = 38;

    if (das_dir != 0)
    {
      das_timer_ms += dt_ms;
      if (das_timer_ms >= INITIAL_DELAY)
      {
        arr_timer_ms += dt_ms;
        while (arr_timer_ms >= REPEAT_DELAY)
        {
          arr_timer_ms -= REPEAT_DELAY;
          try_move(das_dir, 0);
        }
      }
    }

    if (hold_down)
    {
      soft_drop_timer_ms += dt_ms;
      while (soft_drop_timer_ms >= REPEAT_DELAY)
      {
        soft_drop_timer_ms -= REPEAT_DELAY;
        if (try_move(0, 1))
        {
          score += 1;
          high_score = max(high_score, score);
          gravity_timer_ms = 0;
        }
        else
        {
          // Once we reach the bottom update the grid
          lock_current_piece();
          update_render_grid();
          return;
        }
      }
    }

    // Interval between automatic row drops depends on level
    const int32_t interval = gravity_interval_ms();
    gravity_timer_ms += dt_ms;
    if (gravity_timer_ms >= interval)
    {
      gravity_timer_ms -= interval;
      if (!try_move(0, 1))
      {
        lock_current_piece();
      }
    }

    update_render_grid();
  }
};
