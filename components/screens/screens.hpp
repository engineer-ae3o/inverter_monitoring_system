#ifndef _SCREENS_HPP_
#define _SCREENS_HPP_


#include "lvgl.h"
#include "system.hpp"

#include <array>


namespace display {
    
    constexpr inline uint8_t NUM_SCREENS  = 8;
    inline std::array<lv_obj_t*, NUM_SCREENS> screens{};

    // Screens creation
    void create_screen_1();
    void create_screen_2();
    void create_screen_3();
    void create_screen_4();
    void create_screen_5();
    void create_screen_6();
    void create_screen_7();
    void create_screen_8();

    // Updating screens
    void update_screen_1(const sys::data_t& data);
    void update_screen_2(const sys::data_t& data);
    void update_screen_3(const sys::data_t& data);
    void update_screen_4(const sys::data_t& data);
    void update_screen_5(const sys::data_t& data);
    void update_screen_6(const sys::data_t& data);
    void update_screen_7(const sys::data_t& data);
    void update_screen_8(const sys::data_t& data);
    
} // namespace display


#endif // _SCREENS_HPP_