#include "screens.hpp"
#include "colors.hpp"

namespace display {

    // Screen 1: Battery Focus
    static lv_obj_t* arc_battery                            = nullptr;
    static lv_obj_t* label_batt_voltage_large               = nullptr;
    static lv_obj_t* label_batt_percent_large               = nullptr;
    static lv_obj_t* label_batt_status_s1                   = nullptr;
    static lv_obj_t* label_runtime_s1                       = nullptr;

    // Screen 2: Power Metrics
    static lv_obj_t* label_current_large                    = nullptr;
    static lv_obj_t* label_power_large                      = nullptr;
    static lv_obj_t* bar_current                            = nullptr;
    static lv_obj_t* bar_power                              = nullptr;
    static lv_obj_t* label_inv_status_s2                    = nullptr;

    // Screen 3: Environment
    static lv_obj_t* label_temp_large                       = nullptr;
    static lv_obj_t* label_humidity_large                   = nullptr;
    static lv_obj_t* label_batt_status_s3                   = nullptr;
    static lv_obj_t* label_inv_status_s3                    = nullptr;

    // Screen 4: Overview
    static lv_obj_t* label_voltage_s4                       = nullptr;
    static lv_obj_t* label_current_s4                       = nullptr;
    static lv_obj_t* label_power_s4                         = nullptr;
    static lv_obj_t* label_temp_s4                          = nullptr;
    static lv_obj_t* label_humidity_s4                      = nullptr;
    static lv_obj_t* label_percent_s4                       = nullptr;
    static lv_obj_t* label_runtime_s4                       = nullptr;
    static lv_obj_t* label_batt_status_s4                   = nullptr;
    static lv_obj_t* label_inv_status_s4                    = nullptr;

    void create_screen_1(void) {

        screens[0] = lv_obj_create(nullptr);
        lv_obj_set_style_bg_color(screens[0], lv_color_hex(color::BLACK), 0);
        
        lv_obj_t* title = lv_label_create(screens[0]);
        lv_label_set_text(title, "BATTERY");
        lv_obj_set_style_text_color(title, lv_color_hex(color::CYAN), 0);
        lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 5);
        
        arc_battery = lv_arc_create(screens[0]);
        lv_obj_set_size(arc_battery, 100, 100);
        lv_arc_set_range(arc_battery, 0, 100);
        lv_arc_set_value(arc_battery, 0);
        lv_obj_set_style_arc_color(arc_battery, lv_color_hex(color::GREEN), LV_PART_INDICATOR);
        lv_obj_set_style_arc_width(arc_battery, 8, LV_PART_INDICATOR);
        lv_obj_set_style_arc_width(arc_battery, 8, LV_PART_MAIN);
        lv_obj_align(arc_battery, LV_ALIGN_TOP_MID, 0, 25);
        lv_obj_remove_style(arc_battery, nullptr, LV_PART_KNOB);
        
        label_batt_voltage_large = lv_label_create(screens[0]);
        lv_label_set_text(label_batt_voltage_large, "0.00V");
        lv_obj_set_style_text_color(label_batt_voltage_large, lv_color_hex(color::WHITE), 0);
        lv_obj_align(label_batt_voltage_large, LV_ALIGN_TOP_MID, 0, 60);
        
        label_batt_percent_large = lv_label_create(screens[0]);
        lv_label_set_text(label_batt_percent_large, "0.0%");
        lv_obj_set_style_text_color(label_batt_percent_large, lv_color_hex(color::YELLOW), 0);
        lv_obj_align(label_batt_percent_large, LV_ALIGN_TOP_MID, 0, 85);
        
        label_batt_status_s1 = lv_label_create(screens[0]);
        lv_label_set_text(label_batt_status_s1, "Status: IDLE");
        lv_obj_set_style_text_color(label_batt_status_s1, lv_color_hex(color::GREEN), 0);
        lv_obj_align(label_batt_status_s1, LV_ALIGN_TOP_LEFT, 5, 130);
        
        label_runtime_s1 = lv_label_create(screens[0]);
        lv_label_set_text(label_runtime_s1, "Runtime: 00:00:00");
        lv_obj_set_style_text_color(label_runtime_s1, lv_color_hex(color::CYAN), 0);
        lv_obj_align(label_runtime_s1, LV_ALIGN_TOP_LEFT, 5, 145);
    }

    void create_screen_2(void) {

        screens[1] = lv_obj_create(nullptr);
        lv_obj_set_style_bg_color(screens[1], lv_color_hex(color::BLACK), 0);
        
        lv_obj_t* title = lv_label_create(screens[1]);
        lv_label_set_text(title, "POWER");
        lv_obj_set_style_text_color(title, lv_color_hex(color::CYAN), 0);
        lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 5);
        
        lv_obj_t* current_label = lv_label_create(screens[1]);
        lv_label_set_text(current_label, "Current:");
        lv_obj_set_style_text_color(current_label, lv_color_hex(color::WHITE), 0);
        lv_obj_align(current_label, LV_ALIGN_TOP_LEFT, 5, 30);
        
        label_current_large = lv_label_create(screens[1]);
        lv_label_set_text(label_current_large, "0.00 A");
        lv_obj_set_style_text_color(label_current_large, lv_color_hex(color::YELLOW), 0);
        lv_obj_align(label_current_large, LV_ALIGN_TOP_RIGHT, -5, 30);
        
        bar_current = lv_bar_create(screens[1]);
        lv_obj_set_size(bar_current, 110, 15);
        lv_bar_set_range(bar_current, 0, 100);
        lv_bar_set_value(bar_current, 0, LV_ANIM_OFF);
        lv_obj_set_style_bg_color(bar_current, lv_color_hex(color::GREEN), LV_PART_INDICATOR);
        lv_obj_align(bar_current, LV_ALIGN_TOP_MID, 0, 50);
        
        lv_obj_t* power_label = lv_label_create(screens[1]);
        lv_label_set_text(power_label, "Power:");
        lv_obj_set_style_text_color(power_label, lv_color_hex(color::WHITE), 0);
        lv_obj_align(power_label, LV_ALIGN_TOP_LEFT, 5, 75);
        
        label_power_large = lv_label_create(screens[1]);
        lv_label_set_text(label_power_large, "0.00 W");
        lv_obj_set_style_text_color(label_power_large, lv_color_hex(color::YELLOW), 0);
        lv_obj_align(label_power_large, LV_ALIGN_TOP_RIGHT, -5, 75);
        
        bar_power = lv_bar_create(screens[1]);
        lv_obj_set_size(bar_power, 110, 15);
        lv_bar_set_range(bar_power, 0, 100);
        lv_bar_set_value(bar_power, 0, LV_ANIM_OFF);
        lv_obj_set_style_bg_color(bar_power, lv_color_hex(color::GREEN), LV_PART_INDICATOR);
        lv_obj_align(bar_power, LV_ALIGN_TOP_MID, 0, 95);
        
        label_inv_status_s2 = lv_label_create(screens[1]);
        lv_label_set_text(label_inv_status_s2, "Inverter: IDLE");
        lv_obj_set_style_text_color(label_inv_status_s2, lv_color_hex(color::GREEN), 0);
        lv_obj_align(label_inv_status_s2, LV_ALIGN_TOP_LEFT, 5, 125);
    }

    void create_screen_3(void) {

        screens[2] = lv_obj_create(nullptr);
        lv_obj_set_style_bg_color(screens[2], lv_color_hex(color::BLACK), 0);
        
        lv_obj_t* title = lv_label_create(screens[2]);
        lv_label_set_text(title, "ENVIRONMENT");
        lv_obj_set_style_text_color(title, lv_color_hex(color::CYAN), 0);
        lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 5);
        
        lv_obj_t* temp_label = lv_label_create(screens[2]);
        lv_label_set_text(temp_label, "Temperature:");
        lv_obj_set_style_text_color(temp_label, lv_color_hex(color::WHITE), 0);
        lv_obj_align(temp_label, LV_ALIGN_TOP_LEFT, 5, 30);
        
        label_temp_large = lv_label_create(screens[2]);
        lv_label_set_text(label_temp_large, "0.0°C");
        lv_obj_set_style_text_color(label_temp_large, lv_color_hex(color::YELLOW), 0);
        lv_obj_align(label_temp_large, LV_ALIGN_TOP_MID, 0, 50);
        
        lv_obj_t* humid_label = lv_label_create(screens[2]);
        lv_label_set_text(humid_label, "Humidity:");
        lv_obj_set_style_text_color(humid_label, lv_color_hex(color::WHITE), 0);
        lv_obj_align(humid_label, LV_ALIGN_TOP_LEFT, 5, 80);
        
        label_humidity_large = lv_label_create(screens[2]);
        lv_label_set_text(label_humidity_large, "0.0%");
        lv_obj_set_style_text_color(label_humidity_large, lv_color_hex(color::YELLOW), 0);
        lv_obj_align(label_humidity_large, LV_ALIGN_TOP_MID, 0, 100);
        
        label_batt_status_s3 = lv_label_create(screens[2]);
        lv_label_set_text(label_batt_status_s3, "Battery: IDLE");
        lv_obj_set_style_text_color(label_batt_status_s3, lv_color_hex(color::GREEN), 0);
        lv_obj_align(label_batt_status_s3, LV_ALIGN_TOP_LEFT, 5, 130);
        
        label_inv_status_s3 = lv_label_create(screens[2]);
        lv_label_set_text(label_inv_status_s3, "Inverter: IDLE");
        lv_obj_set_style_text_color(label_inv_status_s3, lv_color_hex(color::GREEN), 0);
        lv_obj_align(label_inv_status_s3, LV_ALIGN_TOP_LEFT, 5, 145);
    }

    void create_screen_4(void) {

        screens[3] = lv_obj_create(nullptr);
        lv_obj_set_style_bg_color(screens[3], lv_color_hex(color::BLACK), 0);
        
        lv_obj_t* title = lv_label_create(screens[3]);
        lv_label_set_text(title, "OVERVIEW");
        lv_obj_set_style_text_color(title, lv_color_hex(color::CYAN), 0);
        lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 5);
        
        uint8_t y_offset = 25;
        uint8_t y_spacing = 15;
        
        label_voltage_s4 = lv_label_create(screens[3]);
        lv_label_set_text(label_voltage_s4, "V: 0.00V");
        lv_obj_set_style_text_color(label_voltage_s4, lv_color_hex(color::GREEN), 0);
        lv_obj_align(label_voltage_s4, LV_ALIGN_TOP_LEFT, 5, y_offset);
        y_offset += y_spacing;
        
        label_current_s4 = lv_label_create(screens[3]);
        lv_label_set_text(label_current_s4, "I: 0.00A");
        lv_obj_set_style_text_color(label_current_s4, lv_color_hex(color::GREEN), 0);
        lv_obj_align(label_current_s4, LV_ALIGN_TOP_LEFT, 5, y_offset);
        y_offset += y_spacing;
        
        label_power_s4 = lv_label_create(screens[3]);
        lv_label_set_text(label_power_s4, "P: 0.00W");
        lv_obj_set_style_text_color(label_power_s4, lv_color_hex(color::GREEN), 0);
        lv_obj_align(label_power_s4, LV_ALIGN_TOP_LEFT, 5, y_offset);
        y_offset += y_spacing;
        
        label_percent_s4 = lv_label_create(screens[3]);
        lv_label_set_text(label_percent_s4, "SoC: 0.0%");
        lv_obj_set_style_text_color(label_percent_s4, lv_color_hex(color::GREEN), 0);
        lv_obj_align(label_percent_s4, LV_ALIGN_TOP_LEFT, 5, y_offset);
        y_offset += y_spacing;
        
        label_temp_s4 = lv_label_create(screens[3]);
        lv_label_set_text(label_temp_s4, "T: 0.0C");
        lv_obj_set_style_text_color(label_temp_s4, lv_color_hex(color::GREEN), 0);
        lv_obj_align(label_temp_s4, LV_ALIGN_TOP_LEFT, 5, y_offset);
        y_offset += y_spacing;
        
        label_humidity_s4 = lv_label_create(screens[3]);
        lv_label_set_text(label_humidity_s4, "H: 0.0%");
        lv_obj_set_style_text_color(label_humidity_s4, lv_color_hex(color::GREEN), 0);
        lv_obj_align(label_humidity_s4, LV_ALIGN_TOP_LEFT, 5, y_offset);
        y_offset += y_spacing;
        
        label_runtime_s4 = lv_label_create(screens[3]);
        lv_label_set_text(label_runtime_s4, "Run: 00:00:00");
        lv_obj_set_style_text_color(label_runtime_s4, lv_color_hex(color::GREEN), 0);
        lv_obj_align(label_runtime_s4, LV_ALIGN_TOP_LEFT, 5, y_offset);
        y_offset += y_spacing;
        
        label_batt_status_s4 = lv_label_create(screens[3]);
        lv_label_set_text(label_batt_status_s4, "Batt: IDLE");
        lv_obj_set_style_text_color(label_batt_status_s4, lv_color_hex(color::GREEN), 0);
        lv_obj_align(label_batt_status_s4, LV_ALIGN_TOP_LEFT, 5, y_offset);
        y_offset += y_spacing;
        
        label_inv_status_s4 = lv_label_create(screens[3]);
        lv_label_set_text(label_inv_status_s4, "Inv: IDLE");
        lv_obj_set_style_text_color(label_inv_status_s4, lv_color_hex(color::GREEN), 0);
        lv_obj_align(label_inv_status_s4, LV_ALIGN_TOP_LEFT, 5, y_offset);
    }

    void create_screen_5(void) {

        screens[4] = lv_obj_create(nullptr);
        lv_obj_set_style_bg_color(screens[3], lv_color_hex(color::BLACK), 0);
    }

    void create_screen_6(void) {

        screens[5] = lv_obj_create(nullptr);
        lv_obj_set_style_bg_color(screens[3], lv_color_hex(color::BLACK), 0);
    }

    void create_screen_7(void) {

        screens[6] = lv_obj_create(nullptr);
        lv_obj_set_style_bg_color(screens[3], lv_color_hex(color::BLACK), 0);
    }

    void create_screen_8(void) {

        screens[7] = lv_obj_create(nullptr);
        lv_obj_set_style_bg_color(screens[3], lv_color_hex(color::BLACK), 0);
    }

    void update_screen_1(const sys::data_t& data) {

        char buf[64] = "";
        
        lv_arc_set_value(arc_battery, (int16_t)data.battery_percent);
        if (data.battery_percent > 50.0f) {
            lv_obj_set_style_arc_color(arc_battery, lv_color_hex(color::GREEN), LV_PART_INDICATOR);
        } else if (data.battery_percent > 20.0f) {
            lv_obj_set_style_arc_color(arc_battery, lv_color_hex(color::YELLOW), LV_PART_INDICATOR);
        } else {
            lv_obj_set_style_arc_color(arc_battery, lv_color_hex(color::RED), LV_PART_INDICATOR);
        }
        
        snprintf(buf, sizeof(buf) - 1, "%.2fV", data.battery_voltage);
        lv_label_set_text(label_batt_voltage_large, buf);
        
        snprintf(buf, sizeof(buf) - 1, "%.1f%%", data.battery_percent);
        lv_label_set_text(label_batt_percent_large, buf);
        
        snprintf(buf, sizeof(buf) - 1, "Status: %s", sys::batt_status_to_string(data.batt_status));
        lv_label_set_text(label_batt_status_s1, buf);
        
        uint8_t hours = data.runtime_left_s / 3600;
        uint8_t minutes = (data.runtime_left_s % 3600) / 60;
        uint8_t seconds = data.runtime_left_s % 60;
        if (data.batt_status == sys::batt_status_t::RECHARGING) {
            snprintf(buf, sizeof(buf) - 1, "Full: %02u:%02u:%02u", hours, minutes, seconds);
        } else {
            snprintf(buf, sizeof(buf) - 1, "Runtime: %02u:%02u:%02u", hours, minutes, seconds);
        }
        lv_label_set_text(label_runtime_s1, buf);
    }

    void update_screen_2(const sys::data_t& data) {

        char buf[64] = "";
        
        snprintf(buf, sizeof(buf) - 1, "%.2f A", data.load_current_drawn);
        lv_label_set_text(label_current_large, buf);
        
        snprintf(buf, sizeof(buf) - 1, "%.2f W", data.power_drawn);
        lv_label_set_text(label_power_large, buf);
        
        int16_t current_bar_val = (int16_t)((data.load_current_drawn / 20.0f) * 100.0f);
        if (current_bar_val < 0) current_bar_val = 0;
        if (current_bar_val > 100) current_bar_val = 100;
        lv_bar_set_value(bar_current, current_bar_val, LV_ANIM_ON);
        
        int16_t power_bar_val = (int16_t)((data.power_drawn / 250.0f) * 100.0f);
        if (power_bar_val < 0) power_bar_val = 0;
        if (power_bar_val > 100) power_bar_val = 100;
        lv_bar_set_value(bar_power, power_bar_val, LV_ANIM_ON);
        
        snprintf(buf, sizeof(buf) - 1, "Inverter: %s", sys::inv_status_to_string(data.inv_status));
        lv_label_set_text(label_inv_status_s2, buf);
    }

    void update_screen_3(const sys::data_t& data) {

        char buf[64] = "";
        
        snprintf(buf, sizeof(buf) - 1, "%.1f°C", data.inv_temp);
        lv_label_set_text(label_temp_large, buf);
        
        snprintf(buf, sizeof(buf) - 1, "%.1f%%", data.inv_hmdt);
        lv_label_set_text(label_humidity_large, buf);
        
        snprintf(buf, sizeof(buf) - 1, "Battery: %s", sys::batt_status_to_string(data.batt_status));
        lv_label_set_text(label_batt_status_s3, buf);
        
        snprintf(buf, sizeof(buf) - 1, "Inverter: %s", sys::inv_status_to_string(data.inv_status));
        lv_label_set_text(label_inv_status_s3, buf);
    }

    void update_screen_4(const sys::data_t& data) {

        char buf[64] = "";

        uint8_t hours = data.runtime_left_s / 3600;
        uint8_t minutes = (data.runtime_left_s % 3600) / 60;
        uint8_t seconds = data.runtime_left_s % 60;

        snprintf(buf, sizeof(buf) - 1, "V: %.2fV", data.battery_voltage);
        lv_label_set_text(label_voltage_s4, buf);
        
        snprintf(buf, sizeof(buf) - 1, "I: %.2fA", data.load_current_drawn);
        lv_label_set_text(label_current_s4, buf);
        
        snprintf(buf, sizeof(buf) - 1, "P: %.2fW", data.power_drawn);
        lv_label_set_text(label_power_s4, buf);
        
        snprintf(buf, sizeof(buf) - 1, "T: %.1fC", data.inv_temp);
        lv_label_set_text(label_temp_s4, buf);
        
        snprintf(buf, sizeof(buf) - 1, "H: %.1f%%", data.inv_hmdt);
        lv_label_set_text(label_humidity_s4, buf);
        
        snprintf(buf, sizeof(buf) - 1, "SoC: %.1f%%", data.battery_percent);
        lv_label_set_text(label_percent_s4, buf);
        
        if (data.batt_status == sys::batt_status_t::RECHARGING) {
            snprintf(buf, sizeof(buf) - 1, "Full: %02d:%02d:%02d", hours, minutes, seconds);
        } else {
            snprintf(buf, sizeof(buf) - 1, "Run: %02d:%02d:%02d", hours, minutes, seconds);
        }
        lv_label_set_text(label_runtime_s4, buf);
        
        snprintf(buf, sizeof(buf) - 1, "Batt: %s", sys::batt_status_to_string(data.batt_status));
        lv_label_set_text(label_batt_status_s4, buf);
        
        snprintf(buf, sizeof(buf) - 1, "Inv: %s", sys::inv_status_to_string(data.inv_status));
        lv_label_set_text(label_inv_status_s4, buf);
    }

    void update_screen_5(const sys::data_t& data) {

    }

    void update_screen_6(const sys::data_t& data) {

    }

    void update_screen_7(const sys::data_t& data) {

    }

    void update_screen_8(const sys::data_t& data) {

    }
} // namespace display