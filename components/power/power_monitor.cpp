#include "power_monitor.hpp"

#include "esp_log.h"

#include <cstring>


// Debug logging levels
#define ADC_LOG_LEVEL_INFO 3
#define ADC_LOG_LEVEL_WARN 2
#define ADC_LOG_LEVEL_ERROR 1
#define ADC_LOG_LEVEL_NONE 0

// Set the log level to any appropriate log level
#define ADC_LOG_LEVEL ADC_LOG_LEVEL_WARN
static constexpr const char* TAG = "ADC";

#if ADC_LOG_LEVEL == ADC_LOG_LEVEL_INFO
#define ADC_LOGE(...) ESP_LOGE(TAG, __VA_ARGS__)
#define ADC_LOGW(...) ESP_LOGW(TAG, __VA_ARGS__)
#define ADC_LOGI(...) ESP_LOGI(TAG, __VA_ARGS__)

#elif ADC_LOG_LEVEL == ADC_LOG_LEVEL_WARN
#define ADC_LOGE(...) ESP_LOGE(TAG, __VA_ARGS__)
#define ADC_LOGW(...) ESP_LOGW(TAG, __VA_ARGS__)
#define ADC_LOGI(...)

#elif ADC_LOG_LEVEL == ADC_LOG_LEVEL_ERROR
#define ADC_LOGE(...) ESP_LOGE(TAG, __VA_ARGS__)
#define ADC_LOGW(...)
#define ADC_LOGI(...)

#elif ADC_LOG_LEVEL == ADC_LOG_LEVEL_NONE
#define ADC_LOGE(...)
#define ADC_LOGW(...)
#define ADC_LOGI(...)
#endif


namespace adc {

    // Configuration constants
    static constexpr uint16_t ADC_SAMPLE_RATE_HZ                = 20000;      // (20,000Hz / 2) per channel
    static constexpr uint16_t ADC_FRAME_SIZE                    = 128;        // DMA buffer size (power of 2)
    static constexpr uint8_t TIMEOUT_MS                         = 20;         // Timeout 

    // Sensor calibration constants
    static constexpr float ACS712_20A_SENSITIVITY               = 0.1;        // 100mV/A
    static constexpr float CURRENT_OFFSET_VOLTAGE               = 2.25;       // Volatage reading at 0A
    static constexpr float VOLTAGE_DIVIDER_RATIO                = 5;          // 1:5 divider
    static constexpr uint8_t TIMES_TO_MEASURE_ACS_OFFSET        = 25;         // Number of times to measure ACS zero current offset voltage
    static constexpr uint16_t ADC_RESOLUTION                    = 4096;       // 12 bits (2^12)
    
    // Task context
    static constexpr uint8_t PROC_TASK_PRIORITY = 8;
    static constexpr uint8_t PROC_TASK_CORE = 0;
    static constexpr uint16_t PROC_TASK_STACK_SIZE = 3072;
    
    driver::driver(): zero_current_offset_voltage(0), adc_handle(nullptr), cali_handle(nullptr), data_mutex(nullptr), processing_task_handle(nullptr),
                      data_ready_flag(false), sample_count(0), measurement_data{}, current_channel{},
                      voltage_channel{}, initialized(false), running(false) {}

    driver::~driver() {
        stop();
        if (adc_handle) {
            adc_continuous_flush_pool(adc_handle);
            adc_continuous_deinit(adc_handle);
            adc_handle = nullptr;
        }
        if (cali_handle) {
            adc_cali_delete_scheme_line_fitting(cali_handle);
            cali_handle = nullptr;
        }
        if (data_mutex) {
            vSemaphoreDelete(data_mutex);
            data_mutex = nullptr;
        }
        if (processing_task_handle) {
            vTaskDelete(processing_task_handle);
            processing_task_handle = nullptr;
        }
    }

    bool driver::init(gpio_num_t current_pin, gpio_num_t voltage_pin) {

        if (initialized) {
            ADC_LOGW("Already initialized");
            return true;
        }

        // Convert GPIO pins to ADC channels
        current_channel = gpio_to_adc_channel(current_pin);
        voltage_channel = gpio_to_adc_channel(voltage_pin);

        // Configure ADC continuous mode
        if (!configure_adc_channels()) {
            ADC_LOGE("Failed to configure ADC");
            return false;
        }

        // Setup calibration
        setup_calibration();

        // Create mutex
        data_mutex = xSemaphoreCreateMutex();
        if (!data_mutex) {
            ADC_LOGE("Failed to create mutex");
            return false;
        }

        // Create processing task
        BaseType_t ret = xTaskCreatePinnedToCore(adc_processing_task, "adc_processing_task", PROC_TASK_STACK_SIZE, 
                                                 this, PROC_TASK_PRIORITY, &processing_task_handle, PROC_TASK_CORE);
        if (ret != pdPASS) {
            ADC_LOGE("Failed to create processing task");
            return false;
        }

        initialized = true;

        ADC_LOGI("Initialized successfully");

        return true;
    }

    bool driver::configure_adc_channels() {

        // ADC continuous mode configuration
        constexpr adc_continuous_handle_cfg_t adc_config = {
            .max_store_buf_size = ADC_FRAME_SIZE * 4,
            .conv_frame_size = ADC_FRAME_SIZE,
            .flags = { .flush_pool = 1 }
        };

        esp_err_t ret = adc_continuous_new_handle(&adc_config, &adc_handle);
        if (ret != ESP_OK) {
            ADC_LOGE("ADC init failed: %s", esp_err_to_name(ret));
            return false;
        }

        // Configure channels
        adc_digi_pattern_config_t adc_pattern[2] = {
            { .atten = ADC_ATTEN_DB_12, .channel = current_channel.channel, .unit = current_channel.unit, .bit_width = ADC_BITWIDTH_12 },
            { .atten = ADC_ATTEN_DB_12, .channel = voltage_channel.channel, .unit = voltage_channel.unit, .bit_width = ADC_BITWIDTH_12 }
        };

        adc_continuous_config_t dig_cfg = {
            .pattern_num = 2,
            .adc_pattern = adc_pattern,
            .sample_freq_hz = ADC_SAMPLE_RATE_HZ,
            .conv_mode = ADC_CONV_SINGLE_UNIT_1,
            .format = ADC_DIGI_OUTPUT_FORMAT_TYPE1
        };

        ret = adc_continuous_config(adc_handle, &dig_cfg);
        if (ret != ESP_OK) {
            ADC_LOGE("ADC config failed: %s", esp_err_to_name(ret));
            return false;
        }

        // Register callback
        constexpr adc_continuous_evt_cbs_t cbs = {
            .on_conv_done = adc_conv_done_callback,
            .on_pool_ovf = nullptr
        };

        ret = adc_continuous_register_event_callbacks(adc_handle, &cbs, this);
        if (ret != ESP_OK) {
            ADC_LOGE("Callback registration failed: %s", esp_err_to_name(ret));
            return false;
        }

        return true;
    }

    void driver::setup_calibration() {

        // Setup calibration
        constexpr adc_cali_line_fitting_config_t cali_config = {
            .unit_id = ADC_UNIT_1,
            .atten = ADC_ATTEN_DB_12,
            .bitwidth = ADC_BITWIDTH_12,
            .default_vref = 1086 // default eFuse value gotten from running 'espefuse.py --port <your esp32's port> adc_info'
        };
        
        if (adc_cali_create_scheme_line_fitting(&cali_config, &cali_handle) != ESP_OK) {
            ADC_LOGW("Calibration failed, using default");
            cali_handle = nullptr;
        }
    }
    
    adc_channel_config_t driver::gpio_to_adc_channel(gpio_num_t pin) {
        
        adc_channel_config_t config{};
        adc_continuous_io_to_channel(static_cast<int>(pin), &config.unit, &config.channel);

        return config;
    }

    bool driver::start() {

        if (!initialized) {
            ADC_LOGE("ADC not yet initialized");
            return false;
        }

        if (running) {
            ADC_LOGW("ADC already running");
            return true;
        }

        esp_err_t ret = adc_continuous_start(adc_handle);
        if (ret != ESP_OK) {
            ADC_LOGE("Failed to start ADC: %s", esp_err_to_name(ret));
            return false;
        }

        running = true;
        ADC_LOGI("Started ADC sampling");

        return true;
    }

    bool driver::stop() {

        if (!initialized) {
            ADC_LOGE("ADC not yet initialized");
            return false;
        }

        if (!running) {
            ADC_LOGW("ADC not running");
            return true;
        }

        esp_err_t ret = adc_continuous_stop(adc_handle);
        if (ret != ESP_OK) {
            ADC_LOGE("Failed to stop ADC: %s", esp_err_to_name(ret));
            return false;
        }

        running = false;
        ADC_LOGI("Stopped ADC sampling");

        return true;
    }

    bool driver::adc_conv_done_callback(adc_continuous_handle_t handle, const adc_continuous_evt_data_t* edata, void* user_data) {
        auto driver = static_cast<adc::driver*>(user_data);
        if (!driver || !driver->processing_task_handle) return false;
        BaseType_t higher_priority_task_woken = pdFALSE;
        vTaskNotifyGiveFromISR(driver->processing_task_handle, &higher_priority_task_woken);
        return higher_priority_task_woken == pdTRUE;
    }

    void driver::adc_processing_task(void* arg) {

        auto driver = static_cast<adc::driver*>(arg);
        uint8_t result[ADC_FRAME_SIZE] = {};
        uint32_t out_length = 0;

        // Calculate the ACS712 zero current offset voltage at startup
        {
            if (!driver->running) driver->start();

            std::array<float, TIMES_TO_MEASURE_ACS_OFFSET> current{};

            for (uint8_t times = 0; times < TIMES_TO_MEASURE_ACS_OFFSET; times++) {
                // Block till notification received from ISR
                ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

                esp_err_t ret = adc_continuous_read(driver->adc_handle, result, ADC_FRAME_SIZE, &out_length, TIMEOUT_MS);
                if (ret == ESP_OK && out_length > 0) {
                    auto adc_raw_data_output = reinterpret_cast<adc_digi_output_data_t*>(result);
                    uint16_t num_samples = out_length / sizeof(adc_digi_output_data_t);
                    uint16_t current_idx = 0;

                    for (size_t i = 0; (i < num_samples) && (i < MAX_BUFFER_SIZE); i++) {
                        if (adc_raw_data_output[i].type1.channel == driver->current_channel.channel) {
                            driver->current_samples[current_idx++] = driver->raw_to_voltage(adc_raw_data_output[i].type1.data, driver->cali_handle);
                        }
                    }
                    current[times] = driver->calculate_average(driver->current_samples);

                    driver->current_samples.fill(0.0f);
                }
            }
            float current_avg = 0;
            for (size_t i = 0; i < TIMES_TO_MEASURE_ACS_OFFSET; i++) {
                current_avg += current[i];
            }
            current_avg /= TIMES_TO_MEASURE_ACS_OFFSET;

            driver->zero_current_offset_voltage = (current_avg > 1.5f && current_avg < 3.0f) ? current_avg : CURRENT_OFFSET_VOLTAGE;
            ADC_LOGI("Zero current offset voltage = %.3fV", driver->zero_current_offset_voltage);
        }

        while (1) {
            // Block till notification received from ISR
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

            // Read ADC data
            esp_err_t ret = adc_continuous_read(driver->adc_handle, result, ADC_FRAME_SIZE, &out_length, TIMEOUT_MS);
            if (ret == ESP_OK && out_length > 0) {
                driver->process_adc_data(result, out_length);
            }
        }
    }

    void driver::process_adc_data(uint8_t* buffer, uint32_t length) {

        auto adc_raw_data_output = reinterpret_cast<adc_digi_output_data_t*>(buffer);
        uint16_t num_samples = length / sizeof(adc_digi_output_data_t);
        size_t voltage_idx = 0, current_idx = 0;

        for (size_t i = 0; (i < num_samples) && (i < MAX_BUFFER_SIZE); i++) {
            if (adc_raw_data_output[i].type1.channel == current_channel.channel) {
                current_samples[current_idx++] = raw_to_voltage(adc_raw_data_output[i].type1.data, cali_handle);
            } else if (adc_raw_data_output[i].type1.channel == voltage_channel.channel) {
                voltage_samples[voltage_idx++] = raw_to_voltage(adc_raw_data_output[i].type1.data, cali_handle);
            }
        }

        // Use sample count with lower count
        sample_count = (current_idx < voltage_idx) ? current_idx : voltage_idx;
        update_measurements();
        sample_count = 0;
    }

    float driver::raw_to_voltage(uint32_t raw, adc_cali_handle_t cali_handle) {

        int voltage_mv = 0;

        if (cali_handle) {
            adc_cali_raw_to_voltage(cali_handle, raw, &voltage_mv);
        } else {
            // Fallback: linear conversion
            voltage_mv = (raw * 3300) / ADC_RESOLUTION;
        }

        return static_cast<float>(voltage_mv) / 1000.0f;
    }

    template <typename T>
    float driver::calculate_average(const std::array<T, MAX_BUFFER_SIZE>& samples) {

        float sum = 0.0f;
        for (size_t i = 0; i < sample_count; i++) {
            sum += samples[i];
        }

        return sum / sample_count;
    }

    void driver::update_measurements() {

        // Calculate voltage
        float voltage = calculate_average(voltage_samples);
        voltage *= VOLTAGE_DIVIDER_RATIO;

        // Calculate current
        float current = calculate_average(current_samples);
        current -= zero_current_offset_voltage;
        current /= ACS712_20A_SENSITIVITY;

        // Update shared data
        if (xSemaphoreTake(data_mutex, pdMS_TO_TICKS(TIMEOUT_MS)) == pdTRUE) {
            measurement_data.current_avg = current;
            measurement_data.voltage_avg = voltage;
            measurement_data.apparent_power = voltage * current;
            measurement_data.valid = true;
            data_ready_flag = true;
            xSemaphoreGive(data_mutex);
        }

        // Clear arrays for next cycle
        current_samples.fill(0.0f);
        voltage_samples.fill(0.0f);
    }

    float driver::get_voltage_avg() {

        float value = NAN;

        if (xSemaphoreTake(data_mutex, pdMS_TO_TICKS(TIMEOUT_MS)) == pdTRUE) {
            if(measurement_data.valid) {
                value = measurement_data.voltage_avg;
            }
            xSemaphoreGive(data_mutex);
        }
        return value;
    }

    float driver::get_current_avg() {

        float value = NAN;

        if (xSemaphoreTake(data_mutex, pdMS_TO_TICKS(TIMEOUT_MS)) == pdTRUE) {
            if(measurement_data.valid) {
                value = measurement_data.current_avg;
            }
            xSemaphoreGive(data_mutex);
        }
        return value;
    }

    float driver::get_apparent_power() {

        float value = NAN;

        if (xSemaphoreTake(data_mutex, pdMS_TO_TICKS(TIMEOUT_MS)) == pdTRUE) {
            if(measurement_data.valid) {
                value = measurement_data.apparent_power;
            }
            xSemaphoreGive(data_mutex);
        }
        return value;
    }

    bool driver::get_measurement_data(data_t& data) {

        bool success = false;

        if (xSemaphoreTake(data_mutex, pdMS_TO_TICKS(TIMEOUT_MS)) == pdTRUE) {
            if(measurement_data.valid) {
                data = measurement_data;
                success = true;
                data_ready_flag = false;  // Clear flag after read
            }
            xSemaphoreGive(data_mutex);
        }
        return success;
    }

    bool driver::is_data_ready() {

        bool ready = false;

        if (xSemaphoreTake(data_mutex, pdMS_TO_TICKS(TIMEOUT_MS)) == pdTRUE) {
            ready = data_ready_flag;
            xSemaphoreGive(data_mutex);
        }
        return ready;
    }

} // namespace adc
