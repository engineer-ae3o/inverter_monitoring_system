#ifndef _BUTTON_HANDLER_HPP_
#define _BUTTON_HANDLER_HPP_


#include "esp_err.h"
#include "esp_timer.h"

namespace button {

    enum class event_t : uint8_t {
        NO_EVENT = 0,
        NEXT_BUTTON_PRESSED,
        PREV_BUTTON_PRESSED,
        NEXT_LONG_PRESSED,
        PREV_LONG_PRESSED,
        BLE_BUTTON_PRESSED,
        BLE_LONG_PRESSED
    };

    /**
     * @brief Initialize button GPIO and ISR handlers
     * 
     * @param[out] led_timer_handle Reference to the timer handle which is responsible for led dimming
     * 
     * @return ESP_OK on success, error code otherwise
     */
    esp_err_t init(esp_timer_handle_t& led_timer_handle);

    /**
     * @brief Deinitialize button handler
     * 
     * @return ESP_OK on success, error code otherwise
     */
    esp_err_t deinit(void);

    /** 
     * @brief Getter for button event queue
     * 
     * @return Handle to queue in which events are passed
     */
    QueueHandle_t get_queue();
    
} // namespace button

#endif // _BUTTON_HANDLER_HPP_