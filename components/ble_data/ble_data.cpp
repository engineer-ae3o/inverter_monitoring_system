#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#include "ble_data.hpp"
#include "system.hpp"

#include "esp_log.h"


namespace ble {
    
    static QueueHandle_t data_queue = nullptr;
    static sys::data_t data{};

    void ble_data_init(const QueueHandle_t& ble_data_queue) {
        data_queue = ble_data_queue;
        ASSERT(data_queue, "data_queue cannot be null");
    }

    // We don't have to check the return value of `xQueuePeek`
    // because we return the last cached data stored in data 
    float get_temperature() {
        xQueuePeek(data_queue, &data, 0);
        return data.inv_temp;
    }

    float get_humidity() {
        xQueuePeek(data_queue, &data, 0);
        return data.inv_hmdt;
    }

    float get_voltage() {
        xQueuePeek(data_queue, &data, 0);
        return data.battery_voltage;
    }

    float get_current() {
        xQueuePeek(data_queue, &data, 0);
        return data.load_current_drawn;
    }

    float get_power() {
        xQueuePeek(data_queue, &data, 0);
        return data.power_drawn;
    }

    float get_battery_soc() {
        xQueuePeek(data_queue, &data, 0);
        return data.battery_percent;
    }

    uint64_t get_runtime() {
        xQueuePeek(data_queue, &data, 0);
        return data.runtime_left_s;
    }

} // namespace ble
