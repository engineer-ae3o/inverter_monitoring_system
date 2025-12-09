#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/timers.h"

#include "button_handler.hpp"
#include "config.hpp"

#include "driver/gpio.h"
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


    // Forward declarations
    static void gpio_cleanup(void);
    static void IRAM_ATTR next_button_isr(void* arg);
    static void IRAM_ATTR prev_button_isr(void* arg);
    static void next_button_debounce_timer_cb(TimerHandle_t xTimer);
    static void prev_button_debounce_timer_cb(TimerHandle_t xTimer);


    esp_err_t init(void) {

        BTN_LOGI("Initializing button handler");

        const gpio_config_t button_config = {
            .pin_bit_mask = (1ULL << config::BUTTON_NEXT_PIN) | (1ULL << config::BUTTON_PREV_PIN),
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_ENABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_NEGEDGE
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

        ret = gpio_isr_handler_add(config::BUTTON_NEXT_PIN, next_button_isr, nullptr);
        if (ret != ESP_OK) {
            BTN_LOGE("Failed to add isr for next button gpio: %s", esp_err_to_name(ret));
            gpio_cleanup();
            gpio_uninstall_isr_service();
            return ret;
        }

        ret = gpio_isr_handler_add(config::BUTTON_PREV_PIN, prev_button_isr, nullptr);
        if (ret != ESP_OK) {
            BTN_LOGE("Failed to add isr for prev button gpio: %s", esp_err_to_name(ret));
            gpio_cleanup();
            gpio_uninstall_isr_service();
            gpio_isr_handler_remove(config::BUTTON_NEXT_PIN);
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
        if(!next_button_debounce_timer_handle) {
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
    
    static void IRAM_ATTR next_button_isr(void* arg) {
        BaseType_t higher_priority_task_woken = pdFALSE;
        xTimerStartFromISR(next_button_debounce_timer_handle, &higher_priority_task_woken);
    }

    static void IRAM_ATTR prev_button_isr(void* arg) {
        BaseType_t higher_priority_task_woken = pdFALSE;
        xTimerStartFromISR(prev_button_debounce_timer_handle, &higher_priority_task_woken);
    }

    static void next_button_debounce_timer_cb(TimerHandle_t xTimer) {
        if (gpio_get_level(config::BUTTON_NEXT_PIN)) return;
        event_t event = event_t::NEXT_BUTTON_PRESSED;
        xQueueSend(event_queue, &event, 0);
    }

    static void prev_button_debounce_timer_cb(TimerHandle_t xTimer) {
        if (gpio_get_level(config::BUTTON_PREV_PIN)) return;
        event_t event = event_t::PREV_BUTTON_PRESSED;
        xQueueSend(event_queue, &event, 0);
    }

} // namespace button