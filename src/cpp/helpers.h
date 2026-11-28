#pragma once
#include <cstdint>

using namespace std;

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

  static int32_t normalize(int32_t key)
  {
    switch (key)
    {
    case W:
      return UP;
    case A:
      return LEFT;
    case S:
      return DOWN;
    case D:
      return RIGHT;
    }
    return key;
  }
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

class Shape
{
public:
  Shape(uint8_t type = 0, uint8_t rot = 0) : type(type), rot(rot)
  {
  }

  // Returns true if pixel is set for a given
  // shape of type "type" with the given rotation.
  // row/col offsets are shape-relative [0..3]
  bool isPixelSet(int32_t row, int32_t col) const
  {
    const auto &shape = Shape::SHAPES[type];
    const uint16_t mask = shape[rot % 4];
    const int32_t bit = 15 - (row * 4 + col);
    return ((mask >> bit) & 1u) != 0;
  }

  Shape makeRotatedShape() const
  {
    return Shape(type, (rot + 1) % 4);
  }

  // UI layer uses 0 as empty marker, so shape type becomes 1-based instead of 0-based
  uint8_t getGridValue() const
  {
    return type + 1;
  }

  uint8_t getGridGhostValue() const
  {
    return 17 + type;
  }

protected:
  uint8_t type;
  uint8_t rot;

  // 7 tetrominoes (0..6 -> tile values 1..7),
  // 4 rotations, 4x4 bitmask packed in 16 bits
  // constexpr array<array<uint16_t, 4>, 7> SHAPES = {{
  static constexpr array2d<uint16_t, 4, 7> SHAPES = {{
      {{0x0F00, 0x2222, 0x00F0, 0x4444}}, // 0: I
      {{0x6600, 0x6600, 0x6600, 0x6600}}, // 1: O
      {{0x4E00, 0x4640, 0x0E40, 0x4C40}}, // 2: T
      {{0x6C00, 0x4620, 0x06C0, 0x8C40}}, // 3: S
      {{0xC600, 0x2640, 0x0C60, 0x4C80}}, // 4: Z
      {{0x8E00, 0x6440, 0x0E20, 0x44C0}}, // 5: J
      {{0x2E00, 0x4460, 0x0E80, 0xC440}}, // 6: L
  }};
};

// Wall-kick candidate offsets (dx, dy)
constexpr array<array<int32_t, 2>, 8>
    KICKS = {{
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
  Shape draw_piece()
  {
    if (index >= bag.size())
    {
      refill();
    }
    const uint8_t shape_type = bag[index++];
    return Shape(shape_type);
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