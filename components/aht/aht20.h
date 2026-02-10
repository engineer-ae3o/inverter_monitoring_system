#ifndef _AHT20_H_
#define _AHT20_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "driver/gpio.h"

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief Data stucture to hold the latest AHT20 measurement
 */
typedef struct {
    float temperature;  // Temperature in degree Celsius
    float humidity;     // Relative Humidity in percent
} aht20_data_t;

/**
 * @brief Container for AHT20 error codes
 */
typedef enum {
    AHT_OK = 0,
    AHT_FAIL,
    AHT_TIMEOUT,
    AHT_INVALID_ARGS,
    AHT_INVALID_STATE,
    AHT_CHS_FAIL
} aht20_err_t;

/**
 * @brief Initialize the AHT20 on the specified GPIO I2C pins
 * 
 * @param pin GPIO pins for SDA and SCL
 * 
 * @return AHT_OK on success, AHT_FAIL on I2C error, AHT_INVALID_ARGS if pins are invalid
 */
aht20_err_t aht20_init(gpio_num_t sda, gpio_num_t scl);

/**
 * @brief Deinitialize the AHT20 if already initialized
 * 
 * @return Returns AHT_OK upon success or if the sensor is already in an uninitialized state, AHT_FAIL upon failure
 * 
 */
aht20_err_t aht20_deinit(void);

/**
 * @brief Trigger a read from the AHT20 sensor.
 * This is a blocking read. It will perform up to AHT20_MAX_RETRIES
 * attempts (configurable) before returning an error.
 * 
 * @param[out] data Pointer to destination struct to receive values on success.
 * 
 * @return AHT_OK on success, AHT_FAIL on I2C error, AHT_TIMEOUT on timeout, AHT_INVALID_ARGS if args are invalid, AHT20_INVALID_STATE if driver not initialized
 * 
 */
aht20_err_t aht20_read(aht20_data_t* data);

/**
 * @brief Retrieves the last cached temperature value. Does not perform any reading,
 * hence, aht20_read must be called before it can be used, otherwise it returns stale data.
 *
 * @return Temperature as a float.
 */
float get_temperature(void);

/**
 * @brief Retrieves the last cached humidity value. Does not perform any reading, hence,
 * aht20_read must be called before it can be used, otherwise it returns stale data.
 * 
 * @return Humidity as a float.
 */
float get_humidity(void);

/**
 * @brief Issues a soft reset on the AHT20 sensor. Leaves the sensor in an uninitialized state.
 * 
 * @return AHT_OK on success, AHT_INVALID_ARGS on failure
 */
aht20_err_t aht20_soft_reset(void);

/**
 * @brief Converts corresponding error code to a string
 * 
 * @return Returns a string for the corresponding error code
 */
const char* aht_err_to_string(const aht20_err_t err);

#ifdef __cplusplus
}
#endif

#endif // _AHT20_H_