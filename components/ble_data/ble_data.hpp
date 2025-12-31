#ifndef _BLE_DATA_HPP_
#define _BLE_DATA_HPP_


namespace ble {

    float get_temperature(void);

    float get_humidity(void);

    float get_voltage(void);

    float get_current(void);

    float get_power(void);

    float get_battery_soc(void);

    float get_runtime(void);

} // namespace ble


#endif // _BLE_DATA_HPP_