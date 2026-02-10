#ifndef _BLE_DATA_HPP_
#define _BLE_DATA_HPP_


#include <cstdint>

namespace ble {

    void ble_data_init(const QueueHandle_t& ble_data_queue);

    float get_temperature();

    float get_humidity();

    float get_voltage();

    float get_current();

    float get_power();

    float get_battery_soc();

    uint64_t get_runtime();

} // namespace ble


#endif // _BLE_DATA_HPP_