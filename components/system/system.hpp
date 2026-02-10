#ifndef _SYSTEM_HPP_
#define _SYSTEM_HPP_

#include "aht20.h"
#include "power_monitor.hpp"

#define ASSERT(exp, msg)                                                       \
    do {                                                                       \
        if (!exp) {                                                            \
            ESP_LOGE("ASSERT", "Assert failed: %s", #msg);                     \
            ESP_LOGE("ASSERT", "File: %s, Line: %d", __FILE__, __LINE__);      \
            sys::handle_error();                                               \
        }                                                                      \
    } while(0)

namespace sys {

    enum class inv_status_t : uint8_t {
        IDLE = 0,
        ACTIVE
    };

    enum class batt_status_t : uint8_t {
        IDLE = 0,
        DISCHARGING,
        RECHARGING
    };

    struct data_t {
        float battery_voltage;
        float load_current_drawn;
        float inv_temp;
        float inv_hmdt;
        float battery_percent;
        float power_drawn;
        inv_status_t inv_status;
        batt_status_t batt_status;
        uint64_t runtime_left_s;
    };


    /**
     * @brief Convert inverter status to string
     */
    const char* inv_status_to_string(const inv_status_t& status);

    /**
     * @brief Convert battery status to string
     */
    const char* batt_status_to_string(const batt_status_t& status);

    /**
     * @brief Calculates all the necessary runtime parameters required for a complete measurement of the inverter and battery statuses
     * 
     * @param[in] aht_data Struct reference containing sensor information from the AHT20 sensor
     * @param[in] power_data Struct reference containing measurement data from the voltage and current sensors
     * @param[out] final Struct reference containing the calculated monitoring data of the inverter and the betteries
     * 
     * @return true if all calculations were made without errors, false otherwise
     */
    bool calc_total_runtime_stats(const aht20_data_t& aht_data, const adc::data_t& power_data, data_t& final);

    /**
     * @brief Function to handle irrecoverable errors by rebooting the system
     * 
     * @note This function does not return
     */
    void handle_error(void);

} // namespace sys


#endif // _SYSTEM_HPP_