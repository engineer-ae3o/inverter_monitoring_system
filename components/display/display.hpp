#ifndef _DISPLAY_HPP_
#define _DISPLAY_HPP_


#include "lvgl.h"

#include "system.hpp"
#include "ili9341.h"
#include "config.hpp"

#include <cstdint>


namespace display {

    /**
     * @brief Initializes LVGL and the display interface
     * 
     * @param[in] handle Handle to the current instance of the driver being used
     * @param[in] disp_mutex Mutex to ensure thread safety across lvgl api calls
     * 
     * @return ESP_OK on sucess, error code otherwise
     */
    esp_err_t init(const ili9341_handle_t& handle, const SemaphoreHandle_t& disp_mutex);
    
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
     * @brief This creates all UI screens and displays first screen
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

    struct graph_samples_t {
        std::array<float, config::GRAPH_SAMPLES>& first;
        std::array<float, config::GRAPH_SAMPLES>& second;
    };

    /**
     * @brief Creates all graph screens seperately from the other screens
     * 
     * @param[in] env - Reference to struct containing samples for temperature and humidity
     * @param[in] pow - Reference to struct containing samples for voltage and current
     */
    void create_graph_screen(const graph_samples_t& env, const graph_samples_t& pow);

    /**
     * @brief Switch to temperature and humidity graph screen
     */
    void env_graph_screen();

    /**
     * @brief Switch to voltage and current graph screen
     */
    void pow_graph_screen();

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
     * @return true if popup event was executed successfully, false otherwise or if an invalid parameter was passed
     */
    bool ble_popup(ble_popup_t event);

    /**
     * @brief Checks if there is a BLE popup active
     * 
     * @return true if a BLE popup is active
     */
    [[nodiscard]] bool is_ble_popup_active();

    // Forward declared here; full definition is in alert.hpp
    struct entry_t;

    /**
     * @brief Pushes an alert entry into the display layer's alert queue.
     *        If no alert popup is currently showing, the first entry displays
     *        immediately. Subsequent entries wait and are shown in order as
     *        each auto dismiss timer fires.
     * 
     * @param[in] entry The alert to enqueue
     */
    void push_alert(entry_t entry);

} // namespace display


#endif // _DISPLAY_HPP_