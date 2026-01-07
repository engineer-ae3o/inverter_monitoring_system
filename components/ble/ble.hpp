#ifndef _BLE_HPP_
#define _BLE_HPP_


#include "esp_err.h"

namespace ble {

    /**
     * @brief Initializes the ble interface
     * 
     * @return ESP_OK on success, error code otherwise
     * 
     * @note This also initializes nvs flash
     */
    esp_err_t init();
    
    /**
     * @brief Deinitializes the ble interface
     * 
     * @return ESP_OK on success, error code otherwise
     */
    esp_err_t deinit();
    
    /**
     * @brief Start BLE advertising
     * 
     * @return ESP_OK on success, error code otherwise
     */
    esp_err_t start();

    /**
     * @brief Stops BLE advertising
     * 
     * @return ESP_OK on success, error code otherwise
     */
    esp_err_t stop();

    constexpr float get_temperature() {
        return 35.0;
    }
    constexpr float get_humidity() {
        return 74.0;
    }
    constexpr float get_voltage() {
        return 11.3;
    }
    constexpr float get_current() {
        return 13.2;
    }
    constexpr float get_power() {
        return (get_voltage() * get_current());
    }
    constexpr float get_battery_soc() {
        return 56.2;
    }
    constexpr uint64_t get_runtime() {
        return static_cast<uint64_t>(2.4 * 3600);
    }

} // namespace ble


#endif // _BLE_HPP_