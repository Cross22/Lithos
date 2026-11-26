#include <cstdint>

#include "game.h"

#define WASM_EXPORT(name) __attribute__((export_name(#name)))

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

    g.state = State::TITLE;
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
    if (g.state == State::PLAYING)
    {
      g.state = State::PAUSED;
      g.hold_left = false;
      g.hold_right = false;
      g.hold_down = false;
      g.das_dir = 0;
    }
    else if (g.state == State::PAUSED)
    {
      g.state = State::PLAYING;
    }
    update_render_grid();
  }

  WASM_EXPORT(on_key_event)
  void on_key_event(int32_t key_code, int32_t is_down)
  {
    const bool down = is_down != 0;

    if (down)
    {
      if (key_code == Key::P || key_code == Key::ESC)
      {
        if (g.state == State::PLAYING || g.state == State::PAUSED)
        {
          toggle_pause();
        }
        return;
      }
      if (key_code == Key::R)
      {
        reset_board_and_start(static_cast<uint32_t>(g.rng()) ^
                              static_cast<uint32_t>(g.elapsed_ms + 1));
        return;
      }
      if (key_code == Key::ENTER && (g.state == State::TITLE || g.state == State::GAME_OVER))
      {
        reset_board_and_start(static_cast<uint32_t>(g.rng()) ^ 0xA5A5A5A5u);
        return;
      }
    }

    const bool is_left = (key_code == Key::LEFT || key_code == Key::A);
    const bool is_right = (key_code == Key::RIGHT || key_code == Key::D);
    const bool is_down_key = (key_code == Key::DOWN || key_code == Key::S);
    const bool is_rotate = (key_code == Key::UP || key_code == Key::W);
    const bool is_hard_drop = (key_code == Key::SPACE);

    if (is_left)
    {
      if (down && !g.hold_left)
      {
        g.hold_left = true;
        g.das_dir = -1;
        g.das_timer_ms = 0;
        g.arr_timer_ms = 0;
        if (g.state == State::PLAYING && g.clear_timer_ms == 0)
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
        if (g.state == State::PLAYING && g.clear_timer_ms == 0)
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
        if (g.state == State::PLAYING && g.clear_timer_ms == 0)
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
      if (g.state == State::PLAYING && g.clear_timer_ms == 0)
      {
        try_rotate_cw();
      }
    }
    else if (is_hard_drop && down)
    {
      if (g.state == State::PLAYING && g.clear_timer_ms == 0)
      {
        hard_drop();
      }
      else if (g.state == State::TITLE || g.state == State::GAME_OVER)
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

    if (g.state != State::PLAYING)
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
  int32_t get_board_width() { return CONFIG::WIDTH; }

  WASM_EXPORT(get_board_height)
  int32_t get_board_height() { return CONFIG::HEIGHT; }

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