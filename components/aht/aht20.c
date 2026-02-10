#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "aht20.h"

#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"


// Debug logging levels
#define AHT_LOG_LEVEL_INFO 3
#define AHT_LOG_LEVEL_WARN 2
#define AHT_LOG_LEVEL_ERROR 1
#define AHT_LOG_LEVEL_NONE 0

// Set the log level to any appropriate log level
#define AHT_DEBUG_LEVEL AHT_LOG_LEVEL_WARN
static const char* TAG = "AHT";

#if AHT_DEBUG_LEVEL == AHT_LOG_LEVEL_INFO
#define AHT_LOGE(...) ESP_LOGE(TAG, __VA_ARGS__)
#define AHT_LOGW(...) ESP_LOGW(TAG, __VA_ARGS__)
#define AHT_LOGI(...) ESP_LOGI(TAG, __VA_ARGS__)

#elif AHT_DEBUG_LEVEL == AHT_LOG_LEVEL_WARN
#define AHT_LOGE(...) ESP_LOGE(TAG, __VA_ARGS__)
#define AHT_LOGW(...) ESP_LOGW(TAG, __VA_ARGS__)
#define AHT_LOGI(...)

#elif AHT_DEBUG_LEVEL == AHT_LOG_LEVEL_ERROR
#define AHT_LOGE(...) ESP_LOGE(TAG, __VA_ARGS__)
#define AHT_LOGW(...)
#define AHT_LOGI(...)

#elif AHT_DEBUG_LEVEL == AHT_LOG_LEVEL_NONE
#define AHT_LOGE(...)
#define AHT_LOGW(...)
#define AHT_LOGI(...)
#endif


// AHT20 I2C address and commands
static const uint8_t AHT20_I2C_ADDRESS                  = 0x38;        ///< AHT20's I2C address
static const uint8_t AHT20_TRIGGER_CMD_1                = 0xAC;        ///< Command to trigger measurement
static const uint8_t AHT20_TRIGGER_CMD_2                = 0x33;        ///< First parameter to trigger measurement
static const uint8_t AHT20_TRIGGER_CMD_3                = 0x00;        ///< Second parameter to trigger measurement
static const uint8_t AHT20_CMD_SOFT_RESET               = 0xBA;        ///< Command to initiate a soft reset
static const uint8_t AHT20_TX_INIT_STATUS_BYTE          = 0x71;        ///< Command to receive the initialization status byte
static const uint8_t AHT20_INIT_CMD_1                   = 0xBE;        ///< Command to initialize the AHT20
static const uint8_t AHT20_INIT_CMD_2                   = 0x08;        ///< First parameter for initialization
static const uint8_t AHT20_INIT_CMD_3                   = 0x00;        ///< Second parameter for initialization

// Timing constants
static const uint8_t AHT20_MEASUREMENT_DELAY_MS         = 83;          ///< Measurement time of the AHT20
static const uint8_t AHT20_TIMEOUT_MS                   = 200;         ///< Timeout for sending and receiving data on the I2C bus
static const uint8_t AHT20_POLLING_DELAY_MS             = 2;           ///< Polling delay
static const uint8_t AHT20_READ_ATTEMPTS                = 4;           ///< Maximum number of times a read is attempted before returning an error
static const uint8_t AHT20_DELAY_TO_SETUP_MS            = 15;          ///< Delay for the AHT20 to setup before any reading can take place
static const uint8_t AHT20_DELAY_TO_DEFAULT_MS          = 40;          ///< Delay for the AHT20 to enter it's default state after powering up
static const uint8_t AHT20_BACKOFF_DELAY_MS             = 100;         ///< Backoff between retry attempts
static const uint8_t MAX_POLL_ATTEMPTS                  = 50;          ///< Max number of polling attempts before timing out
static const uint32_t AHT20_MINIMUM_READ_TIME_US        = 2000000;     ///< Minimum time between read attempts (2s)
static const uint32_t AHT20_CONVERSION_FACTOR           = (1 << 20);   ///< Conversion factor for the AHT20 (2^20)

// I2C configuration
static const uint32_t I2C_MASTER_FREQ_HZ                = 100000;      ///< I2C bus frequency
static const i2c_port_t I2C_MASTER_PORT                 = I2C_NUM_1;   ///< I2C bus

// I2C bus and device handle
static i2c_master_dev_handle_t dev_handle               = NULL;        ///< Device handle for the I2C bus being used
static i2c_master_bus_handle_t bus_handle               = NULL;        ///< I2C bus being used

#define GET_BIT(num, bit) (((num >> bit) & 1) ? 1 : 0)                 ///< To get a bit from the specified byte(s)


// AHT20 state
static SemaphoreHandle_t aht_mutex = NULL;
static bool is_initialized = false;
static gpio_num_t i2c_sda_pin = GPIO_NUM_NC;
static gpio_num_t i2c_scl_pin = GPIO_NUM_NC;
static int64_t last_read_time_us = 0;

static aht20_data_t last_data = {};


// Helpers
static inline void aht_cleanup(void);
static aht20_err_t aht_i2c_init(gpio_num_t sda, gpio_num_t scl);
static aht20_err_t poll_till_measurement_done(void);
static aht20_err_t checksum_valid(const uint8_t* data);

// Public API
aht20_err_t aht20_init(gpio_num_t sda, gpio_num_t scl) {

    if (sda < 0 || sda >= GPIO_NUM_MAX || scl < 0 || scl >= GPIO_NUM_MAX) {
        AHT_LOGE("Invalid I2C pins: SDA = GPIO_NUM_%d, SCL = GPIO_NUM_%d", (int)sda, (int)scl);
        return AHT_INVALID_ARGS;
    }

    aht_mutex = xSemaphoreCreateMutex();
    if (!aht_mutex) {
        AHT_LOGE("Failed to create aht mutex");
        return AHT_FAIL;
    }

    if (xSemaphoreTake(aht_mutex, pdMS_TO_TICKS(AHT20_TIMEOUT_MS)) != pdTRUE) {
        AHT_LOGE("Failed to take aht mutex");
        return AHT_TIMEOUT;
    }

    if (is_initialized) {
        AHT_LOGW("AHT20 already initialized");
        xSemaphoreGive(aht_mutex);
        return AHT_OK;
    }

    AHT_LOGI("Starting initialization");

    vTaskDelay(pdMS_TO_TICKS(AHT20_DELAY_TO_DEFAULT_MS));   // Delay for AHT20 to enter default state
    
    // Initializing the I2C bus to use for transmission to the AHT20
    if (aht_i2c_init(sda, scl) != AHT_OK) {
        AHT_LOGE("Failed to initialize I2C bus");
        xSemaphoreGive(aht_mutex);
        aht_cleanup();
        return AHT_FAIL;
    }

    uint8_t rx_init_status_byte = 0;

    // Sending init byte to get init message from the AHT20
    esp_err_t ret = i2c_master_transmit_receive(dev_handle, &AHT20_TX_INIT_STATUS_BYTE, sizeof(AHT20_TX_INIT_STATUS_BYTE),
                                                &rx_init_status_byte, sizeof(rx_init_status_byte), pdMS_TO_TICKS(AHT20_TIMEOUT_MS));
    if (ret != ESP_OK) {
        AHT_LOGE("Failed to send and receive status bytes: %s", esp_err_to_name(ret));
        xSemaphoreGive(aht_mutex);
        aht_cleanup();
        return AHT_FAIL;
    }

    // Checking third bit of the received byte to see if the AHT20 is initialized. For more details, refer to the datasheet
    if (GET_BIT(rx_init_status_byte, 3) == 1) {
        AHT_LOGI("AHT20 fully initialized");

        i2c_sda_pin = sda;
        i2c_scl_pin = scl;
        is_initialized = true;
        
        xSemaphoreGive(aht_mutex);
        return AHT_OK;
    }

    // Sending init message to the AHT20 if it has not been initialized
    const uint8_t tx_buffer[3] = { AHT20_INIT_CMD_1, AHT20_INIT_CMD_2, AHT20_INIT_CMD_3 };

    if (i2c_master_transmit(dev_handle, tx_buffer, sizeof(tx_buffer), pdMS_TO_TICKS(AHT20_TIMEOUT_MS)) != ESP_OK) {
        AHT_LOGE("AHT20 failed to initialize");
        xSemaphoreGive(aht_mutex);
        aht_cleanup();
        return AHT_FAIL;
    }

    // Sending the command and checking the right bit to see if the AHT20 has indeed been initialized
    ret = i2c_master_transmit_receive(dev_handle, &AHT20_TX_INIT_STATUS_BYTE, sizeof(AHT20_TX_INIT_STATUS_BYTE),
                                      &rx_init_status_byte, sizeof(rx_init_status_byte), pdMS_TO_TICKS(AHT20_TIMEOUT_MS));
    if (ret != ESP_OK) {
        AHT_LOGE("Failed to send and receive status bytes");
        xSemaphoreGive(aht_mutex);
        aht_cleanup();
        return AHT_FAIL;
    }

    // Checking the third bit of the received byte to see if the AHT20 has been initialized
    if (GET_BIT(rx_init_status_byte, 3) == 0) {
        AHT_LOGE("AHT20 failed to be initialized");
        xSemaphoreGive(aht_mutex);
        aht_cleanup();
        return AHT_FAIL;
    }

    vTaskDelay(pdMS_TO_TICKS(AHT20_DELAY_TO_SETUP_MS));  // Delay to allow the AHT20 to fully setup before any reading can take place

    AHT_LOGI("AHT fully initialized");

    i2c_sda_pin = sda;
    i2c_scl_pin = scl;

    is_initialized = true;

    xSemaphoreGive(aht_mutex);

    return AHT_OK;
}

aht20_err_t aht20_deinit(void) {

    if (xSemaphoreTake(aht_mutex, pdMS_TO_TICKS(AHT20_TIMEOUT_MS)) != pdTRUE) {
        AHT_LOGE("Failed to take aht mutex");
        return AHT_TIMEOUT;
    }

    if (!is_initialized) {
        AHT_LOGW("AHT20 already deinitialized");
        return AHT_OK;
    }
    xSemaphoreGive(aht_mutex);

    aht_cleanup();
    is_initialized = false;

    return AHT_OK;
}

aht20_err_t aht20_read(aht20_data_t* data) {
    
    if (xSemaphoreTake(aht_mutex, pdMS_TO_TICKS(AHT20_TIMEOUT_MS)) != pdTRUE) {
        AHT_LOGE("Failed to take aht mutex");
        return AHT_TIMEOUT;
    }

    // Checking for right initialization state
    if (!is_initialized) {
        AHT_LOGE("AHT not yet initialized");
        xSemaphoreGive(aht_mutex);
        return AHT_INVALID_STATE;
    }
    
    // Enforcing minimum reading intervals for the AHT20
    if ((last_read_time_us != 0) && ((esp_timer_get_time() - last_read_time_us) < AHT20_MINIMUM_READ_TIME_US)) {
        AHT_LOGE("aht20_read called before minimum read time interval");
        xSemaphoreGive(aht_mutex);
        return AHT_INVALID_STATE;
    }

    // Buffer which contains the values to trigger a read from the AHT20
    const uint8_t tx_trigger_read_buffer[] = { AHT20_TRIGGER_CMD_1, AHT20_TRIGGER_CMD_2, AHT20_TRIGGER_CMD_3 };

    // Buffer to store measurements from the AHT20
    uint8_t rx_data_buffer[7] = {};

    esp_err_t ret = ESP_OK;
    aht20_err_t rc = AHT_OK;

    for (uint8_t attempt = 0; attempt < AHT20_READ_ATTEMPTS; attempt++) {

        AHT_LOGI("Attempt #%u. Starting read", attempt+1);

        ret = i2c_master_transmit(dev_handle, tx_trigger_read_buffer,
                                  sizeof(tx_trigger_read_buffer), pdMS_TO_TICKS(AHT20_TIMEOUT_MS));
        if (ret != ESP_OK) {
            AHT_LOGW("Failed to transmit measurement triggering sequence");
            vTaskDelay(pdMS_TO_TICKS(AHT20_BACKOFF_DELAY_MS));
            continue;
        }

        // Give mutex before blocking
        xSemaphoreGive(aht_mutex);
        
        // Delay to allow the AHT20 take measurements
        vTaskDelay(pdMS_TO_TICKS(AHT20_MEASUREMENT_DELAY_MS));

        // Retake mutex to continue reading
        if (xSemaphoreTake(aht_mutex, pdMS_TO_TICKS(AHT20_TIMEOUT_MS)) != pdTRUE) {
            AHT_LOGE("Failed to take aht mutex");
            return AHT_TIMEOUT;
        }

        // Poll till the AHT20's measurement is complete
        rc = poll_till_measurement_done();
        if (rc != AHT_OK) {
            AHT_LOGW("Failure during polling");
            vTaskDelay(pdMS_TO_TICKS(AHT20_BACKOFF_DELAY_MS));
            continue;
        }

        ret = i2c_master_receive(dev_handle, rx_data_buffer, sizeof(rx_data_buffer), pdMS_TO_TICKS(AHT20_TIMEOUT_MS));
        if (ret != ESP_OK) {
            AHT_LOGW("Failed to receive measurement data");
            vTaskDelay(pdMS_TO_TICKS(AHT20_BACKOFF_DELAY_MS));
            continue;
        }

        rc = checksum_valid(rx_data_buffer);
        if (rc != AHT_OK) {
            AHT_LOGW("AHT20 sensor data gotten corrupted");
            vTaskDelay(pdMS_TO_TICKS(AHT20_BACKOFF_DELAY_MS));
            continue;
        }

        const uint32_t raw_hmdt = ((uint32_t)rx_data_buffer[1] << 12) | ((uint32_t)rx_data_buffer[2] << 4) | (((uint32_t)rx_data_buffer[3] >> 4) & 0x0F);
        const uint32_t raw_temp = (((uint32_t)rx_data_buffer[3] & 0x0F) << 16) | ((uint32_t)rx_data_buffer[4] << 8) | ((uint32_t)rx_data_buffer[5]);

        last_data.humidity = ((float)raw_hmdt / AHT20_CONVERSION_FACTOR) * 100.0f;
        last_data.temperature = ((float)raw_temp / AHT20_CONVERSION_FACTOR) * 200.0f - 50.0f;

        *data = last_data;

        AHT_LOGI("Read finished. Temperature = %.2fC; Humidity = %.2f%%.", data->temperature, data->humidity);

        last_read_time_us = esp_timer_get_time();

        xSemaphoreGive(aht_mutex);
        return AHT_OK;
    }

    // If we reach here, all read attempts failed
    AHT_LOGE("All #%u read attempts failed. Returning AHT_FAIL", AHT20_READ_ATTEMPTS);
    xSemaphoreGive(aht_mutex);

    return AHT_FAIL;
}

float get_temperature(void) {

    if (xSemaphoreTake(aht_mutex, pdMS_TO_TICKS(AHT20_TIMEOUT_MS)) != pdTRUE) {
        AHT_LOGE("Failed to take aht mutex");
        return AHT_TIMEOUT;
    }
    
    float temp = last_data.temperature;
    xSemaphoreGive(aht_mutex);

    return temp;
}

float get_humidity(void) {

    if (xSemaphoreTake(aht_mutex, pdMS_TO_TICKS(AHT20_TIMEOUT_MS)) != pdTRUE) {
        AHT_LOGE("Failed to take aht mutex");
        return AHT_TIMEOUT;
    }

    float hmdt = last_data.humidity;
    xSemaphoreGive(aht_mutex);

    return hmdt;
}

aht20_err_t aht20_soft_reset(void) {

    if (xSemaphoreTake(aht_mutex, pdMS_TO_TICKS(AHT20_TIMEOUT_MS)) != pdTRUE) {
        AHT_LOGE("Failed to take aht mutex");
        return AHT_TIMEOUT;
    }

    // Checking to see if the AHT20 has been initialized or not
    if (!is_initialized) {
        AHT_LOGW("AHT already in an uninitialized state");
        xSemaphoreGive(aht_mutex);
        return AHT_INVALID_STATE;
    }
    
    esp_err_t ret = i2c_master_transmit(dev_handle, &AHT20_CMD_SOFT_RESET,
                                        sizeof(AHT20_CMD_SOFT_RESET), pdMS_TO_TICKS(AHT20_TIMEOUT_MS));
    if (ret != ESP_OK) {
        AHT_LOGE("Failed to transmit soft reset command");
        xSemaphoreGive(aht_mutex);
        return AHT_FAIL;
    }
    is_initialized = false;

    xSemaphoreGive(aht_mutex);

    vTaskDelay(pdMS_TO_TICKS(AHT20_TIMEOUT_MS));  // Delay to allow the AHT20 fully reset

    return AHT_OK;
}

const char* aht_err_to_string(const aht20_err_t err) {
    switch (err) {
        case AHT_OK: return "AHT_OK";
        case AHT_FAIL: return "AHT_FAIL";
        case AHT_TIMEOUT: return "AHT_TIMEOUT";
        case AHT_INVALID_ARGS: return "AHT_INVALID_ARGS";
        case AHT_INVALID_STATE: return "AHT_INVALID_STATE";
        case AHT_CHS_FAIL: return "AHT_CHS_FAIL";
        default: return "INVALID ERROR CODE";
    }
}

// Static helpers
static inline void aht_cleanup(void) {

    if (dev_handle) {
        i2c_master_bus_rm_device(dev_handle);
        dev_handle = NULL;
    }

    if (bus_handle) {
        i2c_del_master_bus(bus_handle);
        bus_handle = NULL;
    }

    if (i2c_sda_pin != GPIO_NUM_NC) {
        gpio_reset_pin(i2c_sda_pin);
        i2c_sda_pin = GPIO_NUM_NC;
    }
    
    if (i2c_scl_pin != GPIO_NUM_NC) {
        gpio_reset_pin(i2c_scl_pin);
        i2c_scl_pin = GPIO_NUM_NC;
    }
    
    if (aht_mutex) {
        vSemaphoreDelete(aht_mutex);
        aht_mutex = NULL;
    }
}

static aht20_err_t aht_i2c_init(gpio_num_t sda, gpio_num_t scl) {

    const i2c_master_bus_config_t i2c_mst_config = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = I2C_MASTER_PORT,
        .scl_io_num = scl,
        .sda_io_num = sda,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true
    };
    
    if (i2c_new_master_bus(&i2c_mst_config, &bus_handle) != ESP_OK) {
        AHT_LOGE("Failed to initialize I2C master bus");
        return AHT_FAIL;
    }

    const i2c_device_config_t dev_config = {
        .scl_speed_hz = I2C_MASTER_FREQ_HZ,
        .device_address = AHT20_I2C_ADDRESS,
        .dev_addr_length = I2C_ADDR_BIT_LEN_7
    };

    if (i2c_master_bus_add_device(bus_handle, &dev_config, &dev_handle) != ESP_OK) {
        AHT_LOGE("Failed to create I2C master device");
        return AHT_FAIL;
    }
    
    return AHT_OK;
}

static aht20_err_t poll_till_measurement_done(void) {

    if (!dev_handle) {
        AHT_LOGE("Invalid device handle");
        return AHT_INVALID_ARGS;
    }

    uint8_t rx_status_byte = 0;

    for (int attempts = 0; attempts < MAX_POLL_ATTEMPTS; attempts++) {

        if (i2c_master_receive(dev_handle, &rx_status_byte, sizeof(rx_status_byte), pdMS_TO_TICKS(AHT20_TIMEOUT_MS)) != ESP_OK) {
            AHT_LOGW("Failed to receive status byte. Trying again");
            vTaskDelay(pdMS_TO_TICKS(AHT20_POLLING_DELAY_MS));  // Small delay before trying again
            continue;
        }

        if (GET_BIT(rx_status_byte, 7) == 0) {
            AHT_LOGI("Measurement completed");
            return AHT_OK;
        }

        AHT_LOGW("AHT20 not done taking measurements");

        vTaskDelay(pdMS_TO_TICKS(AHT20_POLLING_DELAY_MS));  // Small delay before trying again
    }

    return AHT_TIMEOUT;  // If we reach here, it means all polling attempts failed; so we return with a timeout
}

static aht20_err_t checksum_valid(const uint8_t* data) {

    if (!data) {
        AHT_LOGE("Invalid parameter to checksum function");
        return AHT_INVALID_ARGS;
    }

    // CRC8 checksum algorithm
    uint8_t checksum = 0xFF;
    for (int i = 0; i < 6; i++) {
        checksum ^= data[i];
        for (int j = 0; j < 8; j++) {
            if (checksum & 0x80) {
                checksum <<= 1;
                checksum ^= 0x31;
            } else {
                checksum <<= 1;
            }
        }
    }
    
    if (checksum == data[6]) {
        AHT_LOGI("Checksum verified");
        return AHT_OK;
    }

    AHT_LOGE("Checksum verification failed. Expected value = %d; Value gotten = %d", data[6], checksum);

    return AHT_CHS_FAIL;
}
