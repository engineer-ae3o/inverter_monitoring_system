#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/timers.h"

#include "button_handler.hpp"
#include "config.hpp"

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_log.h"

#include <cstdint>


// Debug logging levels
#define BTN_LOG_LEVEL_INFO 3
#define BTN_LOG_LEVEL_WARN 2
#define BTN_LOG_LEVEL_ERROR 1
#define BTN_LOG_LEVEL_NONE 0

// Set the log level to any appropriate log level
#define BTN_LOG_LEVEL BTN_LOG_LEVEL_WARN
static constexpr const char* TAG = "Button_Handler";

#if BTN_LOG_LEVEL == BTN_LOG_LEVEL_INFO
#define BTN_LOGE(...) ESP_LOGE(TAG, __VA_ARGS__)
#define BTN_LOGW(...) ESP_LOGW(TAG, __VA_ARGS__)
#define BTN_LOGI(...) ESP_LOGI(TAG, __VA_ARGS__)

#elif BTN_LOG_LEVEL == BTN_LOG_LEVEL_WARN
#define BTN_LOGE(...) ESP_LOGE(TAG, __VA_ARGS__)
#define BTN_LOGW(...) ESP_LOGW(TAG, __VA_ARGS__)
#define BTN_LOGI(...)

#elif BTN_LOG_LEVEL == BTN_LOG_LEVEL_ERROR
#define BTN_LOGE(...) ESP_LOGE(TAG, __VA_ARGS__)
#define BTN_LOGW(...)
#define BTN_LOGI(...)

#elif BTN_LOG_LEVEL == BTN_LOG_LEVEL_NONE
#define BTN_LOGE(...)
#define BTN_LOGW(...)
#define BTN_LOGI(...)
#endif


namespace button {

    static TimerHandle_t next_button_debounce_timer_handle = nullptr;
    static TimerHandle_t prev_button_debounce_timer_handle = nullptr;
    static TimerHandle_t ble_button_debounce_timer_handle = nullptr;

    static QueueHandle_t event_queue = nullptr;

    static esp_timer_handle_t led_to_50_percent_brightness_timer = nullptr;
    static esp_timer_handle_t led_to_25_percent_brightness_timer = nullptr;
    static esp_timer_handle_t led_to_0_percent_brightness_timer = nullptr;

    static bool screen_at_100_percent = false;

    static int64_t start_prev_us = 0;
    static int64_t start_next_us = 0;
    static int64_t start_ble_us = 0;


    // Forward declarations
    static esp_err_t delete_freertos_timer(TimerHandle_t& timer);
    static void delete_esp_timer(esp_timer_handle_t& timer);
    static void update_display_led_and_timers(void);
    static void next_button_debounce_timer_cb(TimerHandle_t xTimer);
    static void prev_button_debounce_timer_cb(TimerHandle_t xTimer);
    static void ble_button_debounce_timer_cb(TimerHandle_t xTimer);


    // Public APIs
    esp_err_t init(esp_timer_handle_t& led_timer_handle) {

        BTN_LOGI("Initializing button handler");

        constexpr gpio_config_t button_config = {
            .pin_bit_mask = (1ULL << config::BUTTON_NEXT_PIN) | (1ULL << config::BUTTON_PREV_PIN) | (1ULL << config::BLE_PIN),
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_ENABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_ANYEDGE
        };

        esp_err_t ret = gpio_config(&button_config);
        if (ret != ESP_OK) {
            BTN_LOGE("Failed to configure gpio pins: %s", esp_err_to_name(ret));
            return ret;
        }

        ret = gpio_install_isr_service(ESP_INTR_FLAG_IRAM | ESP_INTR_FLAG_EDGE);
        if (ret != ESP_OK) {
            BTN_LOGE("Failed to install isr service: %s", esp_err_to_name(ret));
            deinit();
            return ret;
        }

        ret = gpio_isr_handler_add(config::BUTTON_NEXT_PIN,
            [](void* arg) {
                BaseType_t higher_priority_task_woken = pdFALSE;
                xTimerStartFromISR(next_button_debounce_timer_handle, &higher_priority_task_woken);
                gpio_intr_disable(config::BUTTON_NEXT_PIN);
            },
            nullptr);
        if (ret != ESP_OK) {
            BTN_LOGE("Failed to add isr for next button gpio: %s", esp_err_to_name(ret));
            deinit();
            return ret;
        }

        ret = gpio_isr_handler_add(config::BUTTON_PREV_PIN, 
            [](void* arg) {
                BaseType_t higher_priority_task_woken = pdFALSE;
                xTimerStartFromISR(prev_button_debounce_timer_handle, &higher_priority_task_woken);
                gpio_intr_disable(config::BUTTON_PREV_PIN);
            },
            nullptr);
        if (ret != ESP_OK) {
            BTN_LOGE("Failed to add isr for prev button gpio: %s", esp_err_to_name(ret));
            deinit();
            return ret;
        }

        ret = gpio_isr_handler_add(config::BLE_PIN, 
            [](void* arg) {
                BaseType_t higher_priority_task_woken = pdFALSE;
                xTimerStartFromISR(ble_button_debounce_timer_handle, &higher_priority_task_woken);
                gpio_intr_disable(config::BLE_PIN);
            },
            nullptr);
        if (ret != ESP_OK) {
            BTN_LOGE("Failed to add isr for BTN button gpio pin: %s", esp_err_to_name(ret));
            deinit();
            return ret;
        }

        constexpr ledc_timer_config_t display_led_timer_config = {
            .speed_mode = LEDC_HIGH_SPEED_MODE,
            .duty_resolution = LEDC_TIMER_10_BIT,
            .timer_num = LEDC_TIMER_1,
            .freq_hz = 20 * 1000,
            .clk_cfg = LEDC_AUTO_CLK,
            .deconfigure = false
        };

        ret = ledc_timer_config(&display_led_timer_config);
        if (ret != ESP_OK) {
            BTN_LOGE("Failed to initialize ledc timer");
            deinit();
            return ret;
        }

        constexpr ledc_channel_config_t display_led_channel_config = {
            .gpio_num = static_cast<int>(config::LED_PIN),
            .speed_mode = LEDC_HIGH_SPEED_MODE,
            .channel = LEDC_CHANNEL_1,
            .intr_type = LEDC_INTR_DISABLE,
            .timer_sel = LEDC_TIMER_1,
            .duty = 1023,
            .hpoint = 0,
            .flags = { .output_invert = 0 }
        };

        ret = ledc_channel_config(&display_led_channel_config);
        if (ret != ESP_OK) {
            BTN_LOGE("Failed to initialize ledc channel");
            deinit();
            return ret;
        }

        const esp_timer_create_args_t led_to_50_percent_timer = {
            .callback = [](void* arg) {
                ledc_set_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_1, 512);
                ledc_update_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_1);
                esp_timer_start_once(led_to_25_percent_brightness_timer, config::TIME_TO_LED_25_PERCENT_BRIGHTNESS_US);
                screen_at_100_percent = false;
            },
            .arg = nullptr,
            .dispatch_method = ESP_TIMER_TASK,
            .name = "led_to_50_percent_timer",
            .skip_unhandled_events = false
        };

        ret = esp_timer_create(&led_to_50_percent_timer, &led_to_50_percent_brightness_timer);
        if (ret != ESP_OK) {
            BTN_LOGE("Failed to create led_to_50_percent_timer");
            deinit();
            return ret;
        }

        const esp_timer_create_args_t led_to_25_percent_timer = {
            .callback = [](void* arg) {
                ledc_set_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_1, 256);
                ledc_update_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_1);
                esp_timer_start_once(led_to_0_percent_brightness_timer, config::TIME_TO_LED_0_PERCENT_BRIGHTNESS_US);
                screen_at_100_percent = false;
            },
            .arg = nullptr,
            .dispatch_method = ESP_TIMER_TASK,
            .name = "led_to_25_percent_timer",
            .skip_unhandled_events = false
        };

        ret = esp_timer_create(&led_to_25_percent_timer, &led_to_25_percent_brightness_timer);
        if (ret != ESP_OK) {
            BTN_LOGE("Failed to create led_to_25_percent_timer");
            deinit();
            return ret;
        }

        const esp_timer_create_args_t led_to_0_percent_timer = {
            .callback = [](void* arg) {
                ledc_set_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_1, 0);
                ledc_update_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_1);
                screen_at_100_percent = false;
            },
            .arg = nullptr,
            .dispatch_method = ESP_TIMER_TASK,
            .name = "led_to_0_percent_timer",
            .skip_unhandled_events = false
        };

        ret = esp_timer_create(&led_to_0_percent_timer, &led_to_0_percent_brightness_timer);
        if (ret != ESP_OK) {
            BTN_LOGE("Failed to create led_to_0_percent_timer");
            deinit();
            return ret;
        }

        event_queue = xQueueCreate(config::QUEUE_LENGTH, sizeof(event_t));
        if (!event_queue) {
            BTN_LOGE("Failed to create event_queue");
            deinit();
            return ESP_FAIL;
        }

        next_button_debounce_timer_handle = xTimerCreate("NextButtonDebounceTimer", pdMS_TO_TICKS(config::TIMEOUT_MS),
                                                         pdFALSE, nullptr, next_button_debounce_timer_cb);
        if(!next_button_debounce_timer_handle) {
            BTN_LOGE("Failed to create NextButtonDebounceTimer");
            deinit();
            return ESP_FAIL;
        }

        prev_button_debounce_timer_handle = xTimerCreate("PrevButtonDebounceTimer", pdMS_TO_TICKS(config::TIMEOUT_MS),
                                                         pdFALSE, nullptr, prev_button_debounce_timer_cb);
        if(!prev_button_debounce_timer_handle) {
            BTN_LOGE("Failed to create PrevButtonDebounceTimer");
            deinit();
            return ESP_FAIL;
        }

        ble_button_debounce_timer_handle = xTimerCreate("BTNButtonDebounceTimer", pdMS_TO_TICKS(config::TIMEOUT_MS),
                                                        pdFALSE, nullptr, ble_button_debounce_timer_cb);
        if(!ble_button_debounce_timer_handle) {
            BTN_LOGE("Failed to create ble_button_debounce_timer_handle");
            deinit();
            return ESP_FAIL;
        }

        led_timer_handle = led_to_50_percent_brightness_timer;
        screen_at_100_percent = true;

        BTN_LOGI("Initialization complete");

        return ESP_OK;
    }

    esp_err_t deinit(void) {

        gpio_reset_pin(config::BUTTON_NEXT_PIN);
        gpio_reset_pin(config::BUTTON_PREV_PIN);
        gpio_reset_pin(config::BLE_PIN);
        gpio_reset_pin(config::LED_PIN);

        gpio_isr_handler_remove(config::BUTTON_NEXT_PIN);
        gpio_isr_handler_remove(config::BUTTON_PREV_PIN);
        gpio_isr_handler_remove(config::BLE_PIN);

        gpio_uninstall_isr_service();

        esp_err_t ret = ledc_stop(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_1, 0);
        if (ret != ESP_OK) return ret;

        const ledc_timer_config_t led_timer_deconfig = {
            .speed_mode = LEDC_HIGH_SPEED_MODE,
            .timer_num = LEDC_TIMER_1,
            .deconfigure = true
        };

        ret = ledc_timer_config(&led_timer_deconfig);
        if (ret != ESP_OK) return ret;

        delete_esp_timer(led_to_50_percent_brightness_timer);
        delete_esp_timer(led_to_25_percent_brightness_timer);
        delete_esp_timer(led_to_0_percent_brightness_timer);

        if (event_queue) {
            vQueueDelete(event_queue);
            event_queue = nullptr;
        }

        ret = delete_freertos_timer(next_button_debounce_timer_handle);
        if (ret != ESP_OK) return ret;
        ret = delete_freertos_timer(prev_button_debounce_timer_handle);
        if (ret != ESP_OK) return ret;
        ret = delete_freertos_timer(ble_button_debounce_timer_handle);
        return ret;
    }

    QueueHandle_t get_queue() {
        return event_queue;
    }

    // Static helpers
    static esp_err_t delete_freertos_timer(TimerHandle_t& timer) {
        if (timer) {
            BaseType_t ret = xTimerStop(timer, 0);
            if (ret != pdPASS) return ESP_FAIL;
            ret = xTimerDelete(timer, 0);
            if (ret != pdPASS) return ESP_FAIL;
            timer = nullptr;
        }
        return ESP_OK;
    }

    static void delete_esp_timer(esp_timer_handle_t& timer) {
        if (timer) {
            esp_timer_stop(timer);
            esp_timer_delete(timer);
            timer = nullptr;
        }
    }

    static void update_display_led_and_timers() {
        // Increase the screen's brightness back to the max value
        ledc_set_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_1, 1023);
        ledc_update_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_1);
        screen_at_100_percent = true;
        // Stop all timers immediately if active
        esp_timer_stop(led_to_50_percent_brightness_timer);
        esp_timer_stop(led_to_25_percent_brightness_timer);
        esp_timer_stop(led_to_0_percent_brightness_timer);
        // Start the led_to_50_percent_brightness_timer
        esp_timer_start_once(led_to_50_percent_brightness_timer, config::TIME_TO_LED_50_PERCENT_BRIGHTNESS_US);
    }

    static void next_button_debounce_timer_cb(TimerHandle_t xTimer) {

        gpio_intr_enable(config::BUTTON_NEXT_PIN);

        event_t event = event_t::NO_EVENT;

        if (gpio_get_level(config::BUTTON_NEXT_PIN) == 0) {
            start_next_us = esp_timer_get_time();
            return;

        } else if (gpio_get_level(config::BUTTON_NEXT_PIN) == 1) {
            if (esp_timer_get_time() - start_next_us >= config::BUTTON_LONG_PRESS_US) {
                event = event_t::NEXT_LONG_PRESSED;
            } else {
                event = event_t::NEXT_BUTTON_PRESSED;
            }
            start_next_us = 0;
        }

        // Only send button updates if screen is at 100% brightness, else, just set screen to full brightness
        if (screen_at_100_percent) xQueueSend(event_queue, &event, 0);

        update_display_led_and_timers();
    }

    static void prev_button_debounce_timer_cb(TimerHandle_t xTimer) {

        gpio_intr_enable(config::BUTTON_PREV_PIN);

        event_t event = event_t::NO_EVENT;

        if (gpio_get_level(config::BUTTON_PREV_PIN) == 0) {
            start_prev_us = esp_timer_get_time();
            return;

        } else if (gpio_get_level(config::BUTTON_PREV_PIN) == 1) {
            if (esp_timer_get_time() - start_prev_us >= config::BUTTON_LONG_PRESS_US) {
                event = event_t::PREV_LONG_PRESSED;
            } else {
                event = event_t::PREV_BUTTON_PRESSED;
            }
            start_prev_us = 0;
        }

        // Only send button updates if screen is at 100% brightness, else, just set screen to full brightness
        if (screen_at_100_percent) xQueueSend(event_queue, &event, 0);

        update_display_led_and_timers();
    }

    static void ble_button_debounce_timer_cb(TimerHandle_t xTimer) {

        gpio_intr_enable(config::BLE_PIN);

        event_t event = event_t::NO_EVENT;

        if (gpio_get_level(config::BLE_PIN) == 0) {
            start_ble_us = esp_timer_get_time();
            return;

        } else if (gpio_get_level(config::BLE_PIN) == 1) {
            if ((esp_timer_get_time() - start_ble_us) >= config::BUTTON_EXTRA_LONG_PRESS_US) {
                event = event_t::BLE_EXTRA_LONG_PRESSED;
            } else if ((esp_timer_get_time() - start_ble_us) >= config::BUTTON_LONG_PRESS_US) {
                event = event_t::BLE_LONG_PRESSED;
            } else {
                event = event_t::BLE_BUTTON_PRESSED;
            }
            start_ble_us = 0;
        }

        // Only send button updates if screen is at 100% brightness, else, just set screen to full brightness
        if (screen_at_100_percent) xQueueSend(event_queue, &event, 0);

        update_display_led_and_timers();
    }

} // namespace button
