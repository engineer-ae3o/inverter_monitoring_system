#include "alert.hpp"

#include <cstdio>
#include <cstring>


namespace display {

    alert_handle_t::alert_handle_t(const sys::data_t& data): data(data) {}
    
    void alert_handle_t::voltage_alert_popup() {

        entry_t entry{};

        switch (alerts.voltage) {
        case voltage_t::TOO_LOW:
            entry.severity = severity_t::CRITICAL;
            strncpy(entry.title, "VOLTAGE TOO LOW!", sizeof(entry.title) - 1);
            snprintf(entry.body, sizeof(entry.body) - 1,
                "%.2fV  threshold: 9.0V\nBattery near empty.\nShutdown imminent.", data.battery_voltage);
            break;

        case voltage_t::LOW:
            entry.severity = severity_t::WARNING;
            strncpy(entry.title, "VOLTAGE LOW!", sizeof(entry.title) - 1);
            snprintf(entry.body, sizeof(entry.body) - 1,
                "%.2fV  threshold: 10.5V\nBattery depleting.", data.battery_voltage);
            break;

        case voltage_t::HIGH:
            entry.severity = severity_t::WARNING;
            strncpy(entry.title, "VOLTAGE HIGH!", sizeof(entry.title) - 1);
            snprintf(entry.body, sizeof(entry.body) - 1,
                "%.2fV  threshold: 12.6V\nPossible overcharge.", data.battery_voltage);
            break;

        default:
            return;
        }

        push_alert(entry);
    }
    
    void alert_handle_t::current_alert_popup() {

        entry_t entry{};

        switch (alerts.current) {
        case current_t::CHARGE_TOO_HIGH:
            entry.severity = severity_t::CRITICAL;
            strncpy(entry.title, "CHARGE CURRENT TOO HIGH!", sizeof(entry.title) - 1);
            snprintf(entry.body, sizeof(entry.body) - 1,
                "%.2fA  threshold: -15.0A\nCharger overcurrent.\nCheck charger.", data.load_current_drawn);
            break;

        case current_t::CHARGE_HIGH:
            entry.severity = severity_t::WARNING;
            strncpy(entry.title, "CHARGE CURRENT HIGH!", sizeof(entry.title) - 1);
            snprintf(entry.body, sizeof(entry.body) - 1,
                "%.2fA  threshold: -10.0A\nCharger current elevated.", data.load_current_drawn);
            break;

        case current_t::HIGH:
            entry.severity = severity_t::WARNING;
            strncpy(entry.title, "LOAD CURRENT HIGH!", sizeof(entry.title) - 1);
            snprintf(entry.body, sizeof(entry.body) - 1,
                "%.2fA  threshold: 20.0A\nLoad approaching limit.", data.load_current_drawn);
            break;

        case current_t::TOO_HIGH:
            entry.severity = severity_t::CRITICAL;
            strncpy(entry.title, "LOAD CURRENT TOO HIGH!", sizeof(entry.title) - 1);
            snprintf(entry.body, sizeof(entry.body) - 1,
                "%.2fA  threshold: 25.0A\nLoad overcurrent.\nReduce load now.", data.load_current_drawn);
            break;

        default:
            return;
        }

        push_alert(entry);
    }
    
    void alert_handle_t::temp_alert_popup() {

        entry_t entry{};

        switch (alerts.temp) {
        case temp_t::TOO_LOW:
            entry.severity = severity_t::CRITICAL;
            strncpy(entry.title, "TEMPERATURE TOO LOW!", sizeof(entry.title) - 1);
            snprintf(entry.body, sizeof(entry.body) - 1,
                "%.2f°C  threshold: 0°C\nFreezing conditions.\nCheck environment.", data.inv_temp);
            break;

        case temp_t::LOW:
            entry.severity = severity_t::WARNING;
            strncpy(entry.title, "TEMPERATURE LOW!", sizeof(entry.title) - 1);
            snprintf(entry.body, sizeof(entry.body) - 1,
                "%.2f°C  threshold: 10°C\nCold conditions.", data.inv_temp);
            break;

        case temp_t::HIGH:
            entry.severity = severity_t::WARNING;
            strncpy(entry.title, "TEMPERATURE HIGH!", sizeof(entry.title) - 1);
            snprintf(entry.body, sizeof(entry.body) - 1,
                "%.2f°C  threshold: 45°C\nTemperature elevated.", data.inv_temp);
            break;

        case temp_t::TOO_HIGH:
            entry.severity = severity_t::CRITICAL;
            strncpy(entry.title, "TEMPERATURE TOO HIGH!", sizeof(entry.title) - 1);
            snprintf(entry.body, sizeof(entry.body) - 1,
                "%.2f°C  threshold: 60°C\nThermal danger.\nCheck cooling.", data.inv_temp);
            break;

        default:
            return;
        }

        push_alert(entry);
    }
    
    void alert_handle_t::hmdt_alert_popup() {

        entry_t entry{};

        switch (alerts.hmdt) {
        case hmdt_t::TOO_LOW:
            entry.severity = severity_t::CRITICAL;
            strncpy(entry.title, "HUMIDITY TOO LOW!", sizeof(entry.title) - 1);
            snprintf(entry.body, sizeof(entry.body) - 1,
                "%.2f%%  threshold: 10%%\nVery dry conditions.\nStatic risk.", data.inv_hmdt);
            break;

        case hmdt_t::LOW:
            entry.severity = severity_t::WARNING;
            strncpy(entry.title, "HUMIDITY LOW!", sizeof(entry.title) - 1);
            snprintf(entry.body, sizeof(entry.body) - 1,
                "%.2f%%  threshold: 20%%\nDry conditions.", data.inv_hmdt);
            break;

        case hmdt_t::HIGH:
            entry.severity = severity_t::WARNING;
            strncpy(entry.title, "HUMIDITY HIGH!", sizeof(entry.title) - 1);
            snprintf(entry.body, sizeof(entry.body) - 1,
                "%.2f%%  threshold: 70%%\nHumidity elevated.", data.inv_hmdt);
            break;

        case hmdt_t::TOO_HIGH:
            entry.severity = severity_t::CRITICAL;
            strncpy(entry.title, "HUMIDITY TOO HIGH!", sizeof(entry.title) - 1);
            snprintf(entry.body, sizeof(entry.body) - 1,
                "%.2f%%  threshold: 80%%\nCondensation risk.\nCheck ventilation.", data.inv_hmdt);
            break;

        default:
            return;
        }

        push_alert(entry);
    }
    
    void alert_handle_t::batt_alert_popup() {

        entry_t entry{};

        switch (alerts.batt) {
        case batt_t::BELOW_5:
            entry.severity = severity_t::CRITICAL;
            strncpy(entry.title, "BATTERY SoC TOO LOW!", sizeof(entry.title) - 1);
            snprintf(entry.body, sizeof(entry.body) - 1,
                "%.2f%%  threshold: 5%%\nNear shutdown.\nCharge immediately.", data.battery_percent);
            break;

        case batt_t::BELOW_10:
            entry.severity = severity_t::WARNING;
            strncpy(entry.title, "BATTERY SoC LOW!", sizeof(entry.title) - 1);
            snprintf(entry.body, sizeof(entry.body) - 1,
                "%.2f%%  threshold: 10%%\nBattery getting low.", data.battery_percent);
            break;

        case batt_t::BELOW_15:
            entry.severity = severity_t::WARNING;
            strncpy(entry.title, "BATTERY SoC LOW!", sizeof(entry.title) - 1);
            snprintf(entry.body, sizeof(entry.body) - 1,
                "%.2f%%  threshold: 15%%\nBattery low.", data.battery_percent);
            break;

        case batt_t::BELOW_50:
            entry.severity = severity_t::INFO;
            strncpy(entry.title, "BATTERY SoC NOTICE!", sizeof(entry.title) - 1);
            snprintf(entry.body, sizeof(entry.body) - 1,
                "%.2f%%  threshold: 50%%\nBattery below half.", data.battery_percent);
            break;

        default:
            return;
        }

        push_alert(entry);
    }

    [[nodiscard]] bool alert_handle_t::check_set_alerts() {

        bool alerts_present = false;

        // Voltage classification
        if (data.battery_voltage <= 9.0f) {
            alerts.voltage = voltage_t::TOO_LOW;
            alerts_present = true;
        } else if (data.battery_voltage <= 10.5f) {
            alerts.voltage = voltage_t::LOW;
            alerts_present = true;
        } else if (data.battery_voltage > 12.6f) {
            alerts.voltage = voltage_t::HIGH;
            alerts_present = true;
        } else {
            // Acceptable range: 9.0V - 12.6V
            alerts.voltage = voltage_t::OK;
        }
        
        // Current classification
        if (data.load_current_drawn <= -15.0f) {
            alerts.current = current_t::CHARGE_TOO_HIGH;
            alerts_present = true;
        } else if (data.load_current_drawn <= -10.0f) {
            alerts.current = current_t::CHARGE_HIGH;
            alerts_present = true;
        } else if (data.load_current_drawn >= 25.0f) {
            alerts.current = current_t::TOO_HIGH;
            alerts_present = true;
        } else if (data.load_current_drawn >= 20.0f) {
            alerts.current = current_t::HIGH;
            alerts_present = true;
        } else {
            // Acceptable range: 0A - 20A for discharge
            // Acceptable range: 0A - 10A for recharge
            alerts.current = current_t::OK;
        }
        
        // Temperature classification
        if (data.inv_temp <= 0.0f) {
            alerts.temp = temp_t::TOO_LOW;
            alerts_present = true;
        } else if (data.inv_temp <= 10.0f) {
            alerts.temp = temp_t::LOW;
            alerts_present = true;
        } else if (data.inv_temp >= 60.0f) {
            alerts.temp = temp_t::TOO_HIGH;
            alerts_present = true;
        } else if (data.inv_temp >= 45.0f) {
            alerts.temp = temp_t::HIGH;
            alerts_present = true;
        } else {
            // Acceptable range: 0°C - 45°C
            alerts.temp = temp_t::OK;
        }
        
        // Humidity classification
        if (data.inv_hmdt <= 10.0f) {
            alerts.hmdt = hmdt_t::TOO_LOW;
            alerts_present = true;
        } else if (data.inv_hmdt <= 20.0f) {
            alerts.hmdt = hmdt_t::LOW;
            alerts_present = true;
        } else if (data.inv_hmdt >= 80.0f) {
            alerts.hmdt = hmdt_t::TOO_HIGH;
            alerts_present = true;
        } else if (data.inv_hmdt >= 70.0f) {
            alerts.hmdt = hmdt_t::HIGH;
            alerts_present = true;
        } else {
            // Acceptable range: 20% - 70%
            alerts.hmdt = hmdt_t::OK;
        }
        
        // Battery percentage classification
        if (data.battery_percent <= 5.0f) {
            alerts.batt = batt_t::BELOW_5;
            alerts_present = true;
        } else if (data.battery_percent <= 10.0f) {
            alerts.batt = batt_t::BELOW_10;
            alerts_present = true;
        } else if (data.battery_percent <= 15.0f) {
            alerts.batt = batt_t::BELOW_15;
            alerts_present = true;
        } else if (data.battery_percent <= 50.0f) {
            alerts.batt = batt_t::BELOW_50;
            alerts_present = true;
        } else {
            // Acceptable range: Above 50%
            alerts.batt = batt_t::OK;
        }

        return alerts_present;
    }

    void alert_handle_t::display_warnings_if_alerts() {
        voltage_alert_popup();
        current_alert_popup();
        temp_alert_popup();
        hmdt_alert_popup();
        batt_alert_popup();
    }
    
} // namespace display
