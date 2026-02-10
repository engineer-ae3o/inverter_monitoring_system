#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "system.hpp"
#include "config.hpp"

#include "esp_system.h"
#include "esp_log.h"

#include <cstring>


namespace sys {

    const char* inv_status_to_string(inv_status_t status) {
        switch (status) {
            case inv_status_t::IDLE: return "IDLE";
            case inv_status_t::ACTIVE: return "ACTIVE";
            default: return "UNKNOWN";
        }
    }

    const char* batt_status_to_string(batt_status_t status) {
        switch (status) {
            case batt_status_t::IDLE: return "IDLE";
            case batt_status_t::DISCHARGING: return "IN USE";
            case batt_status_t::RECHARGING: return "CHARGING";
            default: return "UNKNOWN";
        }
    }

    bool calc_total_runtime_stats(const aht20_data_t& aht_data, const adc::data_t& power_data, data_t& final) {

        memset(&final, 0, sizeof(data_t));
        
        // Copy temperature and humidity values
        final.inv_temp = aht_data.temperature;
        final.inv_hmdt = aht_data.humidity;

        // Range validation for the temperature and humidity
        if (final.inv_temp > 85.0f || final.inv_temp < -40.0f) {
            return false;
        }
        if (final.inv_hmdt > 100.0f || final.inv_hmdt < 0.0f) {
            return false;
        }

        // Check to see if data from the ADC is valid
        if (!power_data.valid) {
            return false;
        }

        // Copy voltage, current and voltage values
        final.battery_voltage = power_data.voltage_avg;
        final.load_current_drawn = power_data.current_avg;
        final.power_drawn = power_data.apparent_power;

        // Range validation for the voltage and curent
        if (final.battery_voltage > 16.0f || final.battery_voltage < 0.0f) {
            return false;
        }
        if (final.load_current_drawn > 30.0f || final.load_current_drawn < -30.0f) {
            return false;
        }

        // Calculate battery percentage
        final.battery_percent = (final.battery_voltage - config::BATT_ZERO_PERCENT_VOLTAGE) /                               \
                                (config::BATT_MAX_PERCENT_VOLTAGE - config::BATT_ZERO_PERCENT_VOLTAGE) * 100.0f;
        
        // Clamp battery percent to known values if above or below the threshold for battery soc
        if (final.battery_percent > 100.0f) {
            final.battery_percent = 100.0f;
        } else if (final.battery_percent < 0.0f) {
            final.battery_percent = 0.0f;
        }
    
        // Get inverter status
        if (final.load_current_drawn >= config::INVERTER_ACTIVE_THRESHOLD) {
            final.inv_status = inv_status_t::ACTIVE;
        } else {
            final.inv_status = inv_status_t::IDLE;
        }

        // Get battery status
        if (final.load_current_drawn < config::BATTERY_RECHARGING_THRESHOLD) {
            final.batt_status = batt_status_t::RECHARGING;
        } else if (final.load_current_drawn > config::BATTERY_DISCHARGING_THRESHOLD) {
            final.batt_status = batt_status_t::DISCHARGING;
        } else {
            final.batt_status = batt_status_t::IDLE;
        }

        // Find charge time left if battery status is recharging
        if (final.batt_status == batt_status_t::RECHARGING) {
            float capacity_left_to_full_ah = config::BATTERY_CAPACITY_AH * ((100.0f - final.battery_percent) / 100.0f);
            float charge_current = -1.0f * final.load_current_drawn; // Make positive
            float charge_time_hrs = capacity_left_to_full_ah / charge_current;

            final.runtime_left_s = charge_time_hrs * 3600; // Convert to seconds
        }
        
        // Calculate estimated runtime left if battery is not recharging
        else if ((final.batt_status == batt_status_t::DISCHARGING || final.batt_status == batt_status_t::IDLE) && (final.load_current_drawn != 0.0f)) {
            float remaining_capacity_ah = config::BATTERY_CAPACITY_AH * (final.battery_percent / 100.0f);
            float runtime_hrs = remaining_capacity_ah / final.load_current_drawn;

            final.runtime_left_s = runtime_hrs * 3600; // Convert to seconds

            // Cap on how high the estimated runtime value can get: 7 days
            if (final.runtime_left_s > 86400 * 7) {
                final.runtime_left_s = 86400 * 7;
            }
        }
        // Assign value if load current is 0
        else if (final.load_current_drawn == 0.0f) {
            final.runtime_left_s = UINT64_MAX;
        }

        return true;
    }

    [[noreturn]] void handle_error(void) {
        ESP_LOGE("ERROR", "Non recoverable error occured. Rebooting system");
        vTaskDelay(pdMS_TO_TICKS(20)); // Delay to allow logs to flush
        esp_restart();
        while (1);
    }

} // namespace sys
