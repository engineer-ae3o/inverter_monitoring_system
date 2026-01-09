#include "lvgl.h"

#include "display.hpp"
#include "ili9341.h"
#include "colors.hpp"
#include "vhorde_logo.hpp"
#include "config.hpp"
#include "screens.hpp"
#include "alert.hpp"

#include "esp_log.h"
#include "esp_err.h"
#include "esp_timer.h"

#include <array>
#include <cstdint>


#define DIS_DEBUG 1

#if DIS_DEBUG == 1
static const char* TAG = "DISPLAY";
#define DISP_LOGI(...) ESP_LOGI(TAG, __VA_ARGS__)
#define DISP_LOGW(...) ESP_LOGW(TAG, __VA_ARGS__)
#define DISP_LOGE(...) ESP_LOGE(TAG, __VA_ARGS__)
#else
#define DISP_LOGI(...)
#define DISP_LOGW(...)
#define DISP_LOGE(...)
#endif


namespace display {

    // UI Screens
    static uint8_t current_screen_idx                       = 0;
    
    // Display buffer for LVGL (32 lines worth of pixels)
    static constexpr size_t DISP_BUF_SIZE                   = config::LCD_WIDTH * 32;
    static constexpr uint16_t DISP_BOOTUP_SCREEN_TIME_MS    = 2500;
    
    static std::array<uint16_t, DISP_BUF_SIZE> buf1{};
    static std::array<uint16_t, DISP_BUF_SIZE> buf2{};

    static lv_display_t* display                            = nullptr;
    static esp_timer_handle_t lvgl_tick_timer               = nullptr;
    static ili9341_handle_t display_handle                  = nullptr;
    
    // Forward declarations
    static void disp_flush_cb(lv_display_t* display, const lv_area_t* area, uint8_t* px_map);
    static void create_animated_loading_bar(lv_obj_t* parent, const uint8_t w, const uint8_t h, const uint16_t time_ms);
    
    
    // Public functions
    esp_err_t init(const ili9341_handle_t& handle) {

        DISP_LOGI("Initializing display interface");

        lv_init();

        display = lv_display_create(config::LCD_WIDTH, config::LCD_HEIGHT);

        lv_display_set_buffers(display, buf1.data(), buf2.data(), sizeof(buf1), LV_DISPLAY_RENDER_MODE_PARTIAL);

        lv_display_set_flush_cb(display, disp_flush_cb);
        
        constexpr esp_timer_create_args_t timer_args = {
            .callback = [](void* arg) { lv_tick_inc(1); },
            .arg = nullptr,
            .dispatch_method = ESP_TIMER_TASK,
            .name = "lvgl_tick",
            .skip_unhandled_events = false
        };
        
        esp_err_t ret = esp_timer_create(&timer_args, &lvgl_tick_timer);
        if (ret != ESP_OK) {
            DISP_LOGE("Failed to create LVGL tick timer: %s", esp_err_to_name(ret));
            return ret;
        }
        
        ret = esp_timer_start_periodic(lvgl_tick_timer, 1000); // 1000us
        if (ret != ESP_OK) {
            DISP_LOGE("Failed to start LVGL tick timer: %s", esp_err_to_name(ret));
            esp_timer_delete(lvgl_tick_timer);
            return ret;
        }

        display_handle = handle;
        
        DISP_LOGI("Display interface initialized successfully");

        return ESP_OK;
    }
    
    void deinit() {

        DISP_LOGI("Deinitializing display interface");

        if (lvgl_tick_timer) {
            esp_timer_stop(lvgl_tick_timer);
            esp_timer_delete(lvgl_tick_timer);
            lvgl_tick_timer = nullptr;
        }

        for (auto& screen : screens) {
            if (screen) {
                lv_obj_del(screen);
                screen = nullptr;
            }
        }

        if (display) {
            lv_disp_remove(display);
            display = nullptr;
        }
        
        DISP_LOGI("Display interface deinitialized");
    }

    void bootup_screen() {

        DISP_LOGI("Loading bootup screen");

        lv_obj_t* bootup_scr = lv_obj_create(nullptr);
        lv_obj_set_style_bg_color(bootup_scr, lv_color_hex(color::BLACK), 0);

        lv_obj_t* created_img = lv_img_create(bootup_scr);
        lv_img_set_src(created_img, &vhorde_logo);
        lv_obj_align(created_img, LV_ALIGN_TOP_MID, 0, 44);

        lv_scr_load(bootup_scr);

        create_animated_loading_bar(bootup_scr, 180, 35, DISP_BOOTUP_SCREEN_TIME_MS);

        DISP_LOGI("Done loading bootup screen");
    }
    
    void create_ui() {

        DISP_LOGI("Creating UI");

        create_screen_1();
        create_screen_2();
        create_screen_3();
        create_screen_4();
        create_screen_5();
        create_screen_6();
        create_screen_7();
        create_screen_8();

        lv_scr_load(screens[0]);

        DISP_LOGI("UI created");
    }
    
    void update_screen_data(const sys::data_t& data) {

        switch (current_screen_idx) {
        case 0:
            update_screen_1(data);
            break;
        case 1:
            update_screen_2(data);
            break;
        case 2:
            update_screen_3(data);
            break;
        case 3:
            update_screen_4(data);
            break;
        case 4:
            update_screen_5(data);
            break;
        case 5:
            update_screen_6(data);
            break;
        case 6:
            update_screen_7(data);
            break;
        case 7:
            update_screen_8(data);
            break;
        default:
            DISP_LOGW("Invalid screen index");
            break;
        }

        alert_subsystem_t alerts(data);

        if (alerts.check_set_alerts()) {
            alerts.display_warnings_if_alerts();
        }
    }

    void next_screen() {
        current_screen_idx = (current_screen_idx + 1) % NUM_SCREENS;
        lv_scr_load(screens[current_screen_idx]);

        DISP_LOGI("Switched to screen %d", current_screen_idx);
    }

    void prev_screen() {
        if (current_screen_idx == 0) {
            current_screen_idx = NUM_SCREENS - 1;
        } else {
            current_screen_idx--;
        }
        lv_scr_load(screens[current_screen_idx]);

        DISP_LOGI("Switched to screen %d", current_screen_idx);
    }

    // Helper Functions
    static void disp_flush_cb(lv_display_t* display, const lv_area_t* area, uint8_t* px_map) {

        const uint16_t width = area->x2 - area->x1 + 1;
        const uint16_t height = area->y2 - area->y1 + 1;
        const size_t pixel_count = width * height;

        const auto px_data = reinterpret_cast<uint16_t*>(px_map);

        esp_err_t ret = ili9341_flush(area->x1, area->y1, area->x2, area->y2, px_data, pixel_count, 
            [](void* user_data, esp_err_t ret) {
                auto display = static_cast<lv_display_t*>(user_data);
                lv_disp_flush_ready(display);

                if (ret != ESP_OK) {
                    DISP_LOGW("Flush completed with error: %s", esp_err_to_name(ret));
                }
            },
        display, display_handle);
        if (ret != ESP_OK) {
            DISP_LOGE("Flush failed: %s", esp_err_to_name(ret));
        }
    }

    static void create_animated_loading_bar(lv_obj_t* parent, const uint8_t w, const uint8_t h, const uint16_t time_ms) {

        lv_obj_t* loading_bar = lv_bar_create(parent);

        lv_obj_set_size(loading_bar, w, h);
        lv_bar_set_range(loading_bar, 0, 100);
        lv_obj_align(loading_bar, LV_ALIGN_TOP_MID, 0, 270);
        lv_bar_set_value(loading_bar, 0, LV_ANIM_ON);

        lv_scr_load(parent);

        lv_anim_t bar_anim{};

        lv_anim_init(&bar_anim);
        lv_anim_set_var(&bar_anim, loading_bar);
        lv_anim_set_time(&bar_anim, time_ms);
        lv_anim_set_values(&bar_anim, 0, 100);
        lv_anim_set_repeat_count(&bar_anim, 0);
        lv_anim_set_exec_cb(&bar_anim, 
            [](void* user_data, int32_t value) {
                lv_bar_set_value(static_cast<lv_obj_t*>(user_data), value, LV_ANIM_ON);
            }
        );
        lv_anim_set_path_cb(&bar_anim, lv_anim_path_ease_in);

        lv_anim_start(&bar_anim);

        vTaskDelay(pdMS_TO_TICKS(time_ms));
    }

} // namespace display
