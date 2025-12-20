#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#include "ble_data.hpp"
#include "system.hpp"


extern QueueHandle_t get_data_queue(void);

namespace ble {
    
    static QueueHandle_t data_queue   = get_data_queue();
    sys::data_t data                  = {};

    const float get_temperature(void) {

    }

    const float get_humidity(void) {

    }

    const float get_voltage(void) {

    }

    const float get_current(void) {

    }

    const float get_power(void) {

    }

    const float get_battery_soc(void) {

    }

    const float get_runtime(void) {

    }


} // namespace ble
