#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#include "ble_data.hpp"
#include "system.hpp"


extern QueueHandle_t get_data_queue(void);

namespace ble {
    
    static QueueHandle_t data_queue   = get_data_queue();
    static sys::data_t data           = {};

    // We don't have to check the return value of `xQueuePeek`
    // because we return the last cached data stored in data 
    float get_temperature(void) {
        xQueuePeek(data_queue, &data, 0);
        return data.inv_temp;
    }

    float get_humidity(void) {
        xQueuePeek(data_queue, &data, 0);
        return data.inv_hmdt;
    }

    float get_voltage(void) {
        xQueuePeek(data_queue, &data, 0);
        return data.battery_voltage;
    }

    float get_current(void) {
        xQueuePeek(data_queue, &data, 0);
        return data.load_current_drawn;
    }

    float get_power(void) {
        xQueuePeek(data_queue, &data, 0);
        return data.power_drawn;
    }

    float get_battery_soc(void) {
        xQueuePeek(data_queue, &data, 0);
        return data.battery_percent;
    }

    float get_runtime(void) {
        xQueuePeek(data_queue, &data, 0);
        return data.runtime_left_s;
    }

} // namespace ble