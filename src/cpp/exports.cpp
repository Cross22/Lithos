#include <cstdint>

#include "game.h"

#define WASM_EXPORT(name) __attribute__((export_name(#name)))

Game g;

extern "C"
{

  WASM_EXPORT(init_game)
  void init_game(uint32_t seed) { g.init(seed); }

  WASM_EXPORT(start_game)
  void start_game(uint32_t seed) { g.start(seed); }

  WASM_EXPORT(toggle_pause)
  void toggle_pause() { g.togglePause(); }

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
  void tick(int32_t dt_ms) { g.tick(dt_ms); }

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