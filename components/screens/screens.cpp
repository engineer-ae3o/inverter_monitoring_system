#include "screens.hpp"
#include "colors.hpp"


namespace display {

    // Screen 0: Status
    static lv_obj_t* label_s0_batt_badge                   = nullptr;
    static lv_obj_t* label_s0_batt_value                   = nullptr;
    static lv_obj_t* label_s0_inv_badge                    = nullptr;
    static lv_obj_t* label_s0_inv_value                    = nullptr;
    static lv_obj_t* label_s0_temp_badge                   = nullptr;
    static lv_obj_t* label_s0_temp_value                   = nullptr;
    static lv_obj_t* label_s0_hmdt_badge                   = nullptr;
    static lv_obj_t* label_s0_hmdt_value                   = nullptr;
    static lv_obj_t* label_s0_voltage                      = nullptr;
    static lv_obj_t* label_s0_current                      = nullptr;
    static lv_obj_t* label_s0_power                        = nullptr;
    static lv_obj_t* label_s0_runtime                      = nullptr;
    static lv_obj_t* label_s0_batt_status                  = nullptr;
    static lv_obj_t* label_s0_inv_status                   = nullptr;

    // Screen 1: Power
    static lv_obj_t* label_s1_power_hero                   = nullptr;
    static lv_obj_t* label_s1_voltage_val                  = nullptr;
    static lv_obj_t* bar_s1_voltage_fill                   = nullptr;
    static lv_obj_t* label_s1_voltage_tick                 = nullptr;
    static lv_obj_t* label_s1_current_val                  = nullptr;
    static lv_obj_t* bar_s1_current_fill                   = nullptr;
    static lv_obj_t* label_s1_current_tick                 = nullptr;
    static lv_obj_t* label_s1_inv_status                   = nullptr;
    static lv_obj_t* label_s1_batt_status                  = nullptr;

    // Screen 2: Environment
    static lv_obj_t* label_s2_temp_val                     = nullptr;
    static lv_obj_t* bar_s2_temp_fill                      = nullptr;
    static lv_obj_t* label_s2_temp_overlay                 = nullptr;
    static lv_obj_t* label_s2_temp_tick                    = nullptr;
    static lv_obj_t* label_s2_hmdt_val                     = nullptr;
    static lv_obj_t* bar_s2_hmdt_fill                      = nullptr;
    static lv_obj_t* label_s2_hmdt_overlay                 = nullptr;
    static lv_obj_t* label_s2_hmdt_tick                    = nullptr;
    static lv_obj_t* label_s2_runtime                      = nullptr;
    static lv_obj_t* label_s2_inv_status                   = nullptr;

    // Screen 3: Overview
    static lv_obj_t* dot_s3_voltage                        = nullptr;
    static lv_obj_t* label_s3_voltage_val                  = nullptr;
    static lv_obj_t* dot_s3_current                        = nullptr;
    static lv_obj_t* label_s3_current_val                  = nullptr;
    static lv_obj_t* dot_s3_power                          = nullptr;
    static lv_obj_t* label_s3_power_val                    = nullptr;
    static lv_obj_t* dot_s3_soc                            = nullptr;
    static lv_obj_t* label_s3_soc_val                      = nullptr;
    static lv_obj_t* dot_s3_temp                           = nullptr;
    static lv_obj_t* label_s3_temp_val                     = nullptr;
    static lv_obj_t* dot_s3_hmdt                           = nullptr;
    static lv_obj_t* label_s3_hmdt_val                     = nullptr;
    static lv_obj_t* label_s3_batt_status                  = nullptr;
    static lv_obj_t* label_s3_inv_status                   = nullptr;
    static lv_obj_t* label_s3_runtime                      = nullptr;

    // Screen 4: Graph - Temperature + Humidity
    static lv_obj_t* chart_env                             = nullptr;
    static lv_chart_series_t* series_temp                  = nullptr;
    static lv_chart_series_t* series_hmdt                  = nullptr;

    // Screen 5: Graph - Voltage + Current
    static lv_obj_t* chart_power                           = nullptr;
    static lv_chart_series_t* series_voltage               = nullptr;
    static lv_chart_series_t* series_current               = nullptr;


    // Forward declaration
    // Dark rounded rectangle panel
    static lv_obj_t* create_panel(lv_obj_t* parent, uint16_t x, uint16_t y, uint16_t w, uint16_t h);
    // 6x6 colored dot
    static lv_obj_t* create_dot(lv_obj_t* parent, uint16_t x, uint16_t y, lv_color_t col);
    // Style a label as a colored pill-badge
    static void style_badge(lv_obj_t* lbl, const char* text, lv_color_t bg, lv_color_t fg);

    // Public API
    // Screens Creation
    void create_screen_0() {

        screens[0] = lv_obj_create(nullptr);
        lv_obj_set_style_bg_color(screens[0], lv_color_hex(color::BLACK), 0);

        // Title
        lv_obj_t* title = lv_label_create(screens[0]);
        lv_label_set_text(title, "STATUS");
        lv_obj_set_style_text_color(title, lv_color_hex(color::CYAN), 0);
        lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 5);

        // 4 status cards
        // 240px wide, 4px outer margin each side, 4px gap between cards
        // card_w = (240 - 4*2 - 4*3) / 4 = (232 - 12)/4 = 55 → use 54 for breathing room
        static const uint16_t card_x[4]  = { 4, 62, 120, 178 };
        static const char*    card_name[4] = { "BATT", "INV", "TEMP", "HMDT" };

        lv_obj_t* badges[4]{};
        lv_obj_t* values[4]{};

        for (uint8_t i = 0; i < 4; i++) {
            lv_obj_t* card = create_panel(screens[0], card_x[i], 24, 54, 68);

            // Card title
            lv_obj_t* nm = lv_label_create(card);
            lv_label_set_text(nm, card_name[i]);
            lv_obj_set_style_text_color(nm, lv_color_hex(color::GREY), 0);
            lv_obj_set_style_text_font(nm, &lv_font_montserrat_10, 0);
            lv_obj_align(nm, LV_ALIGN_TOP_MID, 0, 3);

            // Badge
            badges[i] = lv_label_create(card);
            lv_label_set_text(badges[i], "—");
            lv_obj_set_style_text_font(badges[i], &lv_font_montserrat_10, 0);
            lv_obj_align(badges[i], LV_ALIGN_TOP_MID, 0, 17);

            // Value
            values[i] = lv_label_create(card);
            lv_label_set_text(values[i], "—");
            lv_obj_set_style_text_font(values[i], &lv_font_montserrat_10, 0);
            lv_obj_align(values[i], LV_ALIGN_TOP_MID, 0, 40);
        }

        label_s0_batt_badge = badges[0];   label_s0_batt_value = values[0];
        label_s0_inv_badge  = badges[1];   label_s0_inv_value  = values[1];
        label_s0_temp_badge = badges[2];   label_s0_temp_value = values[2];
        label_s0_hmdt_badge = badges[3];   label_s0_hmdt_value = values[3];

        // Live values panel
        lv_obj_t* live = create_panel(screens[0], 4, 98, 232, 88);

        lv_obj_t* live_hdr = lv_label_create(live);
        lv_label_set_text(live_hdr, "LIVE VALUES");
        lv_obj_set_style_text_color(live_hdr, lv_color_hex(color::GREY), 0);
        lv_obj_set_style_text_font(live_hdr, &lv_font_montserrat_10, 0);
        lv_obj_align(live_hdr, LV_ALIGN_TOP_MID, 0, 4);

        // 3 columns
        static const uint16_t col_x[3]    = { 10, 88, 166 };
        static const char*    col_name[3] = { "Voltage", "Current", "Power" };
        static const char*    col_unit[3] = { "V", "A", "W" };
        lv_obj_t* val_lbls[3]{};

        for (uint8_t i = 0; i < 3; i++) {
            lv_obj_t* nm = lv_label_create(live);
            lv_label_set_text(nm, col_name[i]);
            lv_obj_set_style_text_color(nm, lv_color_hex(color::GREY), 0);
            lv_obj_set_style_text_font(nm, &lv_font_montserrat_10, 0);
            lv_obj_set_pos(nm, col_x[i], 20);

            val_lbls[i] = lv_label_create(live);
            lv_label_set_text(val_lbls[i], "0.0");
            lv_obj_set_style_text_color(val_lbls[i], lv_color_hex(color::WHITE), 0);
            lv_obj_set_style_text_font(val_lbls[i], &lv_font_montserrat_20, 0);
            lv_obj_set_pos(val_lbls[i], col_x[i], 34);

            lv_obj_t* un = lv_label_create(live);
            lv_label_set_text(un, col_unit[i]);
            lv_obj_set_style_text_color(un, lv_color_hex(color::GREY), 0);
            lv_obj_set_style_text_font(un, &lv_font_montserrat_10, 0);
            lv_obj_set_pos(un, col_x[i], 58);
        }

        label_s0_voltage = val_lbls[0];
        label_s0_current = val_lbls[1];
        label_s0_power   = val_lbls[2];

        // Bottom row
        lv_obj_t* bot = create_panel(screens[0], 4, 192, 232, 52);

        static const uint16_t bot_x[3]   = { 10, 88, 166 };
        static const char*    bot_hdr[3] = { "Runtime", "Battery", "Inverter" };
        lv_obj_t* bot_vals[3]{};

        for (uint8_t i = 0; i < 3; i++) {
            lv_obj_t* hd = lv_label_create(bot);
            lv_label_set_text(hd, bot_hdr[i]);
            lv_obj_set_style_text_color(hd, lv_color_hex(color::GREY), 0);
            lv_obj_set_style_text_font(hd, &lv_font_montserrat_10, 0);
            lv_obj_set_pos(hd, bot_x[i], 5);

            bot_vals[i] = lv_label_create(bot);
            lv_label_set_text(bot_vals[i], "—");
            lv_obj_set_style_text_font(bot_vals[i], &lv_font_montserrat_12, 0);
            lv_obj_set_pos(bot_vals[i], bot_x[i], 22);
        }

        label_s0_runtime     = bot_vals[0];
        label_s0_batt_status = bot_vals[1];
        label_s0_inv_status  = bot_vals[2];
    }

    void create_screen_1() {

        screens[1] = lv_obj_create(nullptr);
        lv_obj_set_style_bg_color(screens[1], lv_color_hex(color::BLACK), 0);

        // Title
        lv_obj_t* title = lv_label_create(screens[1]);
        lv_label_set_text(title, "POWER");
        lv_obj_set_style_text_color(title, lv_color_hex(color::CYAN), 0);
        lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 5);

        // Hero panel
        lv_obj_t* hero = create_panel(screens[1], 4, 22, 232, 70);

        lv_obj_t* hero_lbl = lv_label_create(hero);
        lv_label_set_text(hero_lbl, "POWER DRAWN");
        lv_obj_set_style_text_color(hero_lbl, lv_color_hex(color::GREY), 0);
        lv_obj_set_style_text_font(hero_lbl, &lv_font_montserrat_10, 0);
        lv_obj_align(hero_lbl, LV_ALIGN_TOP_MID, 0, 6);

        label_s1_power_hero = lv_label_create(hero);
        lv_label_set_text(label_s1_power_hero, "0.0W");
        lv_obj_set_style_text_color(label_s1_power_hero, lv_color_hex(color::WHITE), 0);
        lv_obj_set_style_text_font(label_s1_power_hero, &lv_font_montserrat_36, 0);
        lv_obj_align(label_s1_power_hero, LV_ALIGN_TOP_MID, 0, 24);

        // Voltage panel
        // Bar maps 6.0V..12.6V across 216px (panel inner width with 8px pad each side)
        lv_obj_t* volt = create_panel(screens[1], 4, 98, 232, 82);

        lv_obj_t* volt_hdr = lv_label_create(volt);
        lv_label_set_text(volt_hdr, "VOLTAGE");
        lv_obj_set_style_text_color(volt_hdr, lv_color_hex(color::GREY), 0);
        lv_obj_set_style_text_font(volt_hdr, &lv_font_montserrat_10, 0);
        lv_obj_set_pos(volt_hdr, 8, 5);

        label_s1_voltage_val = lv_label_create(volt);
        lv_label_set_text(label_s1_voltage_val, "0.00 V");
        lv_obj_set_style_text_color(label_s1_voltage_val, lv_color_hex(color::WHITE), 0);
        lv_obj_set_style_text_font(label_s1_voltage_val, &lv_font_montserrat_10, 0);
        lv_obj_align(label_s1_voltage_val, LV_ALIGN_TOP_RIGHT, -8, 4);

        // Bar background (dark)
        lv_obj_t* volt_bg = lv_obj_create(volt);
        lv_obj_set_size(volt_bg, 216, 18);
        lv_obj_set_pos(volt_bg, 8, 24);
        lv_obj_set_style_bg_color(volt_bg, lv_color_hex(0x2a2a2a), 0);
        lv_obj_set_style_border_width(volt_bg, 0, 0);
        lv_obj_set_style_radius(volt_bg, 2, 0);

        // Bar fill (width updated in update_screen_1)
        bar_s1_voltage_fill = lv_obj_create(volt);
        lv_obj_set_size(bar_s1_voltage_fill, 0, 18);
        lv_obj_set_pos(bar_s1_voltage_fill, 8, 24);
        lv_obj_set_style_bg_color(bar_s1_voltage_fill, lv_color_hex(color::GREEN), 0);
        lv_obj_set_style_border_width(bar_s1_voltage_fill, 0, 0);
        lv_obj_set_style_radius(bar_s1_voltage_fill, 2, 0);

        // Tick mark
        label_s1_voltage_tick = lv_label_create(volt);
        lv_label_set_text(label_s1_voltage_tick, "|");
        lv_obj_set_style_text_color(label_s1_voltage_tick, lv_color_hex(color::WHITE), 0);
        lv_obj_set_style_text_font(label_s1_voltage_tick, &lv_font_montserrat_10, 0);
        lv_obj_set_pos(label_s1_voltage_tick, 8, 24);

        // Scale labels at threshold positions mapped to bar
        // 6V  → 0px,  9V  → (9-6)/6.6*216 = 98px,  10.5V → (10.5-6)/6.6*216 = 147px,  12.6V → 216px
        static constexpr char* v_scale_txt[] = { "6V", "9V", "10.5V", "12.6V" };
        static constexpr uint16_t v_scale_x[]   = { 8,   106,  155, 12 };
        for (uint8_t i = 0; i < 4; i++) {
            lv_obj_t* sl = lv_label_create(volt);
            lv_label_set_text(sl, v_scale_txt[i]);
            lv_obj_set_style_text_color(sl, lv_color_hex(color::GREY), 0);
            lv_obj_set_style_text_font(sl, &lv_font_montserrat_8, 0);
            lv_obj_set_pos(sl, v_scale_x[i], 44);
        }

        // Legend
        lv_obj_t* v_leg = lv_label_create(volt);
        lv_label_set_text(v_leg, "\xE2\x97\x8F Crit  \xE2\x97\x8F Warn  \xE2\x97\x8F Normal");
        lv_obj_set_style_text_color(v_leg, lv_color_hex(color::GREY), 0);
        lv_obj_set_style_text_font(v_leg, &lv_font_montserrat_8, 0);
        lv_obj_set_pos(v_leg, 8, 58);

        // Current panel
        // Bar maps 0A..25A across 216px
        // Zone boundaries: 20A = 20/25*216 = 173px,  25A = 216px
        lv_obj_t* cur = create_panel(screens[1], 4, 186, 232, 82);

        lv_obj_t* cur_hdr = lv_label_create(cur);
        lv_label_set_text(cur_hdr, "CURRENT");
        lv_obj_set_style_text_color(cur_hdr, lv_color_hex(color::GREY), 0);
        lv_obj_set_style_text_font(cur_hdr, &lv_font_montserrat_10, 0);
        lv_obj_set_pos(cur_hdr, 8, 5);

        label_s1_current_val = lv_label_create(cur);
        lv_label_set_text(label_s1_current_val, "0.00 A");
        lv_obj_set_style_text_color(label_s1_current_val, lv_color_hex(color::WHITE), 0);
        lv_obj_set_style_text_font(label_s1_current_val, &lv_font_montserrat_10, 0);
        lv_obj_align(label_s1_current_val, LV_ALIGN_TOP_RIGHT, -8, 4);

        // Zone background rects (drawn first so fill paints over them)
        // Green zone: 0..173px
        lv_obj_t* cz_g = lv_obj_create(cur);
        lv_obj_set_size(cz_g, 173, 18);
        lv_obj_set_pos(cz_g, 8, 24);
        lv_obj_set_style_bg_color(cz_g, lv_color_hex(0x1a3a1a), 0);
        lv_obj_set_style_border_width(cz_g, 0, 0);
        lv_obj_set_style_radius(cz_g, 2, 0);
        // Yellow zone: 173..200px
        lv_obj_t* cz_y = lv_obj_create(cur);
        lv_obj_set_size(cz_y, 27, 18);
        lv_obj_set_pos(cz_y, 181, 24);
        lv_obj_set_style_bg_color(cz_y, lv_color_hex(0x3a3a1a), 0);
        lv_obj_set_style_border_width(cz_y, 0, 0);
        // Red zone: 200..216px
        lv_obj_t* cz_r = lv_obj_create(cur);
        lv_obj_set_size(cz_r, 16, 18);
        lv_obj_set_pos(cz_r, 208, 24);
        lv_obj_set_style_bg_color(cz_r, lv_color_hex(0x3a1a1a), 0);
        lv_obj_set_style_border_width(cz_r, 0, 0);

        // Fill
        bar_s1_current_fill = lv_obj_create(cur);
        lv_obj_set_size(bar_s1_current_fill, 0, 18);
        lv_obj_set_pos(bar_s1_current_fill, 8, 24);
        lv_obj_set_style_bg_color(bar_s1_current_fill, lv_color_hex(color::GREEN), 0);
        lv_obj_set_style_border_width(bar_s1_current_fill, 0, 0);
        lv_obj_set_style_radius(bar_s1_current_fill, 2, 0);

        // Tick
        label_s1_current_tick = lv_label_create(cur);
        lv_label_set_text(label_s1_current_tick, "|");
        lv_obj_set_style_text_color(label_s1_current_tick, lv_color_hex(color::WHITE), 0);
        lv_obj_set_style_text_font(label_s1_current_tick, &lv_font_montserrat_10, 0);
        lv_obj_set_pos(label_s1_current_tick, 8, 24);

        // Scale: 0A at 0px, 20A at 173px, 25A at 216px
        static const char*    i_scale_txt[] = { "0A", "20A", "25A" };
        static const uint16_t i_scale_x[]   = { 8,   181,   212 };
        for (uint8_t i = 0; i < 3; i++) {
            lv_obj_t* sl = lv_label_create(cur);
            lv_label_set_text(sl, i_scale_txt[i]);
            lv_obj_set_style_text_color(sl, lv_color_hex(color::GREY), 0);
            lv_obj_set_style_text_font(sl, &lv_font_montserrat_8, 0);
            lv_obj_set_pos(sl, i_scale_x[i], 44);
        }

        // Legend
        lv_obj_t* i_leg = lv_label_create(cur);
        lv_label_set_text(i_leg, "\xE2\x97\x8F Normal  \xE2\x97\x8F High  \xE2\x97\x8F Crit");
        lv_obj_set_style_text_color(i_leg, lv_color_hex(color::GREY), 0);
        lv_obj_set_style_text_font(i_leg, &lv_font_montserrat_8, 0);
        lv_obj_set_pos(i_leg, 8, 58);

        // Bottom status panel
        lv_obj_t* bot = create_panel(screens[1], 4, 274, 232, 40);

        lv_obj_t* inv_hdr = lv_label_create(bot);
        lv_label_set_text(inv_hdr, "INVERTER");
        lv_obj_set_style_text_color(inv_hdr, lv_color_hex(color::GREY), 0);
        lv_obj_set_style_text_font(inv_hdr, &lv_font_montserrat_10, 0);
        lv_obj_set_pos(inv_hdr, 10, 4);

        label_s1_inv_status = lv_label_create(bot);
        lv_label_set_text(label_s1_inv_status, "IDLE");
        lv_obj_set_style_text_color(label_s1_inv_status, lv_color_hex(color::CYAN), 0);
        lv_obj_set_style_text_font(label_s1_inv_status, &lv_font_montserrat_12, 0);
        lv_obj_set_pos(label_s1_inv_status, 10, 20);

        lv_obj_t* batt_hdr = lv_label_create(bot);
        lv_label_set_text(batt_hdr, "BATTERY");
        lv_obj_set_style_text_color(batt_hdr, lv_color_hex(color::GREY), 0);
        lv_obj_set_style_text_font(batt_hdr, &lv_font_montserrat_10, 0);
        lv_obj_set_pos(batt_hdr, 120, 4);

        label_s1_batt_status = lv_label_create(bot);
        lv_label_set_text(label_s1_batt_status, "IDLE");
        lv_obj_set_style_text_color(label_s1_batt_status, lv_color_hex(color::YELLOW), 0);
        lv_obj_set_style_text_font(label_s1_batt_status, &lv_font_montserrat_12, 0);
        lv_obj_set_pos(label_s1_batt_status, 120, 20);
    }

    void create_screen_2() {

        screens[2] = lv_obj_create(nullptr);
        lv_obj_set_style_bg_color(screens[2], lv_color_hex(color::BLACK), 0);

        // Title
        lv_obj_t* title = lv_label_create(screens[2]);
        lv_label_set_text(title, "BATTERY ENVIRONMENT");
        lv_obj_set_style_text_color(title, lv_color_hex(color::CYAN), 0);
        lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 5);

        // Temperature panel
        // Bar maps 0°C to 60°C across 216px
        // Zone boundaries: 10°C = 10 / (60 * 216) = 36px,  45°C = 45 / (60 * 216) = 162px
        lv_obj_t* tp = create_panel(screens[2], 4, 24, 232, 106);

        lv_obj_t* tp_hdr = lv_label_create(tp);
        ASSERT(tp_hdr, "Failed to create tp_hdr");
        lv_label_set_text(tp_hdr, "TEMPERATURE");
        lv_obj_set_style_text_color(tp_hdr, lv_color_hex(color::GREY), 0);
        lv_obj_set_style_text_font(tp_hdr, &lv_font_montserrat_10, 0);
        lv_obj_set_pos(tp_hdr, 8, 5);

        label_s2_temp_val = lv_label_create(tp);
        lv_label_set_text(label_s2_temp_val, "0.0°C");
        lv_obj_set_style_text_color(label_s2_temp_val, lv_color_hex(color::WHITE), 0);
        lv_obj_set_style_text_font(label_s2_temp_val, &lv_font_montserrat_10, 0);
        lv_obj_align(label_s2_temp_val, LV_ALIGN_TOP_RIGHT, -8, 4);

        // Zone backgrounds (3 rects covering the full bar width)
        // Cold/blue: 0..36px
        lv_obj_t* tz_g = lv_obj_create(tp);
        lv_obj_set_size(tz_g, 36, 22);
        lv_obj_set_pos(tz_g, 8, 26);
        lv_obj_set_style_bg_color(tz_g, lv_color_hex(color::BLUE), 0);
        lv_obj_set_style_border_width(tz_g, 0, 0);
        lv_obj_set_style_radius(tz_g, 2, 0);
        // Normal/yellow: 36..162px
        lv_obj_t* tz_y = lv_obj_create(tp);
        lv_obj_set_size(tz_y, 126, 22);
        lv_obj_set_pos(tz_y, 44, 26);
        lv_obj_set_style_bg_color(tz_y, lv_color_hex(color::YELLOW), 0);
        lv_obj_set_style_border_width(tz_y, 0, 0);
        // Hot/red: 162..216px
        lv_obj_t* tz_r = lv_obj_create(tp);
        lv_obj_set_size(tz_r, 54, 22);
        lv_obj_set_pos(tz_r, 170, 26);
        lv_obj_set_style_bg_color(tz_r, lv_color_hex(color::RED), 0);
        lv_obj_set_style_border_width(tz_r, 0, 0);

        // Fill (width + color set in update)
        bar_s2_temp_fill = lv_obj_create(tp);
        lv_obj_set_size(bar_s2_temp_fill, 0, 22);
        lv_obj_set_pos(bar_s2_temp_fill, 8, 26);
        lv_obj_set_style_bg_color(bar_s2_temp_fill, lv_color_hex(color::GREEN), 0);
        lv_obj_set_style_border_width(bar_s2_temp_fill, 0, 0);
        lv_obj_set_style_radius(bar_s2_temp_fill, 2, 0);

        // Value overlay on bar (x centered on fill in update)
        label_s2_temp_overlay = lv_label_create(tp);
        lv_label_set_text(label_s2_temp_overlay, "0.0");
        lv_obj_set_style_text_color(label_s2_temp_overlay, lv_color_hex(color::WHITE), 0);
        lv_obj_set_style_text_font(label_s2_temp_overlay, &lv_font_montserrat_12, 0);
        lv_obj_set_pos(label_s2_temp_overlay, 8, 31);

        // Tick
        label_s2_temp_tick = lv_label_create(tp);
        lv_label_set_text(label_s2_temp_tick, "|");
        lv_obj_set_style_text_color(label_s2_temp_tick, lv_color_hex(color::WHITE), 0);
        lv_obj_set_style_text_font(label_s2_temp_tick, &lv_font_montserrat_10, 0);
        lv_obj_set_pos(label_s2_temp_tick, 8, 26);

        // Scale: 0°C = 0px, 10°C = 36px, 45°C = 162px, 60°C = 216px
        static constexpr char* ts_txt[] = { "0°C", "10°C", "45°C", "60°C" };
        static constexpr uint16_t ts_x[] = { 8, 44, 162, 212 };
        for (uint8_t i = 0; i < 4; i++) {
            lv_obj_t* sl = lv_label_create(tp);
            lv_label_set_text(sl, ts_txt[i]);
            lv_obj_set_style_text_color(sl, lv_color_hex(color::GREY), 0);
            lv_obj_set_style_text_font(sl, &lv_font_montserrat_8, 0);
            lv_obj_set_pos(sl, ts_x[i], 50);
        }

        // Legend
        lv_obj_t* tp_leg = lv_label_create(tp);
        lv_label_set_text(tp_leg, "\xE2\x97\x8F Cold  \xE2\x97\x8F Normal  \xE2\x97\x8F Hot");
        lv_obj_set_style_text_color(tp_leg, lv_color_hex(color::GREY), 0);
        lv_obj_set_style_text_font(tp_leg, &lv_font_montserrat_8, 0);
        lv_obj_set_pos(tp_leg, 8, 72);

        // Humidity panel
        // Bar maps 0%..100% across 216px
        // Zone boundaries: 20% = 43px, 70% = 151px
        lv_obj_t* hm = create_panel(screens[2], 4, 136, 232, 106);

        lv_obj_t* hm_hdr = lv_label_create(hm);
        lv_label_set_text(hm_hdr, "HUMIDITY");
        lv_obj_set_style_text_color(hm_hdr, lv_color_hex(color::GREY), 0);
        lv_obj_set_style_text_font(hm_hdr, &lv_font_montserrat_10, 0);
        lv_obj_set_pos(hm_hdr, 8, 5);

        label_s2_hmdt_val = lv_label_create(hm);
        lv_label_set_text(label_s2_hmdt_val, "0.0%");
        lv_obj_set_style_text_color(label_s2_hmdt_val, lv_color_hex(color::YELLOW), 0);
        lv_obj_set_style_text_font(label_s2_hmdt_val, &lv_font_montserrat_10, 0);
        lv_obj_align(label_s2_hmdt_val, LV_ALIGN_TOP_RIGHT, -8, 4);

        // Zone backgrounds
        // Dry/green: 0..43px
        lv_obj_t* hz_g = lv_obj_create(hm);
        lv_obj_set_size(hz_g, 43, 22);
        lv_obj_set_pos(hz_g, 8, 26);
        lv_obj_set_style_bg_color(hz_g, lv_color_hex(0x1a3a1a), 0);
        lv_obj_set_style_border_width(hz_g, 0, 0);
        lv_obj_set_style_radius(hz_g, 2, 0);
        // Normal/yellow: 43..151px
        lv_obj_t* hz_y = lv_obj_create(hm);
        lv_obj_set_size(hz_y, 108, 22);
        lv_obj_set_pos(hz_y, 51, 26);
        lv_obj_set_style_bg_color(hz_y, lv_color_hex(0x3a3a1a), 0);
        lv_obj_set_style_border_width(hz_y, 0, 0);
        // Humid/red: 151..216px
        lv_obj_t* hz_r = lv_obj_create(hm);
        lv_obj_set_size(hz_r, 65, 22);
        lv_obj_set_pos(hz_r, 159, 26);
        lv_obj_set_style_bg_color(hz_r, lv_color_hex(0x3a1a1a), 0);
        lv_obj_set_style_border_width(hz_r, 0, 0);

        // Fill
        bar_s2_hmdt_fill = lv_obj_create(hm);
        lv_obj_set_size(bar_s2_hmdt_fill, 0, 22);
        lv_obj_set_pos(bar_s2_hmdt_fill, 8, 26);
        lv_obj_set_style_bg_color(bar_s2_hmdt_fill, lv_color_hex(color::GREEN), 0);
        lv_obj_set_style_border_width(bar_s2_hmdt_fill, 0, 0);
        lv_obj_set_style_radius(bar_s2_hmdt_fill, 2, 0);

        // Overlay
        label_s2_hmdt_overlay = lv_label_create(hm);
        lv_label_set_text(label_s2_hmdt_overlay, "0");
        lv_obj_set_style_text_color(label_s2_hmdt_overlay, lv_color_hex(color::WHITE), 0);
        lv_obj_set_style_text_font(label_s2_hmdt_overlay, &lv_font_montserrat_12, 0);
        lv_obj_set_pos(label_s2_hmdt_overlay, 8, 31);

        // Tick
        label_s2_hmdt_tick = lv_label_create(hm);
        lv_label_set_text(label_s2_hmdt_tick, "|");
        lv_obj_set_style_text_color(label_s2_hmdt_tick, lv_color_hex(color::WHITE), 0);
        lv_obj_set_style_text_font(label_s2_hmdt_tick, &lv_font_montserrat_10, 0);
        lv_obj_set_pos(label_s2_hmdt_tick, 8, 26);

        // Scale: 0%=0px, 20%=43px, 70%=151px, 100%=216px
        static const char*    hs_txt[] = { "0%", "20%", "70%", "100%" };
        static const uint16_t hs_x[]   = { 8,    51,    151,   208 };
        for (uint8_t i = 0; i < 4; i++) {
            lv_obj_t* sl = lv_label_create(hm);
            lv_label_set_text(sl, hs_txt[i]);
            lv_obj_set_style_text_color(sl, lv_color_hex(color::GREY), 0);
            lv_obj_set_style_text_font(sl, &lv_font_montserrat_8, 0);
            lv_obj_set_pos(sl, hs_x[i], 50);
        }

        // Legend
        lv_obj_t* hm_leg = lv_label_create(hm);
        lv_label_set_text(hm_leg, "\xE2\x97\x8F Dry  \xE2\x97\x8F Normal  \xE2\x97\x8F Humid");
        lv_obj_set_style_text_color(hm_leg, lv_color_hex(color::GREY), 0);
        lv_obj_set_style_text_font(hm_leg, &lv_font_montserrat_8, 0);
        lv_obj_set_pos(hm_leg, 8, 72);

        // Bottom row
        lv_obj_t* bot = create_panel(screens[2], 4, 248, 232, 52);

        lv_obj_t* rt_hdr = lv_label_create(bot);
        lv_label_set_text(rt_hdr, "RUNTIME");
        lv_obj_set_style_text_color(rt_hdr, lv_color_hex(color::GREY), 0);
        lv_obj_set_style_text_font(rt_hdr, &lv_font_montserrat_10, 0);
        lv_obj_set_pos(rt_hdr, 10, 5);

        label_s2_runtime = lv_label_create(bot);
        lv_label_set_text(label_s2_runtime, "00:00:00");
        lv_obj_set_style_text_color(label_s2_runtime, lv_color_hex(color::CYAN), 0);
        lv_obj_set_style_text_font(label_s2_runtime, &lv_font_montserrat_12, 0);
        lv_obj_set_pos(label_s2_runtime, 10, 22);

        lv_obj_t* inv_hdr = lv_label_create(bot);
        lv_label_set_text(inv_hdr, "INVERTER");
        lv_obj_set_style_text_color(inv_hdr, lv_color_hex(color::GREY), 0);
        lv_obj_set_style_text_font(inv_hdr, &lv_font_montserrat_10, 0);
        lv_obj_set_pos(inv_hdr, 120, 5);

        label_s2_inv_status = lv_label_create(bot);
        lv_label_set_text(label_s2_inv_status, "IDLE");
        lv_obj_set_style_text_color(label_s2_inv_status, lv_color_hex(color::CYAN), 0);
        lv_obj_set_style_text_font(label_s2_inv_status, &lv_font_montserrat_12, 0);
        lv_obj_set_pos(label_s2_inv_status, 120, 22);
    }
 
    void create_screen_3() {

        screens[3] = lv_obj_create(nullptr);
        lv_obj_set_style_bg_color(screens[3], lv_color_hex(color::BLACK), 0);

        // Title
        lv_obj_t* title = lv_label_create(screens[3]);
        lv_label_set_text(title, "OVERVIEW");
        lv_obj_set_style_text_color(title, lv_color_hex(color::CYAN), 0);
        lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 5);

        // Electrical panel
        lv_obj_t* elec = create_panel(screens[3], 4, 24, 232, 120);

        lv_obj_t* elec_hdr = lv_label_create(elec);
        lv_label_set_text(elec_hdr, "ELECTRICAL");
        lv_obj_set_style_text_color(elec_hdr, lv_color_hex(color::GREY), 0);
        lv_obj_set_style_text_font(elec_hdr, &lv_font_montserrat_10, 0);
        lv_obj_align(elec_hdr, LV_ALIGN_TOP_MID, 0, 5);

        // 4 rows: dot | label | value (right-aligned)
        static const char*    elec_labels[4] = { "Voltage", "Current", "Power", "State of Charge" };
        static const uint16_t elec_row_y[4]  = { 28, 50, 72, 94 };

        lv_obj_t* edots[4]{};
        lv_obj_t* evals[4]{};

        for (uint8_t i = 0; i < 4; i++) {
            edots[i] = create_dot(elec, 8, elec_row_y[i] + 4, lv_color_hex(color::GREEN));

            lv_obj_t* lbl = lv_label_create(elec);
            lv_label_set_text(lbl, elec_labels[i]);
            lv_obj_set_style_text_color(lbl, lv_color_hex(color::WHITE), 0);
            lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
            lv_obj_set_pos(lbl, 20, elec_row_y[i]);

            evals[i] = lv_label_create(elec);
            lv_label_set_text(evals[i], "—");
            lv_obj_set_style_text_color(evals[i], lv_color_hex(color::WHITE), 0);
            lv_obj_set_style_text_font(evals[i], &lv_font_montserrat_12, 0);
            lv_obj_align(evals[i], LV_ALIGN_TOP_RIGHT, -8, elec_row_y[i]);
        }

        dot_s3_voltage       = edots[0];   label_s3_voltage_val = evals[0];
        dot_s3_current       = edots[1];   label_s3_current_val = evals[1];
        dot_s3_power         = edots[2];   label_s3_power_val   = evals[2];
        dot_s3_soc           = edots[3];   label_s3_soc_val     = evals[3];

        // Environment panel
        lv_obj_t* env = create_panel(screens[3], 4, 150, 232, 76);

        lv_obj_t* env_hdr = lv_label_create(env);
        lv_label_set_text(env_hdr, "ENVIRONMENT");
        lv_obj_set_style_text_color(env_hdr, lv_color_hex(color::GREY), 0);
        lv_obj_set_style_text_font(env_hdr, &lv_font_montserrat_10, 0);
        lv_obj_align(env_hdr, LV_ALIGN_TOP_MID, 0, 5);

        static const char*    env_labels[2] = { "Temperature", "Humidity" };
        static const uint16_t env_row_y[2]  = { 26, 48 };

        lv_obj_t* ndots[2]{};
        lv_obj_t* nvals[2]{};

        for (uint8_t i = 0; i < 2; i++) {
            ndots[i] = create_dot(env, 8, env_row_y[i] + 4, lv_color_hex(color::GREEN));

            lv_obj_t* lbl = lv_label_create(env);
            lv_label_set_text(lbl, env_labels[i]);
            lv_obj_set_style_text_color(lbl, lv_color_hex(color::WHITE), 0);
            lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
            lv_obj_set_pos(lbl, 20, env_row_y[i]);

            nvals[i] = lv_label_create(env);
            lv_label_set_text(nvals[i], "—");
            lv_obj_set_style_text_color(nvals[i], lv_color_hex(color::WHITE), 0);
            lv_obj_set_style_text_font(nvals[i], &lv_font_montserrat_12, 0);
            lv_obj_align(nvals[i], LV_ALIGN_TOP_RIGHT, -8, env_row_y[i]);
        }

        dot_s3_temp = ndots[0];
        label_s3_temp_val = nvals[0];
        dot_s3_hmdt = ndots[1];
        label_s3_hmdt_val = nvals[1];

        // Bottom row
        lv_obj_t* bot = create_panel(screens[3], 4, 232, 232, 56);

        static const char*    bot_hdrs[3] = { "BATTERY", "INVERTER", "RUNTIME" };
        static const uint16_t bot_x[3]    = { 10, 88, 166 };
        lv_obj_t* bvals[3]{};

        for (uint8_t i = 0; i < 3; i++) {
            lv_obj_t* hd = lv_label_create(bot);
            lv_label_set_text(hd, bot_hdrs[i]);
            lv_obj_set_style_text_color(hd, lv_color_hex(color::GREY), 0);
            lv_obj_set_style_text_font(hd, &lv_font_montserrat_10, 0);
            lv_obj_set_pos(hd, bot_x[i], 5);

            bvals[i] = lv_label_create(bot);
            lv_label_set_text(bvals[i], "—");
            lv_obj_set_style_text_font(bvals[i], &lv_font_montserrat_12, 0);
            lv_obj_set_pos(bvals[i], bot_x[i], 24);
        }

        label_s3_batt_status = bvals[0];
        label_s3_inv_status  = bvals[1];
        label_s3_runtime     = bvals[2];
    }

    void create_screen_4(const graph_samples_t& samples) {

        screens[4] = lv_obj_create(nullptr);
        lv_obj_set_style_bg_color(screens[4], lv_color_hex(color::BLACK), 0);

        lv_obj_t* title = lv_label_create(screens[4]);
        lv_label_set_text(title, "TEMPERATURE & HUMIDITY");
        lv_obj_set_style_text_color(title, lv_color_hex(color::CYAN), 0);
        lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 4);

        lv_obj_t* leg_temp = lv_label_create(screens[4]);
        lv_label_set_text(leg_temp, "── T");
        lv_obj_set_style_text_color(leg_temp, lv_color_hex(color::RED), 0);
        lv_obj_align(leg_temp, LV_ALIGN_TOP_LEFT, 30, 22);

        lv_obj_t* leg_hmdt = lv_label_create(screens[4]);
        lv_label_set_text(leg_hmdt, "── H");
        lv_obj_set_style_text_color(leg_hmdt, lv_color_hex(color::CYAN), 0);
        lv_obj_align(leg_hmdt, LV_ALIGN_TOP_LEFT, 130, 22);

        chart_env = lv_chart_create(screens[4]);
        lv_obj_set_size(chart_env, 220, 250);
        lv_obj_align(chart_env, LV_ALIGN_TOP_MID, 0, 42);
        lv_obj_set_style_bg_color(chart_env, lv_color_hex(color::BLACK), 0);
        lv_obj_set_style_border_color(chart_env, lv_color_hex(color::GREY), 0);
        lv_obj_set_style_border_width(chart_env, 1, 0);
        lv_obj_set_style_line_color(chart_env, lv_color_hex(color::WHITE), 0);

        lv_chart_set_type(chart_env, LV_CHART_TYPE_LINE);
        lv_chart_set_point_count(chart_env, config::GRAPH_SAMPLES);
        lv_chart_set_axis_range(chart_env, LV_CHART_AXIS_PRIMARY_Y, 0, config::GRAPH_SAMPLES);
        lv_chart_set_div_line_count(chart_env, 20, 5);

        lv_obj_t* y_min = lv_label_create(screens[4]);
        lv_label_set_text(y_min, "0");
        lv_obj_set_style_text_color(y_min, lv_color_hex(color::GREY), 0);
        lv_obj_set_style_text_font(y_min, &lv_font_montserrat_10, 0);
        lv_obj_align_to(y_min, chart_env, LV_ALIGN_OUT_LEFT_BOTTOM, -2, 0);

        lv_obj_t* y_mid = lv_label_create(screens[4]);
        lv_label_set_text(y_mid, "50");
        lv_obj_set_style_text_color(y_mid, lv_color_hex(color::GREY), 0);
        lv_obj_set_style_text_font(y_mid, &lv_font_montserrat_10, 0);
        lv_obj_align_to(y_mid, chart_env, LV_ALIGN_OUT_LEFT_MID, -2, 0);

        lv_obj_t* y_max = lv_label_create(screens[4]);
        lv_label_set_text(y_max, "100");
        lv_obj_set_style_text_color(y_max, lv_color_hex(color::GREY), 0);
        lv_obj_set_style_text_font(y_max, &lv_font_montserrat_10, 0);
        lv_obj_align_to(y_max, chart_env, LV_ALIGN_OUT_LEFT_TOP, -2, 0);

        series_temp = lv_chart_add_series(chart_env, lv_color_hex(color::RED), LV_CHART_AXIS_PRIMARY_Y);
        series_hmdt = lv_chart_add_series(chart_env, lv_color_hex(color::CYAN), LV_CHART_AXIS_PRIMARY_Y);

        auto& [temp, hmdt] = samples;
        int32_t tempi32[config::GRAPH_SAMPLES]{};
        int32_t hmdti32[config::GRAPH_SAMPLES]{};

        for (size_t i = 0; i < config::GRAPH_SAMPLES; i++) {
            tempi32[i] = static_cast<int32_t>(temp[i]);
        }

        for (size_t i = 0; i < config::GRAPH_SAMPLES; i++) {
            hmdti32[i] = static_cast<int32_t>(hmdt[i]);
        }
        
        lv_chart_set_series_values(chart_env, series_temp, tempi32, temp.size());
        lv_chart_set_series_values(chart_env, series_hmdt, hmdti32, hmdt.size());
    }

    void create_screen_5(const graph_samples_t& samples) {

        screens[5] = lv_obj_create(nullptr);
        lv_obj_set_style_bg_color(screens[5], lv_color_hex(color::BLACK), 0);

        lv_obj_t* title = lv_label_create(screens[5]);
        lv_label_set_text(title, "VOLTAGE & CURRENT");
        lv_obj_set_style_text_color(title, lv_color_hex(color::CYAN), 0);
        lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 4);

        lv_obj_t* leg_v = lv_label_create(screens[5]);
        lv_label_set_text(leg_v, "── V");
        lv_obj_set_style_text_color(leg_v, lv_color_hex(color::YELLOW), 0);
        lv_obj_align(leg_v, LV_ALIGN_TOP_LEFT, 30, 22);

        lv_obj_t* leg_i = lv_label_create(screens[5]);
        lv_label_set_text(leg_i, "── I");
        lv_obj_set_style_text_color(leg_i, lv_color_hex(color::GREEN), 0);
        lv_obj_align(leg_i, LV_ALIGN_TOP_LEFT, 130, 22);

        chart_power = lv_chart_create(screens[5]);
        lv_obj_set_size(chart_power, 220, 250);
        lv_obj_align(chart_power, LV_ALIGN_TOP_MID, 0, 42);
        lv_obj_set_style_bg_color(chart_power, lv_color_hex(color::BLACK), 0);
        lv_obj_set_style_border_color(chart_power, lv_color_hex(color::GREY), 0);
        lv_obj_set_style_border_width(chart_power, 1, 0);
        lv_obj_set_style_line_color(chart_power, lv_color_hex(color::WHITE), 0);

        lv_chart_set_type(chart_power, LV_CHART_TYPE_LINE);
        lv_chart_set_point_count(chart_power, config::GRAPH_SAMPLES);
        lv_chart_set_axis_range(chart_power, LV_CHART_AXIS_PRIMARY_Y, 0, config::GRAPH_SAMPLES);
        lv_chart_set_div_line_count(chart_power, 6, 3);

        lv_obj_t* y_min = lv_label_create(screens[5]);
        lv_label_set_text(y_min, "0");
        lv_obj_set_style_text_color(y_min, lv_color_hex(color::GREY), 0);
        lv_obj_set_style_text_font(y_min, &lv_font_montserrat_10, 0);
        lv_obj_align_to(y_min, chart_power, LV_ALIGN_OUT_LEFT_BOTTOM, -2, 0);

        lv_obj_t* y_mid = lv_label_create(screens[5]);
        lv_label_set_text(y_mid, "15");
        lv_obj_set_style_text_color(y_mid, lv_color_hex(color::GREY), 0);
        lv_obj_set_style_text_font(y_mid, &lv_font_montserrat_10, 0);
        lv_obj_align_to(y_mid, chart_power, LV_ALIGN_OUT_LEFT_MID, -2, 0);

        lv_obj_t* y_max = lv_label_create(screens[5]);
        lv_label_set_text(y_max, "30");
        lv_obj_set_style_text_color(y_max, lv_color_hex(color::GREY), 0);
        lv_obj_set_style_text_font(y_max, &lv_font_montserrat_10, 0);
        lv_obj_align_to(y_max, chart_power, LV_ALIGN_OUT_LEFT_TOP, -2, 0);

        series_voltage = lv_chart_add_series(chart_power, lv_color_hex(color::YELLOW), LV_CHART_AXIS_PRIMARY_Y);
        series_current = lv_chart_add_series(chart_power, lv_color_hex(color::GREEN), LV_CHART_AXIS_PRIMARY_Y);

        auto& [voltage, current] = samples;
        int32_t volti32[config::GRAPH_SAMPLES]{};
        int32_t curri32[config::GRAPH_SAMPLES]{};

        for (size_t i = 0; i < config::GRAPH_SAMPLES; i++) {
            volti32[i] = static_cast<int32_t>(voltage[i]);
        }

        for (size_t i = 0; i < config::GRAPH_SAMPLES; i++) {
            curri32[i] = static_cast<int32_t>(current[i]);
        }
        
        lv_chart_set_series_values(chart_env, series_voltage, volti32, voltage.size());
        lv_chart_set_series_values(chart_env, series_current, curri32, current.size());
    }

    // Updating screens
    void update_screen_0(const sys::data_t& data) {

        char buf[64]{};

        // BATT card
        if (data.battery_percent > 20.0f) {
            style_badge(label_s0_batt_badge, "OK", lv_color_hex(color::GREEN), lv_color_hex(color::BLACK));
            lv_obj_set_style_text_color(label_s0_batt_value, lv_color_hex(color::GREEN), 0);
        } else {
            style_badge(label_s0_batt_badge, "WARN", lv_color_hex(color::YELLOW), lv_color_hex(color::BLACK));
            lv_obj_set_style_text_color(label_s0_batt_value, lv_color_hex(color::YELLOW), 0);
        }
        snprintf(buf, sizeof(buf) - 1, "%.0f%%", data.battery_percent);
        lv_label_set_text(label_s0_batt_value, buf);

        // INV card
        if (data.inv_status == sys::inv_status_t::ACTIVE) {
            style_badge(label_s0_inv_badge, "ACTIVE", lv_color_hex(0x0066CC), lv_color_hex(color::WHITE));
            lv_label_set_text(label_s0_inv_value, "ON");
            lv_obj_set_style_text_color(label_s0_inv_value, lv_color_hex(color::CYAN), 0);
        } else {
            style_badge(label_s0_inv_badge, "IDLE", lv_color_hex(color::GREY), lv_color_hex(color::WHITE));
            lv_label_set_text(label_s0_inv_value, "OFF");
            lv_obj_set_style_text_color(label_s0_inv_value, lv_color_hex(color::GREY), 0);
        }

        // TEMP card
        if (data.inv_temp >= 45.0f || data.inv_temp <= 10.0f) {
            style_badge(label_s0_temp_badge, "WARN", lv_color_hex(color::YELLOW), lv_color_hex(color::BLACK));
            lv_obj_set_style_text_color(label_s0_temp_value, lv_color_hex(color::YELLOW), 0);
        } else {
            style_badge(label_s0_temp_badge, "OK", lv_color_hex(color::GREEN), lv_color_hex(color::BLACK));
            lv_obj_set_style_text_color(label_s0_temp_value, lv_color_hex(color::GREEN), 0);
        }
        snprintf(buf, sizeof(buf) - 1, "%.0f°C", data.inv_temp);
        lv_label_set_text(label_s0_temp_value, buf);

        // HMDT card
        if (data.inv_hmdt >= 70.0f || data.inv_hmdt <= 20.0f) {
            style_badge(label_s0_hmdt_badge, "WARN", lv_color_hex(color::YELLOW), lv_color_hex(color::BLACK));
            lv_obj_set_style_text_color(label_s0_hmdt_value, lv_color_hex(color::YELLOW), 0);
        } else {
            style_badge(label_s0_hmdt_badge, "OK", lv_color_hex(color::GREEN), lv_color_hex(color::BLACK));
            lv_obj_set_style_text_color(label_s0_hmdt_value, lv_color_hex(color::GREEN), 0);
        }
        snprintf(buf, sizeof(buf) - 1, "%.0f%%", data.inv_hmdt);
        lv_label_set_text(label_s0_hmdt_value, buf);

        // Live values
        snprintf(buf, sizeof(buf) - 1, "%.1f", data.battery_voltage);
        lv_label_set_text(label_s0_voltage, buf);

        snprintf(buf, sizeof(buf) - 1, "%.1f", data.load_current_drawn);
        lv_label_set_text(label_s0_current, buf);

        snprintf(buf, sizeof(buf) - 1, "%.1f", data.power_drawn);
        lv_label_set_text(label_s0_power, buf);

        // Bottom row
        uint8_t hours   = data.runtime_left_s / 3600;
        uint8_t minutes = (data.runtime_left_s % 3600) / 60;
        uint8_t seconds = data.runtime_left_s % 60;

        snprintf(buf, sizeof(buf) - 1, "%02u:%02u:%02u", hours, minutes, seconds);
        lv_label_set_text(label_s0_runtime, buf);
        lv_obj_set_style_text_color(label_s0_runtime, lv_color_hex(color::CYAN), 0);

        snprintf(buf, sizeof(buf) - 1, "%s", sys::batt_status_to_string(data.batt_status));
        lv_label_set_text(label_s0_batt_status, buf);
        lv_obj_set_style_text_color(label_s0_batt_status, lv_color_hex(color::YELLOW), 0);

        if (data.inv_status == sys::inv_status_t::ACTIVE) {
            lv_label_set_text(label_s0_inv_status, "ACTIVE");
            lv_obj_set_style_text_color(label_s0_inv_status, lv_color_hex(color::CYAN), 0);
        } else {
            lv_label_set_text(label_s0_inv_status, "IDLE");
            lv_obj_set_style_text_color(label_s0_inv_status, lv_color_hex(color::GREY), 0);
        }
    }

    void update_screen_1(const sys::data_t& data) {

        char buf[64]{};

        // Hero
        snprintf(buf, sizeof(buf) - 1, "%.1fW", data.power_drawn);
        lv_label_set_text(label_s1_power_hero, buf);

        // Voltage bar: 6.0V..12.6V → 0..216px
        snprintf(buf, sizeof(buf) - 1, "%.2f V", data.battery_voltage);
        lv_label_set_text(label_s1_voltage_val, buf);

        float v = data.battery_voltage;
        if (v < 6.0f)  v = 6.0f;
        if (v > 12.6f) v = 12.6f;
        uint16_t v_px = (uint16_t)(((v - 6.0f) / 6.6f) * 216.0f);
        lv_obj_set_width(bar_s1_voltage_fill, v_px);
        lv_obj_set_x(label_s1_voltage_tick, 8 + (v_px > 3 ? v_px - 3 : 0));

        // Current bar: 0A..25A → 0..216px
        snprintf(buf, sizeof(buf) - 1, "%.2f A", data.load_current_drawn);
        lv_label_set_text(label_s1_current_val, buf);

        float ic = data.load_current_drawn;
        if (ic < 0.0f)  ic = 0.0f;
        if (ic > 25.0f) ic = 25.0f;
        uint16_t i_px = (uint16_t)((ic / 25.0f) * 216.0f);
        lv_obj_set_width(bar_s1_current_fill, i_px);

        // Fill color tracks which zone the value is in
        if (ic >= 25.0f) {
            lv_obj_set_style_bg_color(bar_s1_current_fill, lv_color_hex(color::RED), 0);
        } else if (ic >= 20.0f) {
            lv_obj_set_style_bg_color(bar_s1_current_fill, lv_color_hex(color::YELLOW), 0);
        } else {
            lv_obj_set_style_bg_color(bar_s1_current_fill, lv_color_hex(color::GREEN), 0);
        }
        lv_obj_set_x(label_s1_current_tick, 8 + (i_px > 3 ? i_px - 3 : 0));

        // Status
        snprintf(buf, sizeof(buf) - 1, "%s", sys::inv_status_to_string(data.inv_status));
        lv_label_set_text(label_s1_inv_status, buf);

        snprintf(buf, sizeof(buf) - 1, "%s", sys::batt_status_to_string(data.batt_status));
        lv_label_set_text(label_s1_batt_status, buf);
    }

    void update_screen_2(const sys::data_t& data) {

        char buf[64]{};

        // Temperature: 0°C..60°C → 0..216px
        snprintf(buf, sizeof(buf) - 1, "%.3f°C", data.inv_temp);
        lv_label_set_text(label_s2_temp_val, buf);

        float t = data.inv_temp;
        if (t < 0.0f)  t = 0.0f;
        if (t > 60.0f) t = 60.0f;
        uint16_t t_px = (uint16_t)((t / 60.0f) * 216.0f);
        lv_obj_set_width(bar_s2_temp_fill, t_px);

        if (t >= 45.0f) {
            lv_obj_set_style_bg_color(bar_s2_temp_fill, lv_color_hex(color::RED), 0);
        } else if (t >= 10.0f) {
            lv_obj_set_style_bg_color(bar_s2_temp_fill, lv_color_hex(color::GREEN), 0);
        } else {
            lv_obj_set_style_bg_color(bar_s2_temp_fill, lv_color_hex(0x4488FF), 0);
        }

        // Overlay centered on filled portion
        snprintf(buf, sizeof(buf) - 1, "%.1f", data.inv_temp);
        lv_label_set_text(label_s2_temp_overlay, buf);
        lv_obj_set_x(label_s2_temp_overlay, (t_px > 30) ? (8 + t_px / 2 - 15) : 8);

        lv_obj_set_x(label_s2_temp_tick, 8 + (t_px > 3 ? t_px - 3 : 0));

        // Humidity: 0%..100% → 0..216px
        snprintf(buf, sizeof(buf) - 1, "%.1f%%", data.inv_hmdt);
        lv_label_set_text(label_s2_hmdt_val, buf);

        float h = data.inv_hmdt;
        if (h < 0.0f)   h = 0.0f;
        if (h > 100.0f) h = 100.0f;
        uint16_t h_px = (uint16_t)((h / 100.0f) * 216.0f);
        lv_obj_set_width(bar_s2_hmdt_fill, h_px);

        if (h >= 70.0f) {
            lv_obj_set_style_bg_color(bar_s2_hmdt_fill, lv_color_hex(color::YELLOW), 0);
        } else if (h >= 20.0f) {
            lv_obj_set_style_bg_color(bar_s2_hmdt_fill, lv_color_hex(color::GREEN), 0);
        } else {
            lv_obj_set_style_bg_color(bar_s2_hmdt_fill, lv_color_hex(0x4488FF), 0);
        }

        snprintf(buf, sizeof(buf) - 1, "%.0f", data.inv_hmdt);
        lv_label_set_text(label_s2_hmdt_overlay, buf);
        lv_obj_set_x(label_s2_hmdt_overlay, (h_px > 24) ? (8 + h_px / 2 - 12) : 8);

        lv_obj_set_x(label_s2_hmdt_tick, 8 + (h_px > 3 ? h_px - 3 : 0));

        // Bottom row
        uint8_t hours   = data.runtime_left_s / 3600;
        uint8_t minutes = (data.runtime_left_s % 3600) / 60;
        uint8_t seconds = data.runtime_left_s % 60;
        snprintf(buf, sizeof(buf) - 1, "%02u:%02u:%02u", hours, minutes, seconds);
        lv_label_set_text(label_s2_runtime, buf);

        if (data.inv_status == sys::inv_status_t::ACTIVE) {
            lv_label_set_text(label_s2_inv_status, "ACTIVE");
            lv_obj_set_style_text_color(label_s2_inv_status, lv_color_hex(color::CYAN), 0);
        } else {
            lv_label_set_text(label_s2_inv_status, "IDLE");
            lv_obj_set_style_text_color(label_s2_inv_status, lv_color_hex(color::GREY), 0);
        }
    }
   
    void update_screen_3(const sys::data_t& data) {

        char buf[64]{};

        // Electrical rows
        // Dots go yellow and values go yellow when in warning range

        bool v_warn = (data.battery_voltage <= 10.5f || data.battery_voltage > 12.6f);
        lv_obj_set_style_bg_color(dot_s3_voltage, v_warn ? lv_color_hex(color::YELLOW) : lv_color_hex(color::GREEN), 0);
        snprintf(buf, sizeof(buf) - 1, "%.2f V", data.battery_voltage);
        lv_label_set_text(label_s3_voltage_val, buf);
        lv_obj_set_style_text_color(label_s3_voltage_val, v_warn ? lv_color_hex(color::YELLOW) : lv_color_hex(color::WHITE), 0);

        bool i_warn = (data.load_current_drawn >= 20.0f);
        lv_obj_set_style_bg_color(dot_s3_current, i_warn ? lv_color_hex(color::YELLOW) : lv_color_hex(color::GREEN), 0);
        snprintf(buf, sizeof(buf) - 1, "%.2f A", data.load_current_drawn);
        lv_label_set_text(label_s3_current_val, buf);
        lv_obj_set_style_text_color(label_s3_current_val, i_warn ? lv_color_hex(color::YELLOW) : lv_color_hex(color::WHITE), 0);

        bool p_warn = (data.power_drawn >= 200.0f);
        lv_obj_set_style_bg_color(dot_s3_power, p_warn ? lv_color_hex(color::YELLOW) : lv_color_hex(color::GREEN), 0);
        snprintf(buf, sizeof(buf) - 1, "%.2f W", data.power_drawn);
        lv_label_set_text(label_s3_power_val, buf);
        lv_obj_set_style_text_color(label_s3_power_val, p_warn ? lv_color_hex(color::YELLOW) : lv_color_hex(color::WHITE), 0);

        // SoC: yellow dot + yellow value when ≤50% (matches screenshot)
        bool soc_warn = (data.battery_percent <= 50.0f);
        lv_obj_set_style_bg_color(dot_s3_soc, soc_warn ? lv_color_hex(color::YELLOW) : lv_color_hex(color::GREEN), 0);
        snprintf(buf, sizeof(buf) - 1, "%.1f %%", data.battery_percent);
        lv_label_set_text(label_s3_soc_val, buf);
        lv_obj_set_style_text_color(label_s3_soc_val, soc_warn ? lv_color_hex(color::YELLOW) : lv_color_hex(color::WHITE), 0);

        // Environment rows
        bool t_warn = (data.inv_temp >= 45.0f || data.inv_temp <= 10.0f);
        lv_obj_set_style_bg_color(dot_s3_temp, t_warn ? lv_color_hex(color::YELLOW) : lv_color_hex(color::GREEN), 0);
        snprintf(buf, sizeof(buf) - 1, "%.1f °C", data.inv_temp);
        lv_label_set_text(label_s3_temp_val, buf);
        lv_obj_set_style_text_color(label_s3_temp_val, t_warn ? lv_color_hex(color::YELLOW) : lv_color_hex(color::WHITE), 0);

        bool h_warn = (data.inv_hmdt >= 70.0f || data.inv_hmdt <= 20.0f);
        lv_obj_set_style_bg_color(dot_s3_hmdt, h_warn ? lv_color_hex(color::YELLOW) : lv_color_hex(color::GREEN), 0);
        snprintf(buf, sizeof(buf) - 1, "%.1f %%", data.inv_hmdt);
        lv_label_set_text(label_s3_hmdt_val, buf);
        lv_obj_set_style_text_color(label_s3_hmdt_val, h_warn ? lv_color_hex(color::YELLOW) : lv_color_hex(color::WHITE), 0);

        // Bottom row
        snprintf(buf, sizeof(buf) - 1, "%s", sys::batt_status_to_string(data.batt_status));
        lv_label_set_text(label_s3_batt_status, buf);
        lv_obj_set_style_text_color(label_s3_batt_status, lv_color_hex(color::YELLOW), 0);

        if (data.inv_status == sys::inv_status_t::ACTIVE) {
            lv_label_set_text(label_s3_inv_status, "ACTIVE");
            lv_obj_set_style_text_color(label_s3_inv_status, lv_color_hex(color::CYAN), 0);
        } else {
            lv_label_set_text(label_s3_inv_status, "IDLE");
            lv_obj_set_style_text_color(label_s3_inv_status, lv_color_hex(color::GREY), 0);
        }

        // Runtime: HH:MM only (screenshot shows "04:32", no seconds)
        uint8_t hours   = data.runtime_left_s / 3600;
        uint8_t minutes = (data.runtime_left_s % 3600) / 60;
        snprintf(buf, sizeof(buf) - 1, "%02u:%02u", hours, minutes);
        lv_label_set_text(label_s3_runtime, buf);
        lv_obj_set_style_text_color(label_s3_runtime, lv_color_hex(color::CYAN), 0);
    }

    void update_screen_4(const sys::data_t& data) {
        lv_chart_set_next_value(chart_env, series_temp, static_cast<int32_t>(data.inv_temp));
        lv_chart_set_next_value(chart_env, series_hmdt, static_cast<int32_t>(data.inv_hmdt));
        lv_chart_refresh(chart_env);
    }

    void update_screen_5(const sys::data_t& data) {
        lv_chart_set_next_value(chart_power, series_voltage, static_cast<int32_t>(data.battery_voltage));
        lv_chart_set_next_value(chart_power, series_current, static_cast<int32_t>(data.load_current_drawn));
        lv_chart_refresh(chart_power);
    }

    // Static helpers
    static lv_obj_t* create_panel(lv_obj_t* parent, uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
        lv_obj_t* p = lv_obj_create(parent);
        lv_obj_set_size(p, w, h);
        lv_obj_set_pos(p, x, y);
        lv_obj_set_style_bg_color(p, lv_color_hex(0x1a1a1a), 0);
        lv_obj_set_style_border_color(p, lv_color_hex(color::GREY), 0);
        lv_obj_set_style_border_width(p, 1, 0);
        lv_obj_set_style_radius(p, 6, 0);
        lv_obj_clear_flag(p, LV_OBJ_FLAG_SCROLLABLE);
        return p;
    }

    static lv_obj_t* create_dot(lv_obj_t* parent, uint16_t x, uint16_t y, lv_color_t col) {
        lv_obj_t* d = lv_obj_create(parent);
        lv_obj_set_size(d, 6, 6);
        lv_obj_set_pos(d, x, y);
        lv_obj_set_style_bg_color(d, col, 0);
        lv_obj_set_style_border_width(d, 0, 0);
        lv_obj_set_style_radius(d, 3, 0);
        return d;
    }

    static void style_badge(lv_obj_t* lbl, const char* text, lv_color_t bg, lv_color_t fg) {
        lv_label_set_text(lbl, text);
        lv_obj_set_style_text_color(lbl, fg, 0);
        lv_obj_set_style_bg_color(lbl, bg, 0);
        lv_obj_set_style_bg_opa(lbl, 255, 0);
        lv_obj_set_style_radius(lbl, 3, 0);
        lv_obj_set_style_pad_left(lbl, 4, 0);
        lv_obj_set_style_pad_right(lbl, 4, 0);
        lv_obj_set_style_pad_top(lbl, 1, 0);
        lv_obj_set_style_pad_bottom(lbl, 1, 0);
    }

} // namespace display
