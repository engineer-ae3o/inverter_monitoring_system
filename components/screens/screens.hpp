#ifndef _SCREENS_HPP_
#define _SCREENS_HPP_


#include "lvgl.h"

#include "system.hpp"
#include "config.hpp"
#include "display.hpp"

#include <array>


namespace display {
    
    // Only 4 regular screens that can be accessed by regular button presses
    constexpr inline uint8_t NUM_SCREENS{6};
    inline std::array<lv_obj_t*, NUM_SCREENS> screens{};

    // Screens creation
    void create_screen_0();
    void create_screen_1();
    void create_screen_2();
    void create_screen_3();
    void create_screen_4(const graph_samples_t& samples);
    void create_screen_5(const graph_samples_t& samples);

    // Updating screens
    void update_screen_0(const sys::data_t& data);
    void update_screen_1(const sys::data_t& data);
    void update_screen_2(const sys::data_t& data);
    void update_screen_3(const sys::data_t& data);
    void update_screen_4(const sys::data_t& data);
    void update_screen_5(const sys::data_t& data);
    
} // namespace display


#endif // _SCREENS_HPP_