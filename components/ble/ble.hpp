#ifndef _BLE_HPP_
#define _BLE_HPP_


#include "system.hpp"

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
     * @brief Sends notifications to ble client if subscribed to any notification
     * 
     * @param[in] data Reference to data containing data to be sent to ble client
     * 
     * @return ESP_OK on success, error code otherwise
     */
    esp_err_t notify_data(const sys::data_t& data);
    
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

} // namespace ble


#endif // _BLE_HPP_