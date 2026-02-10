#ifndef _POWER_MONITOR_HPP_
#define _POWER_MONITOR_HPP_


#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "esp_adc/adc_continuous.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "driver/gpio.h"

#include <cstdint>
#include <cmath>
#include <array>


namespace adc {

    /**
    * @brief Measurement data structure
    */
    struct data_t {
        float current_avg;          // Average current in Amperes
        float voltage_avg;          // Average voltage in Volts
        float apparent_power;       // Apparent power in VA
        bool valid;                 // Data validity flag
    };

    /**
    * @brief ADC channel configuration
    */
    struct adc_channel_config_t {
        adc_channel_t channel;
        adc_unit_t unit;
    };

    /**
    * @brief Main power monitoring class.
    * Thread safe power monitoring using the ESP32's ADC continuous mode
    */
    class driver {
    public:
        driver();
        ~driver();

        /**
        * @brief Initialize ADC and GPIO for power monitoring
        * @param current_pin GPIO pin for current sensor (ADC1 channels only)
        * @param voltage_pin GPIO pin for voltage sensor (ADC1 channels only)
        * @return true if initialization successful, false otherwise
        */
        bool init(gpio_num_t current_pin, gpio_num_t voltage_pin);

        /**
        * @brief Get Average current reading
        * Non-blocking. Returns most recent calculated value
        * @return Average current in Amperes, NaN if data invalid
        */
        float get_current_avg();

        /**
        * @brief Get Average voltage reading
        * Non-blocking. Returns most recent calculated value
        * @return Average voltage in Volts, NaN if data invalid
        */
        float get_voltage_avg();

        /**
        * @brief Get apparent power
        * @return Apparent power in VA, NaN if data invalid
        */
        float get_apparent_power();

        /**
        * @brief Get complete measurement data
        * @param[out] data Struct reference to store measurement data
        * @return true if data is valid and fresh
        */
        bool get_measurement_data(data_t& data);

        /**
        * @brief Check if new data is available
        *@return true if fresh data available since last read
        */
        bool is_data_ready();

        /**
        * @brief Start continuous ADC sampling
        * @return true if started successfully
        */
        bool start();

        /**
        * @brief Stop continuous ADC sampling
        * @return true if stopped successfully
        */
        bool stop();

    private:
        float zero_current_offset_voltage;

        // ADC handles
        adc_continuous_handle_t adc_handle;
        adc_cali_handle_t cali_handle;

        SemaphoreHandle_t data_mutex;
        TaskHandle_t processing_task_handle;

        bool data_ready_flag;
        uint32_t sample_count;

        // Measurement data
        data_t measurement_data;

        // Channel configuration
        adc_channel_config_t current_channel;
        adc_channel_config_t voltage_channel;

        // Processing buffers
        static constexpr size_t MAX_BUFFER_SIZE = 128;
        std::array<float, MAX_BUFFER_SIZE> current_samples{};
        std::array<float, MAX_BUFFER_SIZE> voltage_samples{};

        // State flags
        bool initialized;
        bool running;

        // Private methods
        bool configure_adc_channels();
        void setup_calibration();
        adc_channel_config_t gpio_to_adc_channel(gpio_num_t pin);

        template <typename T>
        float calculate_average(const std::array<T, MAX_BUFFER_SIZE>& samples);
        
        void process_adc_data(uint8_t* buffer, uint32_t length);
        float raw_to_voltage(uint32_t raw, adc_cali_handle_t cali_handle);
        void update_measurements();

        // Static callback for ADC ISR
        static bool IRAM_ATTR adc_conv_done_callback(adc_continuous_handle_t handle, const adc_continuous_evt_data_t* edata, void* user_data);

        // Processing task
        static void adc_processing_task(void* arg);
    };

} // namespace adc

#endif // _POWER_MONITOR_HPP_