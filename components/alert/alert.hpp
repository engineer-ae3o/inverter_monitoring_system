#ifndef _ALERT_HPP_
#define _ALERT_HPP_


#include "system.hpp"

#include <cstdint>


namespace display {

    class alert_subsystem_t {
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

        sys::data_t data;
        alerts_t alerts;

        bool voltage_alert_present;
        bool current_alert_present;
        bool temp_alert_present;
        bool hmdt_alert_present;
        bool batt_alert_present;

        void voltage_alert_popup();
        void current_alert_popup();
        void temp_alert_popup();
        void hmdt_alert_popup();
        void batt_alert_popup();

    public:
        /**
         * @brief Takes in a reference to the struct containing all relevant data
         */
        alert_subsystem_t(const sys::data_t& data);

        /**
         * @brief This checks for any alarms possible and fills the alert struct with the values gotten from the data struct
         * 
         * @return true if there are any alarms, false otherwise
         */
        [[nodiscard]] bool check_set_alerts();

        /**
         * @brief This function displays all alert popups, if any, one after the other
         */
        void display_warnings_if_alerts();
    };
}


#endif // _ALERT_HPP_