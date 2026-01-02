#ifndef _BLE_DATA_HPP_
#define _BLE_DATA_HPP_


#include <cstdint>

namespace ble {

    float get_temperature();

    float get_humidity();

    float get_voltage();

    float get_current();

    float get_power();

    float get_battery_soc();

    uint64_t get_runtime();

} // namespace ble


#endif // _BLE_DATA_HPP_