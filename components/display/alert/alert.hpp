#ifndef _ALERT_HPP_
#define _ALERT_HPP_


#include "system.hpp"

#include <cstdint>


namespace display {
    
    // Alerts
    enum class voltage_t : int8_t {
        LOW = -1,
        OK = 0,
        HIGH = 1
    };

    enum class current_t : int8_t {
        LOW = -1,
        OK = 0,
        HIGH = 1
    };

    enum class temp_t : int8_t {
        LOW = -1,
        OK = 0,
        HIGH = 1
    };

    enum class hmdt_t : int8_t {
        LOW = -1,
        OK = 0,
        HIGH = 1
    };

    enum class batt_t : uint8_t {
        OK = 0,
        BELOW_50,
        BELOW_15,
        BELOW_10,
        BELOW_5
    };
    
    struct alerts_t {
        voltage_t voltage;
        current_t current;
        temp_t temp;
        hmdt_t hmdt;
        batt_t batt;
    };


    [[nodiscard]] bool check_set_alerts(const sys::data_t& data, alerts_t& alert);

    void display_warnings_if_alerts(const alerts_t& alert);

} // namespace display

#endif // _ALERT_HPP_