#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "lvgl.h"

#include "display.hpp"
#include "colors.hpp"
#include "vhorde_logo.hpp"
#include "screens.hpp"
#include "alert.hpp"

#include "esp_log.h"
#include "esp_err.h"
#include "esp_timer.h"

#include <array>
#include <cstdint>


// Debug logging levels
#define DISP_LOG_LEVEL_INFO 3
#define DISP_LOG_LEVEL_WARN 2
#define DISP_LOG_LEVEL_ERROR 1
#define DISP_LOG_LEVEL_NONE 0

// Set the log level to any appropriate log level
#define DISP_DEBUG_LEVEL DISP_LOG_LEVEL_INFO
static constexpr const char* TAG = "DISPLAY";

#if DISP_DEBUG_LEVEL == DISP_LOG_LEVEL_INFO
#define DISP_LOGE(...) ESP_LOGE(TAG, __VA_ARGS__)
#define DISP_LOGW(...) ESP_LOGW(TAG, __VA_ARGS__)
#define DISP_LOGI(...) ESP_LOGI(TAG, __VA_ARGS__)

#elif DISP_DEBUG_LEVEL == DISP_LOG_LEVEL_WARN
#define DISP_LOGE(...) ESP_LOGE(TAG, __VA_ARGS__)
#define DISP_LOGW(...) ESP_LOGW(TAG, __VA_ARGS__)
#define DISP_LOGI(...)

#elif DISP_DEBUG_LEVEL == DISP_LOG_LEVEL_ERROR
#define DISP_LOGE(...) ESP_LOGE(TAG, __VA_ARGS__)
#define DISP_LOGW(...)
#define DISP_LOGI(...)

#elif DISP_DEBUG_LEVEL == DISP_LOG_LEVEL_NONE
#define DISP_LOGE(...)
#define DISP_LOGW(...)
#define DISP_LOGI(...)
#endif


namespace display {

    // Screens
    static uint8_t current_screen_idx                       = 0;
    static constexpr uint8_t REGULAR_SCREENS_NUM            = 4;
    static constexpr uint8_t ENV_GRAPH_IDX                  = 4;
    static constexpr uint8_t POW_GRAPH_IDX                  = 5;

    // BLE popup and logo
    static lv_obj_t* ble_popup_handle                       = nullptr;
    static lv_obj_t* ble_logo_handle                        = nullptr;
    static esp_timer_handle_t ble_popup_close_timer         = nullptr;

    // Timeouts
    static constexpr uint32_t POPUP_TIMEOUT_US              = 2'000'000;
    static constexpr uint32_t TIMEOUT_MS                    = 200;
    
    // Display buffer size for LVGL (40 lines worth of pixels)
    static constexpr size_t DISP_BUF_SIZE                   = config::LCD_WIDTH * 40;
    static constexpr uint16_t DISP_BOOTUP_SCREEN_TIME_MS    = 2500;
    
    // LVGL buffers
    static std::array<lv_color16_t, DISP_BUF_SIZE> buf1{};
    static std::array<lv_color16_t, DISP_BUF_SIZE> buf2{};

    // General utilities
    static lv_display_t* display                            = nullptr;
    static esp_timer_handle_t lvgl_tick_timer               = nullptr;
    static ili9341_handle_t display_handle                  = nullptr;
    static SemaphoreHandle_t display_mutex                  = nullptr;

    // Alert queue, max 10 pending alerts
    static constexpr uint8_t ALERT_QUEUE_SIZE               = 10;
    static bool alerts_enabled                              = false;
    static lv_obj_t* alert_popup_handle                     = nullptr;
    static esp_timer_handle_t alert_popup_close_timer       = nullptr;

    // Bootup screen
    static lv_obj_t* bootup_scr                             = nullptr;

    struct ble_popup_text_t {
        const char* title{};
        const char* body{};
    };

    static constexpr ble_popup_text_t ble_text_lut[] = {
        [static_cast<uint8_t>(ble_popup_t::NO_EVENT)] = {
            nullptr,
            nullptr
        },
        [static_cast<uint8_t>(ble_popup_t::ACTIVATED)] = {
            "BLE activated!",
            "You can now be found and connect with other devices"
        },
        [static_cast<uint8_t>(ble_popup_t::DEACTIVATED)] = {
            "BLE deactivated!",
            "You can no longer be found or connect with other devices"
        },
        [static_cast<uint8_t>(ble_popup_t::ALREADY_ACTIVE)] = {
            "BLE already active!",
            nullptr
        },
        [static_cast<uint8_t>(ble_popup_t::ALREADY_INACTIVE)] = {
            "BLE already inactive!",
            nullptr
        },
        [static_cast<uint8_t>(ble_popup_t::ACTIVATION_FAILED)] = {
            "BLE activation failed!",
            "BLE activation failed for an unknown reason. Please try again"
        },
        [static_cast<uint8_t>(ble_popup_t::DEACTIVATION_FAILED)] = {
            "BLE deactivation failed!",
            "BLE deactivation failed for an unknown reason. Please try again"
        }
    };

    template<typename T, uint8_t N>
    class queue_t {
    private:
        std::array<T, N> data{};
        uint8_t count{};
        uint8_t head{};
        uint8_t tail{};

    public:
        [[nodiscard]] uint8_t get_num_elements() const { return count; }

        [[nodiscard]] bool is_empty() const { return count == 0; }

        [[nodiscard]] bool is_full() const { return count >= N; }

        [[nodiscard]] bool pop(T& e) {
            if (is_empty()) return false;
            e = data[tail];
            tail = (tail + 1) % N;
            count--;
            return true;
        }

        void push(const T& e) {
            data[head] = e;
            head = (head + 1) % N;
            if (count >= N) {
                tail = (tail + 1) % N;
            } else {
                count++;
            }
        }
    };

    static queue_t<entry_t, ALERT_QUEUE_SIZE> alert{};
    
    // Forward declarations
    static void disp_flush_cb(lv_display_t* display, const lv_area_t* area, uint8_t* px_map);
    static void create_animated_loading_bar(lv_obj_t* parent, uint8_t w, uint8_t h, uint16_t time_ms);
    static void show_next_alert();
    static void create_ble_logo();
    static void destroy_ble_logo();
    
    
    // Public functions
    esp_err_t init(const ili9341_handle_t& handle, SemaphoreHandle_t& disp_mutex) {

        DISP_LOGI("Initializing display interface");

        display_handle = handle;

        display_mutex = xSemaphoreCreateMutex();
        if (!display_mutex) {
            DISP_LOGE("Failed to create lvgl mutex");
            return ESP_ERR_NO_MEM;
        }

        disp_mutex = display_mutex;

        lv_init();

        display = lv_display_create(config::LCD_WIDTH, config::LCD_HEIGHT);
        lv_display_set_buffers(display, buf1.data(), buf2.data(), sizeof(buf1), LV_DISPLAY_RENDER_MODE_PARTIAL);
        lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
        lv_display_set_flush_cb(display, disp_flush_cb);
        
        // LVGL tick timer: required by LVGL
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
        
        ret = esp_timer_start_periodic(lvgl_tick_timer, 1000); // 1ms
        if (ret != ESP_OK) {
            DISP_LOGE("Failed to start LVGL tick timer: %s", esp_err_to_name(ret));
            deinit();
            return ret;
        }

        // BLE popup auto dismiss timer: required to close the ble popup that was created
        constexpr esp_timer_create_args_t ble_popup_close_args = {
            .callback = [](void* arg) {
                if (xSemaphoreTake(display_mutex, pdMS_TO_TICKS(TIMEOUT_MS)) != pdTRUE) return;
                if (ble_popup_handle) {
                    lv_msgbox_close(ble_popup_handle);
                    ble_popup_handle = nullptr;
                }
                xSemaphoreGive(display_mutex);
            },
            .arg = nullptr,
            .dispatch_method = ESP_TIMER_TASK,
            .name = "ble_popup_close_timer",
            .skip_unhandled_events = false
        };
        
        ret = esp_timer_create(&ble_popup_close_args, &ble_popup_close_timer);
        if (ret != ESP_OK) {
            DISP_LOGE("Failed to create ble_popup_close_timer: %s", esp_err_to_name(ret));
            deinit();
            return ret;
        }

        // Alert popup auto dismiss timer: calls `show_next_alert()` to advance
        // the queue and closes whichever alert popup that was active
        constexpr esp_timer_create_args_t alert_popup_close_args = {
            .callback = [](void* arg) {
                if (xSemaphoreTake(display_mutex, pdMS_TO_TICKS(TIMEOUT_MS)) != pdTRUE) return;
                if (alert_popup_handle) {
                    lv_msgbox_close(alert_popup_handle);
                    alert_popup_handle = nullptr;
                }
                // Display next queued alert popup, if any
                show_next_alert();
                xSemaphoreGive(display_mutex);
            },
            .arg = nullptr,
            .dispatch_method = ESP_TIMER_TASK,
            .name = "alert_popup_close_timer",
            .skip_unhandled_events = false
        };

        ret = esp_timer_create(&alert_popup_close_args, &alert_popup_close_timer);
        if (ret != ESP_OK) {
            DISP_LOGE("Failed to create alert_popup_close_timer: %s", esp_err_to_name(ret));
            deinit();
            return ret;
        }
        
        DISP_LOGI("Display interface initialized successfully");

        return ESP_OK;
    }

    void deinit() {

        if (xSemaphoreTake(display_mutex, pdMS_TO_TICKS(TIMEOUT_MS)) != pdTRUE) return;

        DISP_LOGI("Deinitializing display interface");

        if (lvgl_tick_timer) {
            esp_timer_stop(lvgl_tick_timer);
            esp_timer_delete(lvgl_tick_timer);
            lvgl_tick_timer = nullptr;
        }

        if (ble_popup_close_timer) {
            esp_timer_stop(ble_popup_close_timer);
            esp_timer_delete(ble_popup_close_timer);
            ble_popup_close_timer = nullptr;
        }

        if (alert_popup_close_timer) {
            esp_timer_stop(alert_popup_close_timer);
            esp_timer_delete(alert_popup_close_timer);
            alert_popup_close_timer = nullptr;
        }

        if (alert_popup_handle) {
            lv_msgbox_close(alert_popup_handle);
            alert_popup_handle = nullptr;
        }

        if (ble_popup_handle) {
            lv_msgbox_close(ble_popup_handle);
            ble_popup_handle = nullptr;
        }
        
        if (bootup_scr) {
            lv_obj_del(bootup_scr);
            bootup_scr = nullptr;
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

        xSemaphoreGive(display_mutex);
    }

    void bootup_screen() {

        if (xSemaphoreTake(display_mutex, pdMS_TO_TICKS(TIMEOUT_MS)) != pdTRUE) return;

        DISP_LOGI("Loading bootup screen");

        bootup_scr = lv_obj_create(nullptr);
        lv_obj_set_style_bg_color(bootup_scr, lv_color_hex(color::BLACK), 0);

        lv_obj_t* created_img = lv_img_create(bootup_scr);
        lv_img_set_src(created_img, &vhorde_logo);
        lv_obj_align(created_img, LV_ALIGN_TOP_MID, 0, 20);

        lv_scr_load(bootup_scr);

        xSemaphoreGive(display_mutex);

        create_animated_loading_bar(bootup_scr, 180, 35, DISP_BOOTUP_SCREEN_TIME_MS);

        DISP_LOGI("Done loading bootup screen");
    }
    
    void create_ui() {

        if (xSemaphoreTake(display_mutex, pdMS_TO_TICKS(TIMEOUT_MS)) != pdTRUE) return;

        DISP_LOGI("Creating UI");

        create_screen_0();
        create_screen_1();
        create_screen_2();
        create_screen_3();

        lv_scr_load(screens[0]);

        // Cleanup bootup screen resources
        // The children get auto deleted when
        // the parent screen is deleted
        if (bootup_scr) {
            lv_obj_del(bootup_scr);
            bootup_scr = nullptr;
        }

        xSemaphoreGive(display_mutex);

        DISP_LOGI("UI created");
    }
    
    void update_screen_data(const sys::data_t& data) {

        if (xSemaphoreTake(display_mutex, pdMS_TO_TICKS(TIMEOUT_MS)) != pdTRUE) return;

        switch (current_screen_idx) {
        case 0:
            update_screen_0(data);
            break;
        case 1:
            update_screen_1(data);
            break;
        case 2:
            update_screen_2(data);
            break;
        case 3:
            update_screen_3(data);
            break;
        case 4:
            update_screen_4(data);
            break;
        case 5:
            update_screen_5(data);
            break;
        default:
            DISP_LOGW("Invalid screen index");
            break;
        }

        if (alerts_enabled) {
            alert_handle_t alerts(data);
            if (alerts.check_set_alerts()) {
                alerts.display_warnings_if_alerts();
                show_next_alert();
            }
        }

        xSemaphoreGive(display_mutex);
    }

    void next_screen() {

        if (xSemaphoreTake(display_mutex, pdMS_TO_TICKS(TIMEOUT_MS)) != pdTRUE) return;

        // If we are currently displaying any of the graph screens,
        // go to screen 0 when we go to the next screen,
        // because the graph screens are inaccessible from the
        // `next_screen()` function
        if (current_screen_idx >= REGULAR_SCREENS_NUM) {
            current_screen_idx = 0;
        } else {
            current_screen_idx = (current_screen_idx + 1) % REGULAR_SCREENS_NUM;
        }
        lv_scr_load(screens[current_screen_idx]);

        DISP_LOGI("Switched to screen %d", current_screen_idx);

        xSemaphoreGive(display_mutex);
    }

    void prev_screen() {

        if (xSemaphoreTake(display_mutex, pdMS_TO_TICKS(TIMEOUT_MS)) != pdTRUE) return;

        // If we are currently displaying any of the graph screens, treat it 
        // similarly to screen 0 when we return to the previous screen,
        // because the graph screens are inaccessible from the
        // `prev_screen()` function
        if ((current_screen_idx >= REGULAR_SCREENS_NUM) || (current_screen_idx == 0)) {
            current_screen_idx = REGULAR_SCREENS_NUM - 1;
        } else {
            current_screen_idx--;
        }
        lv_scr_load(screens[current_screen_idx]);

        DISP_LOGI("Switched to screen %d", current_screen_idx);

        xSemaphoreGive(display_mutex);
    }

    void create_graph_screen(const graph_samples_t& env, const graph_samples_t& pow) {
        if (xSemaphoreTake(display_mutex, pdMS_TO_TICKS(TIMEOUT_MS)) != pdTRUE) return;
        create_screen_4(env);
        create_screen_5(pow);
        xSemaphoreGive(display_mutex);

        // Sanity check their creations
        ASSERT_(screens[ENV_GRAPH_IDX]);
        ASSERT_(screens[POW_GRAPH_IDX]);
    }

    void env_graph_screen() {
        if (xSemaphoreTake(display_mutex, pdMS_TO_TICKS(TIMEOUT_MS)) != pdTRUE) return;
        current_screen_idx = ENV_GRAPH_IDX;
        lv_scr_load(screens[current_screen_idx]);
        DISP_LOGI("Switched to screen %u", current_screen_idx);
        xSemaphoreGive(display_mutex);
    }

    void pow_graph_screen() {
        if (xSemaphoreTake(display_mutex, pdMS_TO_TICKS(TIMEOUT_MS)) != pdTRUE) return;
        current_screen_idx = POW_GRAPH_IDX;
        lv_scr_load(screens[current_screen_idx]);
        DISP_LOGI("Switched to screen %u", current_screen_idx);
        xSemaphoreGive(display_mutex);
    }

    bool ble_popup(ble_popup_t event) {

        if (event == ble_popup_t::NO_EVENT) return true;

        if (xSemaphoreTake(display_mutex, pdMS_TO_TICKS(TIMEOUT_MS)) != pdTRUE) return false;

        if (ble_popup_handle) {
            lv_msgbox_close(ble_popup_handle);
            ble_popup_handle = nullptr;
            esp_timer_stop(ble_popup_close_timer);
        }

        if (alert_popup_handle) {
            lv_msgbox_close(alert_popup_handle);
            alert_popup_handle = nullptr;
            esp_timer_stop(alert_popup_close_timer);
        }

        if (event == ble_popup_t::CLEAR_POPUPS) {
            xSemaphoreGive(display_mutex);
            return true;
        }

        const uint8_t idx = static_cast<uint8_t>(event);
        if (idx >= (sizeof(ble_text_lut) / sizeof(ble_text_lut[0]))) {
            xSemaphoreGive(display_mutex);
            return false;
        }
        const auto& [title, body] = ble_text_lut[idx];

        ble_popup_handle = lv_msgbox_create(screens[current_screen_idx]);
        if (title) lv_msgbox_add_title(ble_popup_handle, title);
        if (body) lv_msgbox_add_text(ble_popup_handle, body);

        // Use a bigger message box if the popup has both a title and a body
        if (title && body) lv_obj_set_size(ble_popup_handle, 200, 120);
        else if (title) lv_obj_set_size(ble_popup_handle, 200, 45);
        lv_obj_set_style_text_font(ble_popup_handle, &lv_font_montserrat_16, LV_PART_MAIN);
        lv_obj_center(ble_popup_handle);
        lv_scr_load(screens[current_screen_idx]);
        esp_timer_start_once(ble_popup_close_timer, POPUP_TIMEOUT_US);

        // Create the BLE logo if BLE was just activated
        if (event == ble_popup_t::ACTIVATED) {
            create_ble_logo();
        }
        // Destroy the BLE logo if BLE was just deactivated
        else if (event == ble_popup_t::DEACTIVATED) {
            destroy_ble_logo();
        }

        xSemaphoreGive(display_mutex);

        return true;
    }

    [[nodiscard]] bool is_popup_active() {
        if (ble_popup_handle || alert_popup_handle) return true;
        return false;
    }

    bool toggle_alert_popup_status() {
        alerts_enabled = alerts_enabled ? false : true;
        return alerts_enabled;
    }

    void push_alert(const entry_t& entry) {
        
        alert.push(entry);

        // If this is the only entry and no alert popup
        // is currently showing, display it immediately.
        if ((alert.get_num_elements() == 1) && (!alert_popup_handle)) {
            if (xSemaphoreTake(display_mutex, pdMS_TO_TICKS(TIMEOUT_MS)) != pdTRUE) return;
            show_next_alert();
            xSemaphoreGive(display_mutex);
        }
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

    static void create_animated_loading_bar(lv_obj_t* parent, uint8_t w, uint8_t h, uint16_t time_ms) {

        if (xSemaphoreTake(display_mutex, pdMS_TO_TICKS(TIMEOUT_MS)) != pdTRUE) return;

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

        xSemaphoreGive(display_mutex);

        vTaskDelay(pdMS_TO_TICKS(time_ms));
    }

    // Renders the next entry from the queue as an 
    // `lv_msgbox` with severity appropriate title color
    static void show_next_alert() {
        
        entry_t entry{};
        if (!alert.pop(entry)) return;

        // Pick title color by severity
        lv_color_t title_color{};
        switch (entry.severity) {
        case severity_t::CRITICAL:
            title_color = lv_color_hex(color::RED);
            break;
        case severity_t::WARNING:
            title_color = lv_color_hex(color::YELLOW);
            break;
        case severity_t::INFO:
        default:
            title_color = lv_color_hex(color::CYAN);
            break;
        }

        // If there is a currently active popup, we return
        if (alert_popup_handle) return;
        
        // Create msgbox on the currently visible screen
        alert_popup_handle = lv_msgbox_create(screens[current_screen_idx]);
        lv_msgbox_add_title(alert_popup_handle, entry.title);
        lv_msgbox_add_text(alert_popup_handle, entry.body);

        // Apply styling
        lv_obj_set_size(alert_popup_handle, 210, 120);
        lv_obj_set_style_text_font(alert_popup_handle, &lv_font_montserrat_16, LV_PART_MAIN);
        lv_obj_center(alert_popup_handle);

        // Style the title label as it's the first child of the message box
        lv_obj_t* title_label = lv_obj_get_child(alert_popup_handle, 0);
        if (title_label) lv_obj_set_style_text_color(title_label, title_color, 0);

        lv_scr_load(screens[current_screen_idx]);

        // Start auto dismiss timer; if more alerts are queued,
        // the callback will chain into the next one automatically
        // The popups last half as long as the ble popups
        esp_timer_start_once(alert_popup_close_timer, (POPUP_TIMEOUT_US / 2));
    }

    static void create_ble_logo() {

    }
    
    static void destroy_ble_logo() {

    }
    
} // namespace display
