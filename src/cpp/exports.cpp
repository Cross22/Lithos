#include <cstdint>

#include "game.h"

#define WASM_EXPORT(name) __attribute__((export_name(#name)))

Game g;

extern "C"
{

  WASM_EXPORT(init_game)
  void init_game(uint32_t seed)
  {
    g.init(seed);
  }

  WASM_EXPORT(start_game)
  void start_game(uint32_t seed)
  {
    g.start(seed);
  }

  WASM_EXPORT(toggle_pause)
  void toggle_pause()
  {
    g.togglePause();
  }

  WASM_EXPORT(on_key_event)
  void on_key_event(int32_t key_code, int32_t is_down)
  {
    const bool down = is_down != 0;

    if (down)
      g.onKeyPressed(key_code);
    else
      g.onKeyReleased(key_code);
  }

  WASM_EXPORT(tick)
  void tick(int32_t dt_ms)
  {
    dt_ms = clamp(dt_ms, 0, 100);

    if (g.state != State::PLAYING)
    {
      g.update_render_grid();
      return;
    }

    g.elapsed_ms += dt_ms;

    if (g.clear_timer_ms > 0)
    {
      g.clear_timer_ms -= dt_ms;
      if (g.clear_timer_ms <= 0)
      {
        g.collapse_cleared_rows();
      }
      g.update_render_grid();
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
          g.try_move(g.das_dir, 0);
        }
      }
    }

    if (g.hold_down)
    {
      g.soft_drop_timer_ms += dt_ms;
      while (g.soft_drop_timer_ms >= 42)
      {
        g.soft_drop_timer_ms -= 42;
        if (g.try_move(0, 1))
        {
          g.score += 1;
          g.high_score = max(g.high_score, g.score);
          g.gravity_timer_ms = 0;
        }
        else
        {
          g.lock_current_piece();
          g.update_render_grid();
          return;
        }
      }
    }

    g.gravity_timer_ms += dt_ms;
    const int32_t interval = g.gravity_interval_ms();
    if (g.gravity_timer_ms >= interval)
    {
      g.gravity_timer_ms -= interval;
      if (!g.try_move(0, 1))
      {
        g.lock_current_piece();
      }
    }

    g.update_render_grid();
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