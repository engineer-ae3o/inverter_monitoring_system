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
     * @brief Switch to next screen
     */
    void next_screen();

    /**
     * @brief Switch to previous screen
     */
    void prev_screen();

} // namespace display


#endif // _DISPLAY_HPP_