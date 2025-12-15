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


#define BTN_DEBUG 0

#if BTN_DEBUG == 1
static const char* TAG = "BUTTON_HANDLER";
#define BTN_LOGI(...) ESP_LOGI(TAG, __VA_ARGS__)
#define BTN_LOGW(...) ESP_LOGW(TAG, __VA_ARGS__)
#define BTN_LOGE(...) ESP_LOGE(TAG, __VA_ARGS__)
#else
#define BTN_LOGI(...)
#define BTN_LOGW(...)
#define BTN_LOGE(...)
#endif


namespace button {

    static TimerHandle_t next_button_debounce_timer_handle = nullptr;
    static StaticTimer_t next_button_debounce_timer_buffer;

    static TimerHandle_t prev_button_debounce_timer_handle = nullptr;
    static StaticTimer_t prev_button_debounce_timer_buffer;

    static QueueHandle_t event_queue = nullptr;
    static uint8_t final_data_queue_stack[config::QUEUE_LENGTH * sizeof(event_t)];
    static StaticQueue_t final_data_queue_buffer;

    static esp_timer_handle_t led_to_50_percent_brightness_timer = nullptr;
    static esp_timer_handle_t led_to_25_percent_brightness_timer = nullptr;
    static esp_timer_handle_t led_to_0_percent_brightness_timer = nullptr;

    static bool screen_is_on = false;

    static int64_t start_prev_us = 0;
    static int64_t start_next_us = 0;


    // Forward declarations
    static void gpio_cleanup(void);
    static void update_display_led_and_timers(void);
    static void next_button_debounce_timer_cb(TimerHandle_t xTimer);
    static void prev_button_debounce_timer_cb(TimerHandle_t xTimer);


    esp_err_t init(esp_timer_handle_t* led_timer_handle) {

        BTN_LOGI("Initializing button handler");

        const gpio_config_t button_config = {
            .pin_bit_mask = (1ULL << config::BUTTON_NEXT_PIN) | (1ULL << config::BUTTON_PREV_PIN),
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
            gpio_cleanup();
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
            gpio_cleanup();
            gpio_uninstall_isr_service();
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
            gpio_cleanup();
            gpio_uninstall_isr_service();
            gpio_isr_handler_remove(config::BUTTON_NEXT_PIN);
            return ret;
        }

        const ledc_timer_config_t display_led_timer_config = {
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

        const ledc_channel_config_t display_led_channel_config = {
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
                screen_is_on = false;
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

        event_queue = xQueueCreateStatic(config::QUEUE_LENGTH, sizeof(event_t), final_data_queue_stack, &final_data_queue_buffer);
        if (!event_queue) {
            BTN_LOGE("Failed to create event_queue");
            deinit();
            return ESP_FAIL;
        }

        next_button_debounce_timer_handle = xTimerCreateStatic(
                                                               "NextButtonDebounceTimer",
                                                               pdMS_TO_TICKS(config::TIMEOUT_MS),
                                                               pdFALSE,
                                                               nullptr,
                                                               next_button_debounce_timer_cb,
                                                               &next_button_debounce_timer_buffer
        );
        if(!next_button_debounce_timer_handle) 
        {
            BTN_LOGE("Failed to create NextButtonDebounceTimer");
            deinit();
            return ESP_FAIL;
        }

        prev_button_debounce_timer_handle = xTimerCreateStatic(
                                                               "PrevButtonDebounceTimer",
                                                               pdMS_TO_TICKS(config::TIMEOUT_MS),
                                                               pdFALSE,
                                                               nullptr,
                                                               prev_button_debounce_timer_cb,
                                                               &prev_button_debounce_timer_buffer
        );
        if(!prev_button_debounce_timer_handle) {
            BTN_LOGE("Failed to create PrevButtonDebounceTimer");
            deinit();
            return ESP_FAIL;
        }

        *led_timer_handle = led_to_50_percent_brightness_timer;
        screen_is_on = true;

        BTN_LOGI("Initialization complete");

        return ESP_OK;
    }

    esp_err_t deinit(void) {

        gpio_cleanup();

        gpio_isr_handler_remove(config::BUTTON_NEXT_PIN);
        gpio_isr_handler_remove(config::BUTTON_PREV_PIN);

        gpio_uninstall_isr_service();

        if (next_button_debounce_timer_handle) {
            BaseType_t ret = xTimerStop(next_button_debounce_timer_handle, 0);
            if (ret != pdPASS) return ret;
            next_button_debounce_timer_handle = nullptr;
        }

        if (prev_button_debounce_timer_handle) {
            BaseType_t ret = xTimerStop(prev_button_debounce_timer_handle, 0);
            if (ret != pdPASS) return ret;
            prev_button_debounce_timer_handle = nullptr;
        }

        event_queue = nullptr;

        return ESP_OK;
    }

    QueueHandle_t get_queue(void) {
        return event_queue;
    }

    static void gpio_cleanup(void) {
        gpio_reset_pin(config::BUTTON_NEXT_PIN);
        gpio_reset_pin(config::BUTTON_PREV_PIN);
    }

    static void update_display_led_and_timers(void) {
        // Increase the screen's brightness back to the max value
        ledc_set_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_1, 1024);
        ledc_update_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_1);
        screen_is_on = true;
        // Stop all timers immediately
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
                start_next_us = 0;
            } else {
                event = event_t::NEXT_BUTTON_PRESSED;
                start_next_us = 0;
            }
        }

        // Only send button updates if screen is on
        if (screen_is_on) xQueueSend(event_queue, &event, 0);

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
                start_prev_us = 0;
            } else {
                event = event_t::PREV_BUTTON_PRESSED;
                start_prev_us = 0;
            }
        }

        // Only send button updates if screen is on
        if (screen_is_on) xQueueSend(event_queue, &event, 0);

        update_display_led_and_timers();
    }

} // namespace button