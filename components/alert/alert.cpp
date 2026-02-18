#include "alert.hpp"

#include <cstdio>
#include <cstring>


namespace display {

    alert_handle_t::alert_handle_t(const sys::data_t& data): data(data) {}

    enum class total_alerts_t : uint8_t {
        NONE = 0,
        
        VOLTAGE_HIGH,

        CURRENT_CHARGE_TOO_HIGH,
        CURRENT_CHARGE_HIGH,
        CURRENT_HIGH,
        CURRENT_TOO_HIGH,

        TEMP_TOO_LOW,
        TEMP_LOW,
        TEMP_HIGH,
        TEMP_TOO_HIGH,

        HMDT_TOO_LOW,
        HMDT_LOW,
        HMDT_HIGH,
        HMDT_TOO_HIGH,

        BATT_BELOW_20,
        BATT_BELOW_15,
        BATT_BELOW_10,
        BATT_BELOW_5
    };

    total_alerts_t last_active_alert = total_alerts_t::NONE;
    
    void alert_handle_t::voltage_alert_popup() {

        entry_t entry{};

        switch (alerts.voltage) {
        case voltage_t::HIGH:
            if (last_active_alert == total_alerts_t::VOLTAGE_HIGH) return;
            entry.severity = severity_t::WARNING;
            strncpy(entry.title, "VOLTAGE HIGH!", sizeof(entry.title) - 1);
            snprintf(entry.body, sizeof(entry.body) - 1,
                "%.1fV  threshold: 12.6V\nPossible overcharge.", data.battery_voltage);
            push_alert(entry);
            last_active_alert = total_alerts_t::VOLTAGE_HIGH;
            return;

        default:
            return;
        }
    }
    
    void alert_handle_t::current_alert_popup() {

        entry_t entry{};

        switch (alerts.current) {
        case current_t::CHARGE_TOO_HIGH:
            if (last_active_alert == total_alerts_t::CURRENT_CHARGE_TOO_HIGH) return;
            entry.severity = severity_t::CRITICAL;
            strncpy(entry.title, "CHARGE CURRENT TOO HIGH!", sizeof(entry.title) - 1);
            snprintf(entry.body, sizeof(entry.body) - 1,
                "%.1fA  threshold: -15.0A\nCharger overcurrent.\nCheck charger.", data.load_current_drawn);
            push_alert(entry);
            last_active_alert = total_alerts_t::CURRENT_CHARGE_TOO_HIGH;
            return;

        case current_t::CHARGE_HIGH:
            if (last_active_alert == total_alerts_t::CURRENT_CHARGE_HIGH) return;
            entry.severity = severity_t::WARNING;
            strncpy(entry.title, "CHARGE CURRENT HIGH!", sizeof(entry.title) - 1);
            snprintf(entry.body, sizeof(entry.body) - 1,
                "%.1fA  threshold: -10.0A\nCharger current elevated.", data.load_current_drawn);
            push_alert(entry);
            last_active_alert = total_alerts_t::CURRENT_CHARGE_HIGH;
            return;

        case current_t::HIGH:
            if (last_active_alert == total_alerts_t::CURRENT_HIGH) return;
            entry.severity = severity_t::WARNING;
            strncpy(entry.title, "LOAD CURRENT HIGH!", sizeof(entry.title) - 1);
            snprintf(entry.body, sizeof(entry.body) - 1,
                "%.1fA  threshold: 20.0A\nLoad approaching limit.", data.load_current_drawn);
            push_alert(entry);
            last_active_alert = total_alerts_t::CURRENT_HIGH;
            return;

        case current_t::TOO_HIGH:
            if (last_active_alert == total_alerts_t::CURRENT_TOO_HIGH) return;
            entry.severity = severity_t::CRITICAL;
            strncpy(entry.title, "LOAD CURRENT TOO HIGH!", sizeof(entry.title) - 1);
            snprintf(entry.body, sizeof(entry.body) - 1,
                "%.1fA  threshold: 25.0A\nLoad overcurrent.\nReduce load now.", data.load_current_drawn);
            push_alert(entry);
            last_active_alert = total_alerts_t::CURRENT_TOO_HIGH;
            return;

        default:
            return;
        }
    }
    
    void alert_handle_t::temp_alert_popup() {

        entry_t entry{};

        switch (alerts.temp) {
        case temp_t::TOO_LOW:
            if (last_active_alert == total_alerts_t::TEMP_TOO_LOW) return;
            entry.severity = severity_t::CRITICAL;
            strncpy(entry.title, "TEMPERATURE TOO LOW!", sizeof(entry.title) - 1);
            snprintf(entry.body, sizeof(entry.body) - 1,
                "%.1f°C  threshold: 0°C\nFreezing conditions.\nCheck environment.", data.inv_temp);
            push_alert(entry);
            last_active_alert = total_alerts_t::TEMP_TOO_LOW;
            return;

        case temp_t::LOW:
            if (last_active_alert == total_alerts_t::TEMP_LOW) return;
            entry.severity = severity_t::WARNING;
            strncpy(entry.title, "TEMPERATURE LOW!", sizeof(entry.title) - 1);
            snprintf(entry.body, sizeof(entry.body) - 1,
                "%.1f°C  threshold: 10°C\nCold conditions.", data.inv_temp);
            push_alert(entry);
            last_active_alert = total_alerts_t::TEMP_LOW;
            return;

        case temp_t::HIGH:
            if (last_active_alert == total_alerts_t::TEMP_HIGH) return;
            entry.severity = severity_t::WARNING;
            strncpy(entry.title, "TEMPERATURE HIGH!", sizeof(entry.title) - 1);
            snprintf(entry.body, sizeof(entry.body) - 1,
                "%.1f°C  threshold: 45°C\nTemperature elevated.", data.inv_temp);
            push_alert(entry);
            last_active_alert = total_alerts_t::TEMP_HIGH;
            return;

        case temp_t::TOO_HIGH:
            if (last_active_alert == total_alerts_t::TEMP_TOO_HIGH) return;
            entry.severity = severity_t::CRITICAL;
            strncpy(entry.title, "TEMPERATURE TOO HIGH!", sizeof(entry.title) - 1);
            snprintf(entry.body, sizeof(entry.body) - 1,
                "%.1f°C  threshold: 60°C\nThermal danger.\nCheck cooling.", data.inv_temp);
            push_alert(entry);
            last_active_alert = total_alerts_t::TEMP_TOO_HIGH;
            return;

        default:
            return;
        }
    }
    
    void alert_handle_t::hmdt_alert_popup() {

        entry_t entry{};

        switch (alerts.hmdt) {
        case hmdt_t::TOO_LOW:
            if (last_active_alert == total_alerts_t::HMDT_TOO_LOW) return;
            entry.severity = severity_t::CRITICAL;
            strncpy(entry.title, "HUMIDITY TOO LOW!", sizeof(entry.title) - 1);
            snprintf(entry.body, sizeof(entry.body) - 1,
                "%.1f%%  threshold: 10%%\nVery dry conditions.\nStatic risk.", data.inv_hmdt);
            push_alert(entry);
            last_active_alert = total_alerts_t::HMDT_TOO_LOW;
            return;

        case hmdt_t::LOW:
            if (last_active_alert == total_alerts_t::HMDT_LOW) return;
            entry.severity = severity_t::WARNING;
            strncpy(entry.title, "HUMIDITY LOW!", sizeof(entry.title) - 1);
            snprintf(entry.body, sizeof(entry.body) - 1,
                "%.1f%%  threshold: 20%%\nDry conditions.", data.inv_hmdt);
            push_alert(entry);
            last_active_alert = total_alerts_t::HMDT_LOW;
            return;

        case hmdt_t::HIGH:
            if (last_active_alert == total_alerts_t::HMDT_HIGH) return;
            entry.severity = severity_t::WARNING;
            strncpy(entry.title, "HUMIDITY HIGH!", sizeof(entry.title) - 1);
            snprintf(entry.body, sizeof(entry.body) - 1,
                "%.1f%%  threshold: 70%%\nHumidity elevated.", data.inv_hmdt);
            push_alert(entry);
            last_active_alert = total_alerts_t::HMDT_HIGH;
            return;

        case hmdt_t::TOO_HIGH:
            if (last_active_alert == total_alerts_t::HMDT_TOO_HIGH) return;
            entry.severity = severity_t::CRITICAL;
            strncpy(entry.title, "HUMIDITY TOO HIGH!", sizeof(entry.title) - 1);
            snprintf(entry.body, sizeof(entry.body) - 1,
                "%.1f%%  threshold: 80%%\nCondensation risk.\nCheck ventilation.", data.inv_hmdt);
            push_alert(entry);
            last_active_alert = total_alerts_t::HMDT_TOO_HIGH;
            return;

        default:
            return;
        }
    }
    
    void alert_handle_t::batt_alert_popup() {

        entry_t entry{};

        switch (alerts.batt) {
        case batt_t::BELOW_5:
            if (last_active_alert == total_alerts_t::BATT_BELOW_5) return;
            entry.severity = severity_t::CRITICAL;
            strncpy(entry.title, "BATTERY TOO LOW!", sizeof(entry.title) - 1);
            snprintf(entry.body, sizeof(entry.body) - 1,
                "%.1f%%  threshold: 5%%\nDevice near shutdown.\nCharge immediately.", data.battery_percent);
            push_alert(entry);
            last_active_alert = total_alerts_t::BATT_BELOW_5;
            return;

        case batt_t::BELOW_10:
            if (last_active_alert == total_alerts_t::BATT_BELOW_10) return;
            entry.severity = severity_t::WARNING;
            strncpy(entry.title, "BATTERY TOO LOW!", sizeof(entry.title) - 1);
            snprintf(entry.body, sizeof(entry.body) - 1,
                "%.1f%%  threshold: 10%%\nBattery extremely low.\ncharge immediately", data.battery_percent);
            push_alert(entry);
            last_active_alert = total_alerts_t::BATT_BELOW_10;
            return;

        case batt_t::BELOW_15:
            if (last_active_alert == total_alerts_t::BATT_BELOW_15) return;
            entry.severity = severity_t::WARNING;
            strncpy(entry.title, "BATTERY LOW!", sizeof(entry.title) - 1);
            snprintf(entry.body, sizeof(entry.body) - 1,
                "%.1f%%  threshold: 15%%\nRisk of damage to your device.", data.battery_percent);
            push_alert(entry);
            last_active_alert = total_alerts_t::BATT_BELOW_15;
            return;

        case batt_t::BELOW_20:
            if (last_active_alert == total_alerts_t::BATT_BELOW_20) return;
            entry.severity = severity_t::INFO;
            strncpy(entry.title, "BATTERY LOW!", sizeof(entry.title) - 1);
            snprintf(entry.body, sizeof(entry.body) - 1,
                "%.1f%%  threshold: 20%%\nPlease charge your device.", data.battery_percent);
            push_alert(entry);
            last_active_alert = total_alerts_t::BATT_BELOW_20;
            return;

        default:
            return;
        }
    }

    [[nodiscard]] bool alert_handle_t::check_set_alerts() {

        bool alerts_present = false;

        // Voltage classification
        if (data.battery_voltage > 12.6f) {
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
        } else if (data.battery_percent <= 20.0f) {
            alerts.batt = batt_t::BELOW_20;
            alerts_present = true;
        } else {
            // Acceptable range: Above 20%
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
