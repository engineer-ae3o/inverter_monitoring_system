#ifndef _COLORS_HPP_
#define _COLORS_HPP_

#include <cstdint>

namespace color {
    // RGB565 format: RRRRRGGGGGGBBBBB
    constexpr inline uint16_t RED          = 0xF800;  // 11111 000000 00000
    constexpr inline uint16_t ORANGE       = 0xFC00;  // 11111 100000 00000
    constexpr inline uint16_t YELLOW       = 0xFFE0;  // 11111 111111 00000
    constexpr inline uint16_t GREEN        = 0x07E0;  // 00000 111111 00000
    constexpr inline uint16_t BLUE         = 0x001F;  // 00000 000000 11111
    constexpr inline uint16_t INDIGO       = 0x4810;  // 01001 000001 00000
    constexpr inline uint16_t VIOLET       = 0x8010;  // 10000 000001 00000
    constexpr inline uint16_t CYAN         = 0x07FF;  // 00000 111111 11111
    constexpr inline uint16_t DARK_BLUE    = 0x001F;  // 00000 000000 10000
    constexpr inline uint16_t WHITE        = 0xFFFF;  // 11111 111111 11111
    constexpr inline uint16_t BLACK        = 0x0000;  // 00000 000000 00000
} // namespace color

#endif // _COLORS_HPP_