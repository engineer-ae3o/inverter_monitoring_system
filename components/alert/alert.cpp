#include "alert.hpp"

#include <cstring>


namespace display {

    alert_subsystem_t::alert_subsystem_t(const sys::data_t& data): data(data),
                                                               alerts{},
                                                               voltage_alert_present(false),
                                                               current_alert_present(false),
                                                               temp_alert_present(false),
                                                               hmdt_alert_present(false),
                                                               batt_alert_present(false) {}


    // TODO: Implement alert popups
    void alert_subsystem_t::voltage_alert_popup(void) {

    }

    void alert_subsystem_t::current_alert_popup(void) {

    }

    void alert_subsystem_t::temp_alert_popup(void) {

    }

    void alert_subsystem_t::hmdt_alert_popup(void) {

    }

    void alert_subsystem_t::batt_alert_popup(void) {

    }

    [[nodiscard]] bool alert_subsystem_t::check_set_alerts(void) {

        bool alerts_present = false;

        // Voltage classification
        if (data.battery_voltage <= 9.0f) {
            alerts.voltage = voltage_t::TOO_LOW;

            voltage_alert_present = true;
            alerts_present = true;

        } else if (data.battery_voltage <= 10.5f) {
            alerts.voltage = voltage_t::LOW;

            voltage_alert_present = true;
            alerts_present = true;

        } else if (data.battery_voltage > 12.6f) {
            alerts.voltage = voltage_t::HIGH;

            voltage_alert_present = true;
            alerts_present = true;

        } else {
            // Acceptable range: 9.0V - 12.6V
            alerts.voltage = voltage_t::OK;
        }
        
        // Current classification
        if (data.load_current_drawn <= -15.0f) {
            alerts.current = current_t::CHARGE_TOO_HIGH;

            current_alert_present = true;
            alerts_present = true;

        } else if (data.load_current_drawn <= -10.0f) {
            alerts.current = current_t::CHARGE_HIGH;

            current_alert_present = true;
            alerts_present = true;

        } else if (data.load_current_drawn >= 25.0f) {
            alerts.current = current_t::TOO_HIGH;

            current_alert_present = true;
            alerts_present = true;

        } else if (data.load_current_drawn >= 20.0f) {
            alerts.current = current_t::HIGH;

            current_alert_present = true;
            alerts_present = true;

        } else {
            // Acceptable range: 0A - 20A for discharge
            // Acceptable range: 0A - 10A for recharge
            alerts.current = current_t::OK;
        }
        
        // Temperature classification
        if (data.inv_temp <= 0.0f) {
            alerts.temp = temp_t::TOO_LOW;

            temp_alert_present = true;
            alerts_present = true;

        } else if (data.inv_temp <= 10.0f) {
            alerts.temp = temp_t::LOW;
            
            temp_alert_present = true;
            alerts_present = true;

        } else if (data.inv_temp >= 60.0f) {
            alerts.temp = temp_t::TOO_HIGH;
            
            temp_alert_present = true;
            alerts_present = true;

        } else if (data.inv_temp >= 45.0f) {
            alerts.temp = temp_t::HIGH;
            
            temp_alert_present = true;
            alerts_present = true;

        } else {
            // Acceptable range: 0°C - 45°C
            alerts.temp = temp_t::OK;
        }
        
        // Humidity classification
        if (data.inv_hmdt <= 10.0f) {
            alerts.hmdt = hmdt_t::TOO_LOW;

            temp_alert_present = true;
            alerts_present = true;

        } else if (data.inv_hmdt <= 20.0f) {
            alerts.hmdt = hmdt_t::LOW;

            temp_alert_present = true;
            alerts_present = true;

        } else if (data.inv_hmdt >= 80.0f) {
            alerts.hmdt = hmdt_t::TOO_HIGH;

            temp_alert_present = true;
            alerts_present = true;

        } else if (data.inv_hmdt >= 70.0f) {
            alerts.hmdt = hmdt_t::HIGH;

            temp_alert_present = true;
            alerts_present = true;

        } else {
            // Acceptable range: 20% - 70%
            alerts.hmdt = hmdt_t::OK;
        }
        
        // Battery percentage classification
        if (data.battery_percent <= 5.0f) {
            alerts.batt = batt_t::BELOW_5;

            batt_alert_present = true;
            alerts_present = true;

        } else if (data.battery_percent <= 10.0f) {
            alerts.batt = batt_t::BELOW_10;

            batt_alert_present = true;
            alerts_present = true;

        } else if (data.battery_percent <= 15.0f) {
            alerts.batt = batt_t::BELOW_15;
            
            batt_alert_present = true;
            alerts_present = true;

        } else if (data.battery_percent <= 50.0f) {
            alerts.batt = batt_t::BELOW_50;

            batt_alert_present = true;
            alerts_present = true;

        } else {
            // Acceptable range: Above 50%
            alerts.batt = batt_t::OK;
        }

        return alerts_present;
    }

    void alert_subsystem_t::display_warnings_if_alerts(void) {

        if (voltage_alert_present) {
            voltage_alert_popup();
        }

        if (current_alert_present) {
            current_alert_popup();
        }

        if (temp_alert_present) {
            temp_alert_popup();
        }

        if (hmdt_alert_present) {
            hmdt_alert_popup();
        }

        if (batt_alert_present) {
            batt_alert_popup();
        }

        // Clear for next cycle if any
        voltage_alert_present = false;
        current_alert_present = false;
        temp_alert_present = false;
        hmdt_alert_present = false;
        batt_alert_present = false;
    }
    
} // namespace display