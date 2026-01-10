#ifndef _DISPLAY_HPP_
#define _DISPLAY_HPP_


#include "lvgl.h"

#include "system.hpp"
#include "ili9341.h"

#include <cstdint>


namespace display {

    /**
     * @brief Initializes LVGL and the display interface
     * 
     * @param[in] handle Handle to the current instance of the driver being used
     * 
     * @return ESP_OK on sucess, error code otherwise
     */
    esp_err_t init(const ili9341_handle_t& handle);
    
    /**
     * @brief Deinitializes the display interface
     */
    void deinit();

    /**
     * @brief Displays the bootup screen
     * 
     * @note Must be called once after init and before calling any other function in this header
     */
    void bootup_screen();
    
    /**
     * @brief This creates and displays all UI screens
     */
    void create_ui();

    /**
     * @brief This updates all UI screens with data received
     * 
     * @param[in] data Reference to struct containing data to update the display
     */
    void update_screen_data(const sys::data_t& data);

    /**
     * @brief Switches to next screen
     */
    void next_screen();

    /**
     * @brief Switches to previous screen
     */
    void prev_screen();

    enum class ble_popup_t : uint8_t {
        NO_EVENT = 0,
        ACTIVATED,
        DEACTIVATED,
        ALREADY_ACTIVE,
        ALREADY_INACTIVE,
        ACTIVATION_FAILED,
        DEACTIVATION_FAILED,
        CLEAR_POPUPS
    };

    /**
     * @brief Displays a popup screen for ble button events depending on current ble state
     * 
     * @param event BLE button event which occured
     * 
     * @return true if popup was displayed successfully, false otherwise or if an invalid parameter was passed
     */
    bool ble_popup(ble_popup_t event);

    /**
     * @brief Checks if there is a BLE popup active
     * 
     * @return true if a BLE popup is active
     */
    bool is_ble_popup_active();

} // namespace display


#endif // _DISPLAY_HPP_