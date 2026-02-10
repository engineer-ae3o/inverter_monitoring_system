#ifndef _ALERT_HPP_
#define _ALERT_HPP_


#include "system.hpp"
#include "display.hpp"

#include <cstdint>


namespace display {

    // Severity levels: this drives title prefix and popup color
    enum class severity_t : uint8_t {
        INFO     = 0,   // Cyan: informational, no action needed
        WARNING  = 1,   // Yellow: monitor, may require action
        CRITICAL = 2    // Red: requires immediate attention
    };

    // Entry pushed into the alert queue by
    // alert_handle_t, used to render the popup.
    struct entry_t {
        severity_t severity{};
        char title[32]{}; // e.g. "VOLTAGE LOW"
        char body[96]{};  // e.g. "8.7V  threshold: 10.5V\nBattery depleting"
    };

    class alert_handle_t {
    private:
        enum class voltage_t : int8_t {
            TOO_LOW = -2,
            LOW = -1,
            OK = 0,
            HIGH = 1
        };

        enum class current_t : int8_t {
            CHARGE_TOO_HIGH = -2,
            CHARGE_HIGH = -1,
            OK = 0,
            HIGH = 1,
            TOO_HIGH = 2
        };

        enum class temp_t : int8_t {
            TOO_LOW = -2,
            LOW = -1,
            OK = 0,
            HIGH = 1,
            TOO_HIGH = 2
        };

        enum class hmdt_t : int8_t {
            TOO_LOW = -2,
            LOW = -1,
            OK = 0,
            HIGH = 1,
            TOO_HIGH = 2
        };

        enum class batt_t : uint8_t {
            OK = 0,
            BELOW_50 = 1,
            BELOW_15 = 2,
            BELOW_10 = 3,
            BELOW_5 = 4
        };

        struct alerts_t {
            voltage_t voltage{};
            current_t current{};
            temp_t temp{};
            hmdt_t hmdt{};
            batt_t batt{};
        };

        sys::data_t data{};
        alerts_t alerts{};
        
        // Each builds an entry_t and enqueues it via enqueue_alert()
        void voltage_alert_popup();
        void current_alert_popup();
        void temp_alert_popup();
        void hmdt_alert_popup();
        void batt_alert_popup();

    public:
        /**
         * @brief Takes in a reference to the struct containing all relevant data
         */
        alert_handle_t(const sys::data_t& data);
        
        /**
         * @brief Checks for any alarms and fills the alert struct
         * 
         * @return true if there are any alarms, false otherwise
         */
        [[nodiscard]] bool check_set_alerts();

        /**
         * @brief Enqueues alert popups for all active alerts
         */
        void display_warnings_if_alerts();
    };
}


#endif // _ALERT_HPP_