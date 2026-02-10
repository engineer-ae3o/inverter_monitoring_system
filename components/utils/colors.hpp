#ifndef _COLORS_HPP_
#define _COLORS_HPP_

#include <cstdint>

namespace color {
    // RGB565 format: RRRRRGGGGGGBBBBB
    constexpr inline uint16_t RED          = 0xF800;
    constexpr inline uint16_t ORANGE       = 0xFC00;
    constexpr inline uint16_t YELLOW       = 0xFFE0;
    constexpr inline uint16_t GREEN        = 0x07E0;
    constexpr inline uint16_t BLUE         = 0x001F;
    constexpr inline uint16_t INDIGO       = 0x4810;
    constexpr inline uint16_t VIOLET       = 0x8010;
    constexpr inline uint16_t CYAN         = 0x07FF;
    constexpr inline uint16_t DARK_BLUE    = 0x001F;
    constexpr inline uint16_t WHITE        = 0xFFFF;
    constexpr inline uint16_t BLACK        = 0x0000;
    constexpr inline uint16_t GREY         = 0x7BEF;
    constexpr inline uint16_t DARK_GREY    = 0x294A;
    constexpr inline uint16_t OFF_WHITE    = 0xE71C;

} // namespace color

#endif // _COLORS_HPP_