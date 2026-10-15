#pragma once

// Original hand-drawn 5x7 demo alphabet; no runtime font or system font dependency.
// Lowercase input uses the matching capital shape to keep the instrument lettering.
#include "visor/draw/mono_font.hpp"
#include <array>
#include <cstdint>

namespace visor::examples {

inline constexpr std::array<std::uint8_t, 539> kDemoBits{
    {
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // ' '
        0x20, 0x20, 0x20, 0x20, 0x20, 0x00, 0x20, // '!'
        0xc8, 0xd0, 0x10, 0x20, 0x40, 0x58, 0x98, // '%'
        0x10, 0x20, 0x40, 0x40, 0x40, 0x20, 0x10, // '('
        0x40, 0x20, 0x10, 0x10, 0x10, 0x20, 0x40, // ')'
        0x00, 0x20, 0x20, 0xf8, 0x20, 0x20, 0x00, // '+'
        0x00, 0x00, 0x00, 0x00, 0x00, 0x30, 0x20, // ','
        0x00, 0x00, 0x00, 0xf8, 0x00, 0x00, 0x00, // '-'
        0x00, 0x00, 0x00, 0x00, 0x00, 0x30, 0x30, // '.'
        0x08, 0x10, 0x10, 0x20, 0x40, 0x40, 0x80, // '/'
        0x70, 0x88, 0x98, 0xa8, 0xc8, 0x88, 0x70, // '0'
        0x20, 0x60, 0x20, 0x20, 0x20, 0x20, 0x70, // '1'
        0x70, 0x88, 0x08, 0x10, 0x20, 0x40, 0xf8, // '2'
        0xf0, 0x08, 0x08, 0x70, 0x08, 0x08, 0xf0, // '3'
        0x10, 0x30, 0x50, 0x90, 0xf8, 0x10, 0x10, // '4'
        0xf8, 0x80, 0x80, 0xf0, 0x08, 0x08, 0xf0, // '5'
        0x70, 0x80, 0x80, 0xf0, 0x88, 0x88, 0x70, // '6'
        0xf8, 0x08, 0x10, 0x20, 0x40, 0x40, 0x40, // '7'
        0x70, 0x88, 0x88, 0x70, 0x88, 0x88, 0x70, // '8'
        0x70, 0x88, 0x88, 0x78, 0x08, 0x08, 0x70, // '9'
        0x00, 0x30, 0x30, 0x00, 0x30, 0x30, 0x00, // ':'
        0x00, 0x30, 0x30, 0x00, 0x30, 0x20, 0x40, // ';'
        0x00, 0x00, 0xf8, 0x00, 0xf8, 0x00, 0x00, // '='
        0x70, 0x88, 0x08, 0x10, 0x20, 0x00, 0x20, // '?'
        0x70, 0x88, 0x88, 0xf8, 0x88, 0x88, 0x88, // 'A'
        0xf0, 0x88, 0x88, 0xf0, 0x88, 0x88, 0xf0, // 'B'
        0x78, 0x80, 0x80, 0x80, 0x80, 0x80, 0x78, // 'C'
        0xf0, 0x88, 0x88, 0x88, 0x88, 0x88, 0xf0, // 'D'
        0xf8, 0x80, 0x80, 0xf0, 0x80, 0x80, 0xf8, // 'E'
        0xf8, 0x80, 0x80, 0xf0, 0x80, 0x80, 0x80, // 'F'
        0x78, 0x80, 0x80, 0xb8, 0x88, 0x88, 0x78, // 'G'
        0x88, 0x88, 0x88, 0xf8, 0x88, 0x88, 0x88, // 'H'
        0xf8, 0x20, 0x20, 0x20, 0x20, 0x20, 0xf8, // 'I'
        0x38, 0x10, 0x10, 0x10, 0x90, 0x90, 0x60, // 'J'
        0x88, 0x90, 0xa0, 0xc0, 0xa0, 0x90, 0x88, // 'K'
        0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0xf8, // 'L'
        0x88, 0xd8, 0xa8, 0xa8, 0x88, 0x88, 0x88, // 'M'
        0x88, 0xc8, 0xc8, 0xa8, 0x98, 0x98, 0x88, // 'N'
        0x70, 0x88, 0x88, 0x88, 0x88, 0x88, 0x70, // 'O'
        0xf0, 0x88, 0x88, 0xf0, 0x80, 0x80, 0x80, // 'P'
        0x70, 0x88, 0x88, 0x88, 0xa8, 0x90, 0x68, // 'Q'
        0xf0, 0x88, 0x88, 0xf0, 0xa0, 0x90, 0x88, // 'R'
        0x78, 0x80, 0x80, 0x70, 0x08, 0x08, 0xf0, // 'S'
        0xf8, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, // 'T'
        0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x70, // 'U'
        0x88, 0x88, 0x88, 0x88, 0x88, 0x50, 0x20, // 'V'
        0x88, 0x88, 0x88, 0xa8, 0xa8, 0xd8, 0x88, // 'W'
        0x88, 0x88, 0x50, 0x20, 0x50, 0x88, 0x88, // 'X'
        0x88, 0x88, 0x50, 0x20, 0x20, 0x20, 0x20, // 'Y'
        0xf8, 0x08, 0x10, 0x20, 0x40, 0x80, 0xf8, // 'Z'
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xf8, // '_'
        0x70, 0x88, 0x88, 0xf8, 0x88, 0x88, 0x88, // 'a'
        0xf0, 0x88, 0x88, 0xf0, 0x88, 0x88, 0xf0, // 'b'
        0x78, 0x80, 0x80, 0x80, 0x80, 0x80, 0x78, // 'c'
        0xf0, 0x88, 0x88, 0x88, 0x88, 0x88, 0xf0, // 'd'
        0xf8, 0x80, 0x80, 0xf0, 0x80, 0x80, 0xf8, // 'e'
        0xf8, 0x80, 0x80, 0xf0, 0x80, 0x80, 0x80, // 'f'
        0x78, 0x80, 0x80, 0xb8, 0x88, 0x88, 0x78, // 'g'
        0x88, 0x88, 0x88, 0xf8, 0x88, 0x88, 0x88, // 'h'
        0xf8, 0x20, 0x20, 0x20, 0x20, 0x20, 0xf8, // 'i'
        0x38, 0x10, 0x10, 0x10, 0x90, 0x90, 0x60, // 'j'
        0x88, 0x90, 0xa0, 0xc0, 0xa0, 0x90, 0x88, // 'k'
        0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0xf8, // 'l'
        0x88, 0xd8, 0xa8, 0xa8, 0x88, 0x88, 0x88, // 'm'
        0x88, 0xc8, 0xc8, 0xa8, 0x98, 0x98, 0x88, // 'n'
        0x70, 0x88, 0x88, 0x88, 0x88, 0x88, 0x70, // 'o'
        0xf0, 0x88, 0x88, 0xf0, 0x80, 0x80, 0x80, // 'p'
        0x70, 0x88, 0x88, 0x88, 0xa8, 0x90, 0x68, // 'q'
        0xf0, 0x88, 0x88, 0xf0, 0xa0, 0x90, 0x88, // 'r'
        0x78, 0x80, 0x80, 0x70, 0x08, 0x08, 0xf0, // 's'
        0xf8, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, // 't'
        0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x70, // 'u'
        0x88, 0x88, 0x88, 0x88, 0x88, 0x50, 0x20, // 'v'
        0x88, 0x88, 0x88, 0xa8, 0xa8, 0xd8, 0x88, // 'w'
        0x88, 0x88, 0x50, 0x20, 0x50, 0x88, 0x88, // 'x'
        0x88, 0x88, 0x50, 0x20, 0x20, 0x20, 0x20, // 'y'
        0xf8, 0x08, 0x10, 0x20, 0x40, 0x80, 0xf8, // 'z'
    },
};

inline constexpr std::array<draw::GlyphEntry, 77> kDemoGlyphs{
    {
        {.codepoint = 32, .width = 5, .height = 7, .byte_offset = 0, .advance = 6},
        {.codepoint = 33, .width = 5, .height = 7, .byte_offset = 7, .advance = 6},
        {.codepoint = 37, .width = 5, .height = 7, .byte_offset = 14, .advance = 6},
        {.codepoint = 40, .width = 5, .height = 7, .byte_offset = 21, .advance = 6},
        {.codepoint = 41, .width = 5, .height = 7, .byte_offset = 28, .advance = 6},
        {.codepoint = 43, .width = 5, .height = 7, .byte_offset = 35, .advance = 6},
        {.codepoint = 44, .width = 5, .height = 7, .byte_offset = 42, .advance = 6},
        {.codepoint = 45, .width = 5, .height = 7, .byte_offset = 49, .advance = 6},
        {.codepoint = 46, .width = 5, .height = 7, .byte_offset = 56, .advance = 6},
        {.codepoint = 47, .width = 5, .height = 7, .byte_offset = 63, .advance = 6},
        {.codepoint = 48, .width = 5, .height = 7, .byte_offset = 70, .advance = 6},
        {.codepoint = 49, .width = 5, .height = 7, .byte_offset = 77, .advance = 6},
        {.codepoint = 50, .width = 5, .height = 7, .byte_offset = 84, .advance = 6},
        {.codepoint = 51, .width = 5, .height = 7, .byte_offset = 91, .advance = 6},
        {.codepoint = 52, .width = 5, .height = 7, .byte_offset = 98, .advance = 6},
        {.codepoint = 53, .width = 5, .height = 7, .byte_offset = 105, .advance = 6},
        {.codepoint = 54, .width = 5, .height = 7, .byte_offset = 112, .advance = 6},
        {.codepoint = 55, .width = 5, .height = 7, .byte_offset = 119, .advance = 6},
        {.codepoint = 56, .width = 5, .height = 7, .byte_offset = 126, .advance = 6},
        {.codepoint = 57, .width = 5, .height = 7, .byte_offset = 133, .advance = 6},
        {.codepoint = 58, .width = 5, .height = 7, .byte_offset = 140, .advance = 6},
        {.codepoint = 59, .width = 5, .height = 7, .byte_offset = 147, .advance = 6},
        {.codepoint = 61, .width = 5, .height = 7, .byte_offset = 154, .advance = 6},
        {.codepoint = 63, .width = 5, .height = 7, .byte_offset = 161, .advance = 6},
        {.codepoint = 65, .width = 5, .height = 7, .byte_offset = 168, .advance = 6},
        {.codepoint = 66, .width = 5, .height = 7, .byte_offset = 175, .advance = 6},
        {.codepoint = 67, .width = 5, .height = 7, .byte_offset = 182, .advance = 6},
        {.codepoint = 68, .width = 5, .height = 7, .byte_offset = 189, .advance = 6},
        {.codepoint = 69, .width = 5, .height = 7, .byte_offset = 196, .advance = 6},
        {.codepoint = 70, .width = 5, .height = 7, .byte_offset = 203, .advance = 6},
        {.codepoint = 71, .width = 5, .height = 7, .byte_offset = 210, .advance = 6},
        {.codepoint = 72, .width = 5, .height = 7, .byte_offset = 217, .advance = 6},
        {.codepoint = 73, .width = 5, .height = 7, .byte_offset = 224, .advance = 6},
        {.codepoint = 74, .width = 5, .height = 7, .byte_offset = 231, .advance = 6},
        {.codepoint = 75, .width = 5, .height = 7, .byte_offset = 238, .advance = 6},
        {.codepoint = 76, .width = 5, .height = 7, .byte_offset = 245, .advance = 6},
        {.codepoint = 77, .width = 5, .height = 7, .byte_offset = 252, .advance = 6},
        {.codepoint = 78, .width = 5, .height = 7, .byte_offset = 259, .advance = 6},
        {.codepoint = 79, .width = 5, .height = 7, .byte_offset = 266, .advance = 6},
        {.codepoint = 80, .width = 5, .height = 7, .byte_offset = 273, .advance = 6},
        {.codepoint = 81, .width = 5, .height = 7, .byte_offset = 280, .advance = 6},
        {.codepoint = 82, .width = 5, .height = 7, .byte_offset = 287, .advance = 6},
        {.codepoint = 83, .width = 5, .height = 7, .byte_offset = 294, .advance = 6},
        {.codepoint = 84, .width = 5, .height = 7, .byte_offset = 301, .advance = 6},
        {.codepoint = 85, .width = 5, .height = 7, .byte_offset = 308, .advance = 6},
        {.codepoint = 86, .width = 5, .height = 7, .byte_offset = 315, .advance = 6},
        {.codepoint = 87, .width = 5, .height = 7, .byte_offset = 322, .advance = 6},
        {.codepoint = 88, .width = 5, .height = 7, .byte_offset = 329, .advance = 6},
        {.codepoint = 89, .width = 5, .height = 7, .byte_offset = 336, .advance = 6},
        {.codepoint = 90, .width = 5, .height = 7, .byte_offset = 343, .advance = 6},
        {.codepoint = 95, .width = 5, .height = 7, .byte_offset = 350, .advance = 6},
        {.codepoint = 97, .width = 5, .height = 7, .byte_offset = 357, .advance = 6},
        {.codepoint = 98, .width = 5, .height = 7, .byte_offset = 364, .advance = 6},
        {.codepoint = 99, .width = 5, .height = 7, .byte_offset = 371, .advance = 6},
        {.codepoint = 100, .width = 5, .height = 7, .byte_offset = 378, .advance = 6},
        {.codepoint = 101, .width = 5, .height = 7, .byte_offset = 385, .advance = 6},
        {.codepoint = 102, .width = 5, .height = 7, .byte_offset = 392, .advance = 6},
        {.codepoint = 103, .width = 5, .height = 7, .byte_offset = 399, .advance = 6},
        {.codepoint = 104, .width = 5, .height = 7, .byte_offset = 406, .advance = 6},
        {.codepoint = 105, .width = 5, .height = 7, .byte_offset = 413, .advance = 6},
        {.codepoint = 106, .width = 5, .height = 7, .byte_offset = 420, .advance = 6},
        {.codepoint = 107, .width = 5, .height = 7, .byte_offset = 427, .advance = 6},
        {.codepoint = 108, .width = 5, .height = 7, .byte_offset = 434, .advance = 6},
        {.codepoint = 109, .width = 5, .height = 7, .byte_offset = 441, .advance = 6},
        {.codepoint = 110, .width = 5, .height = 7, .byte_offset = 448, .advance = 6},
        {.codepoint = 111, .width = 5, .height = 7, .byte_offset = 455, .advance = 6},
        {.codepoint = 112, .width = 5, .height = 7, .byte_offset = 462, .advance = 6},
        {.codepoint = 113, .width = 5, .height = 7, .byte_offset = 469, .advance = 6},
        {.codepoint = 114, .width = 5, .height = 7, .byte_offset = 476, .advance = 6},
        {.codepoint = 115, .width = 5, .height = 7, .byte_offset = 483, .advance = 6},
        {.codepoint = 116, .width = 5, .height = 7, .byte_offset = 490, .advance = 6},
        {.codepoint = 117, .width = 5, .height = 7, .byte_offset = 497, .advance = 6},
        {.codepoint = 118, .width = 5, .height = 7, .byte_offset = 504, .advance = 6},
        {.codepoint = 119, .width = 5, .height = 7, .byte_offset = 511, .advance = 6},
        {.codepoint = 120, .width = 5, .height = 7, .byte_offset = 518, .advance = 6},
        {.codepoint = 121, .width = 5, .height = 7, .byte_offset = 525, .advance = 6},
        {.codepoint = 122, .width = 5, .height = 7, .byte_offset = 532, .advance = 6},
    },
};

inline constexpr draw::MonoFont kDemoFont{
    .line_height = 7,
    .glyphs = kDemoGlyphs,
    .bits = kDemoBits,
};

} // namespace visor::examples
