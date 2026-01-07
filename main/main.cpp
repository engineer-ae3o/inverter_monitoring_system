/*#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"

#include "power_monitor.hpp"
#include "ble.hpp"
#include "config.hpp"
#include "system.hpp"
#include "display.hpp"
#include "button_handler.hpp"
#include "aht20.h"
#include "ili9341.h"

#include "esp_task_wdt.h"
#include "esp_littlefs.h"
#include "esp_err.h"
#include "esp_timer.h"
#include "esp_log.h"

#include <cstdio>
#include <array>


#define DEBUG 1

#if DEBUG == 1
static const char* TAG = "MAIN";
#define LOGI(...) ESP_LOGI(TAG, __VA_ARGS__)
#define LOGW(...) ESP_LOGW(TAG, __VA_ARGS__)
#define LOGE(...) ESP_LOGE(TAG, __VA_ARGS__)
#else 
#define LOGI(...)
#define LOGW(...)
#define LOGE(...)
#endif


#define TWDT_ADD_TASK(name)                                                            \
    do {                                                                               \
        esp_err_t ret = esp_task_wdt_add(nullptr);                                     \
        if (ret != ESP_OK) {                                                           \
            LOGE("Failed to subscribe %s to TWDT", #name);                             \
            sys::handle_error();                                                       \
        }                                                                              \
    } while (0)

#define TWDT_RESET_FROM_TASK(name)                                                     \
    do {                                                                               \
        esp_err_t ret = esp_task_wdt_reset();                                          \
        if (ret != ESP_OK) {                                                           \
            LOGE("Failed to reset TWDT from %s. Likely system reboot", #name);         \
        }                                                                              \
    } while (0)


#define ADC_TASK_PROFILING                                  0
#define AHT_TASK_PROFILING                                  0
#define LOG_TASK_PROFILING                                  0
#define CALC_TASK_PROFILING                                 0
#define DISPLAY_TASK_PROFILING                              0
#define LVGL_TASK_PROFILING                                 0
#define BLE_TASK_PROFILING                                  0

using namespace config;
using FILE_t = FILE;

// Task handles
static TaskHandle_t calc_runtime_task_handle = nullptr;
static TaskHandle_t display_task_handle = nullptr;
static TaskHandle_t adc_task_handle = nullptr;
static TaskHandle_t aht_task_handle = nullptr;
static TaskHandle_t lvgl_task_handle = nullptr;
static TaskHandle_t log_task_handle = nullptr;
static TaskHandle_t ble_task_handle = nullptr;

//  Queue parameters
static QueueHandle_t aht_queue = nullptr;
static QueueHandle_t power_queue = nullptr;
static QueueHandle_t final_data_queue = nullptr;

// Mutex for thread safety between lvgl_handler_task and display_task
SemaphoreHandle_t lvgl_display_mutex = nullptr;

// File data
struct file_data_t {
    float voltage;
    float current;
    float temperature;
    float humidity;
    float battery_soc;
};

static esp_timer_handle_t display_led_timer_handle = nullptr;
static ili9341_handle_t display_handle = nullptr;

static adc::driver power;

static void init_all() {

    // AHT20 Initialization
    aht20_err_t ret = aht20_init(AHT_SDA_PIN, AHT_SCL_PIN);
    if (ret != AHT_OK) {
        LOGE("AHT20 initialization error: %s", aht_err_to_string(ret));
        sys::handle_error();
    }

    // ADC Initialization
    bool chk = power.init(CURRENT_SENSOR_PIN, VOLTAGE_SENSOR_PIN);
    if (!chk) {
        LOGE("Failed to initialize ADC");
        sys::handle_error();
    }

    // Button handler initialization
    esp_err_t result = button::init(display_led_timer_handle);
    if (result != ESP_OK) {
        LOGE("Failed to initialize button handler: %s", esp_err_to_name(result));
        sys::handle_error();
    }
    ASSERT(display_led_timer_handle, "display_led_timer_handle cannot be null");

    // LCD Initialization
    constexpr ili9341_config_t config = {
        // SPI configuration
        .spi_host = SPI_LCD_HOST,
        .spi_clock_speed_hz = SPI_CLK_SPEED,
        // GPIO pins
        .pin_mosi = MOSI_PIN,
        .pin_sclk = SCLK_PIN,
        .pin_cs = CS_PIN,
        .pin_dc = DC_PIN,
        .pin_rst = RST_PIN,
        // Display parameters
        .width = LCD_WIDTH,
        .height = LCD_HEIGHT,
        .rotation = LCD_ROTATION,
        // Retries
        .max_retries = LCD_SPI_MAX_RETRIES,
        // Task configuration
        .queue_size = 10,
        .task_priority = 8,
        .task_core = 1,
        .task_stack_size = 4096
    };

    result = ili9341_init(&config, &display_handle);
    if (result != ESP_OK) {
        LOGE("LCD initialization error: %s", esp_err_to_name(result));
        sys::handle_error();
    }

    // Display interface initialization
    result = display::init(display_handle);
    if (result != ESP_OK) {
        LOGE("Failed to initialize LVGL and the display interface: %s", esp_err_to_name(result));
        sys::handle_error();
    }

    // LittleFS and partition initialization
    constexpr esp_vfs_littlefs_conf_t littlefs_config = {
        .base_path = "/storage",
        .partition_label = "storage",
        .partition = nullptr,
        .format_if_mount_failed = 1,
        .read_only = 0,
        .dont_mount = 0,
        .grow_on_mount = 1
    };

    result = esp_vfs_littlefs_register(&littlefs_config);
    if (result != ESP_OK) {
        LOGE("Failed to mount littlefs partition: %s", esp_err_to_name(result));
        sys::handle_error();
    }

    result = ble::init();
    if (result != ESP_OK) {
        LOGE("Failed to initialize BLE GATT server: %s", esp_err_to_name(result));
        sys::handle_error();
    }
    
    LOGI("Initialization Complete");
}

static void queue_create() {

    aht_queue = xQueueCreate(1, sizeof(aht20_data_t));
    if (!aht_queue) {
        LOGE("Failed to create AHT20 sensor data queue");
        sys::handle_error();
    }

    power_queue = xQueueCreate(1, sizeof(adc::data_t));
    if (!power_queue) {
        LOGE("Failed to create queue for power readings");
        sys::handle_error();
    }

    final_data_queue = xQueueCreate(QUEUE_LENGTH, sizeof(sys::data_t));
    if (!final_data_queue) {
        LOGE("Failed to create queue to store final data");
        sys::handle_error();
    }
}

QueueHandle_t& get_data_queue() {
    return final_data_queue;
}

// LVGL handler task
void lvgl_handler_task(void* arg) {

    LOGI("Starting lvgl_handler_task");

    TWDT_ADD_TASK(lvgl_handler_task);

#if LVGL_TASK_PROFILING == 1
    int64_t end[100] = {};
    size_t i = 0;
#endif

    while (1) {

#if LVGL_TASK_PROFILING == 1
        int64_t start = esp_timer_get_time();
#endif

        TWDT_RESET_FROM_TASK(lvgl_handler_task);

        if (xSemaphoreTake(lvgl_display_mutex, pdMS_TO_TICKS(TIMEOUT_MS)) == pdTRUE) {
            lv_timer_handler();
            xSemaphoreGive(lvgl_display_mutex);
        } else {
            LOGW("Failed to take mutex. Skipping frame");
        }

#if LVGL_TASK_PROFILING == 1
        end[i] = esp_timer_get_time() - start;
        LOGI("Time for lvgl_handler_task: %.3fms", static_cast<float>(end[i]) / 1000.0f);

        i++;
        if (i >= 100) {
            double average = 0;
            for (size_t j = 0; j < 100; j++) {
                average += end[j];
            }
            average /= 100;
            LOGI("Average execution time for lvgl_handler_task: %.3fms", average / 1000.0f);
            i = 0;
        }
#endif

        vTaskDelay(pdMS_TO_TICKS(LVGL_TASK_PERIOD_MS));
    }
}

// AHT20 read task
void aht_task(void* arg) {

    LOGI("Starting aht_task");

    TWDT_ADD_TASK(aht_task);

    aht20_data_t data{};
    aht20_err_t ret = AHT_OK;

#if AHT_TASK_PROFILING == 1
    int64_t end[100] = {};
    size_t i = 0;
#endif

    while (1) {

#if AHT_TASK_PROFILING == 1
        int64_t start = esp_timer_get_time();
#endif

        TWDT_RESET_FROM_TASK(aht_task);

        ret = aht20_read(&data);
        if (ret != AHT_OK) {
            LOGW("Failed to read data from the AHT20: %s", aht_err_to_string(ret));
            continue;
        }

        xQueueOverwrite(aht_queue, &data);

#if AHT_TASK_PROFILING == 1
        end[i] = esp_timer_get_time() - start;
        LOGI("Time for aht_task: %.3fms", static_cast<float>(end[i]) / 1000.0f);

        i++;
        if (i >= 100) {
            double average = 0;
            for (size_t j = 0; j < 100; j++) {
                average += end[j];
            }
            average /= 100;
            LOGI("Average execution time for aht_task: %.3fms", average / 1000.0f);
            i = 0;
        }
#endif

        vTaskDelay(pdMS_TO_TICKS(AHT_READ_PERIOD_MS));
    } 
}

// Log task
void log_task(void* arg) {

    LOGI("Starting log_task");

    // Open f_data_file for reading and writing in binary format
    // We first check if the file exists with rb+. If it exists, we proceed
    // NOTE: rb+ returns NULL if the file doesn't exist
    FILE_t* f_data_file = fopen(DATA_FILE_NAME, "rb+");
    if (!f_data_file) {
        // If the file doesn't exist, we create it with wb+
        // We can't use wb+ initially because it zeros out our file whether or not it does exists,
        // overwriting existing data
        f_data_file = fopen(DATA_FILE_NAME, "wb+");
        ASSERT(f_data_file, "f_data_file cannot be null");
    }

    // Open f_meta_data_file for reading and writing in binary format
    // We first check if the file exists with rb+. If it exists, we proceed
    FILE_t* f_meta_data_file = fopen(META_DATA_FILE_NAME, "rb+");
    size_t data_file_idx = 0;
    if (!f_meta_data_file) {
        // If the file doesn't exist, we create it with wb+
        f_meta_data_file = fopen(META_DATA_FILE_NAME, "wb+");
        ASSERT(f_meta_data_file, "f_meta_data_file cannot be null");
        // We then write data_file_idx's initial 0 value to it
        fwrite(&data_file_idx, sizeof(data_file_idx), 1, f_meta_data_file);
    } else {
        // Since the file exists, we read from it and we bounds check against MAX_SAMPLES_TO_LOG
        fread(&data_file_idx, sizeof(data_file_idx), 1, f_meta_data_file);
        if (data_file_idx >= MAX_SAMPLES_TO_LOG) {
            data_file_idx = 0;
        }
    }

    // Position f_data_file at the next write location (resume from where we left off on last boot)
    fseek(f_data_file, data_file_idx * sizeof(file_data_t), SEEK_SET);
    
    sys::data_t data{};
    file_data_t file_data{};

    std::array<file_data_t, NUM_OF_ITEMS_TO_STORE_TEMP> data_buffer_temp{};
    size_t temp_buffer_idx = 0;

#if LOG_TASK_PROFILING == 1
    int64_t end[100] = {};
    size_t i = 0;
#endif

    while (1) {

#if LOG_TASK_PROFILING == 1
        int64_t start = esp_timer_get_time();
#endif

        if (xQueuePeek(final_data_queue, &data, pdMS_TO_TICKS(TIMEOUT_MS)) != pdTRUE) {
            LOGW("Failed to receive data from final_data_queue (log_task)");
            vTaskDelay(pdMS_TO_TICKS(LOG_TASK_PERIOD_MS));
            continue;
        }

        file_data.voltage         = data.battery_voltage;
        file_data.current         = data.load_current_drawn;
        file_data.temperature     = data.inv_temp;
        file_data.humidity        = data.inv_hmdt;
        file_data.battery_soc     = data.battery_percent;

        // Store the received data in a temporary buffer and increment index
        data_buffer_temp[temp_buffer_idx++] = file_data;
        
        if (temp_buffer_idx >= NUM_OF_ITEMS_TO_STORE_TEMP) {
            // Write samples to flash after our temporary buffer is full
            fwrite(data_buffer_temp.data(), sizeof(file_data_t), NUM_OF_ITEMS_TO_STORE_TEMP, f_data_file);
            temp_buffer_idx = 0;

            // Increment data_file_idx by NUM_OF_ITEMS_TO_STORE_TEMP because we stored that number of items
            data_file_idx += NUM_OF_ITEMS_TO_STORE_TEMP;

            // Set f_meta_data_file back to the beginning of the file before writing to the file
            // to overwrite the old data index present because we don't need to store different indices
            rewind(f_meta_data_file);
            fwrite(&data_file_idx, sizeof(data_file_idx), 1, f_meta_data_file);

            // No need to call `fflush()` as `fwrite()` writes to flash immediately
        }

        // Wrap around to the beginning of the file if data_file_idx gets to MAX_SAMPLES_TO_LOG
        if (data_file_idx >= MAX_SAMPLES_TO_LOG) {
            data_file_idx = 0;
            // Move f_data_file to the beginning of the file so we can overwrite the oldest data
            // since we have gotten to MAX_SAMPLES_TO_LOG 
            rewind(f_data_file);
        }

#if LOG_TASK_PROFILING == 1
        end[i] = esp_timer_get_time() - start;
        LOGI("Time for log_task: %.3fms", static_cast<float>(end[i]) / 1000.0f);

        i++;
        if (i >= 100) {
            double average = 0;
            for (size_t j = 0; j < 100; j++) {
                average += end[j];
            }
            average /= 100;
            LOGI("Average execution time for log_task: %.3fms", average / 1000.0f);
            i = 0;
        }
#endif
    
        vTaskDelay(pdMS_TO_TICKS(LOG_TASK_PERIOD_MS));
    } 
}

// ADC read task
void adc_task(void* arg) {

    LOGI("Starting adc_task");

    TWDT_ADD_TASK(adc_task);

    adc::data_t data{};
    bool ret = false;

#if ADC_TASK_PROFILING == 1
    int64_t end[100] = {};
    size_t i = 0;
#endif

    while (1) {

#if ADC_TASK_PROFILING == 1
        int64_t start = esp_timer_get_time();
#endif

        TWDT_RESET_FROM_TASK(adc_task);

        ret = power.get_measurement_data(data);
        if (!ret) {
            LOGW("Failed to read ADC data.");
            continue;
        }

        xQueueOverwrite(power_queue, &data);

#if ADC_TASK_PROFILING == 1
        end[i] = esp_timer_get_time() - start;
        LOGI("Time for adc_task: %.3fus", static_cast<float>(end[i]));

        i++;
        if (i >= 100) {
            double average = 0;
            for (size_t j = 0; j < 100; j++) {
                average += end[j];
            }
            average /= 100;
            LOGI("Average execution time for adc_task: %.3fus", average);
            i = 0;
        }
#endif

        vTaskDelay(pdMS_TO_TICKS(ADC_READ_PERIOD_MS));
    }
}

// Task to calculate runtime parameters
void runtime_calc_task(void* arg) {
     
    LOGI("Starting runtime_calc_task");

    TWDT_ADD_TASK(runtime_calc_task);

    aht20_data_t aht_data{};
    adc::data_t power_data{};
    sys::data_t final_data{};

#if CALC_TASK_PROFILING == 1
    int64_t end[100] = {};
    size_t i = 0;
#endif

    while (1) {

#if CALC_TASK_PROFILING == 1
        int64_t start = esp_timer_get_time();
#endif

        TWDT_RESET_FROM_TASK(runtime_calc_task);

        if (xQueueReceive(aht_queue, &aht_data, 0) != pdTRUE) {
            // This is commented out because the AHT20 can only be read from at certain intevals, so we
            // will get a lot of stale reads, so logging each one would flood the logs
            // LOGW("Data not received from aht data queue. Using stale data");
        }

        if (xQueueReceive(power_queue, &power_data, 0) != pdTRUE) {
            LOGW("Data not received from power data queue. Using stale data");
        }

        bool ret = sys::calc_total_runtime_stats(aht_data, power_data, final_data);
        if (!ret) {
            LOGW("Failed to calculate all run time parameters successfully");
            continue;
        }

        if (xQueueSend(final_data_queue, &final_data, 0) != pdTRUE) {
            // Removing oldest data
            sys::data_t dummy{};
            xQueueReceive(final_data_queue, &dummy, 0);
            // Sending latest data again
            xQueueSend(final_data_queue, &final_data, 0);
        }

        xTaskNotifyGive(display_task_handle);

#if CALC_TASK_PROFILING == 1
        end[i] = esp_timer_get_time() - start;
        LOGI("Time for runtime_calc_task: %.3fus", static_cast<float>(end[i]));

        i++;
        if (i >= 100) {
            double average = 0;
            for (size_t j = 0; j < 100; j++) {
                average += end[j];
            }
            average /= 100;
            LOGI("Average execution time for runtime_calc_task: %.3fus", average);
            i = 0;
        }
#endif

        vTaskDelay(pdMS_TO_TICKS(CALC_TASK_PERIOD_MS));
    }
}

// Task that controls UI updates
void display_task(void* arg) {

    LOGI("Starting display_task");

    TWDT_ADD_TASK(display_task);

    // Start loading bootup screen
    display::bootup_screen();

    // Get queue in which button events are passed
    QueueHandle_t btn_queue = button::get_queue();
    ASSERT(btn_queue, "btn_queue cannot be null");

    // Initialize local variables being used
    button::event_t event = button::event_t::NO_EVENT;
    sys::data_t data{};
    esp_err_t ret = ESP_OK;

    // Arbitrary delay to allow the bootup screen flush properly before creating the UI
    vTaskDelay(pdMS_TO_TICKS(100));
    display::create_ui();

    // Start timer which controls led dimming
    esp_timer_start_once(display_led_timer_handle, TIME_TO_LED_50_PERCENT_BRIGHTNESS_US);

#if DISPLAY_TASK_PROFILING == 1
    int64_t end[100] = {};
    size_t i = 0;
#endif

    while (1) {

#if DISPLAY_TASK_PROFILING == 1
        int64_t start = esp_timer_get_time();
#endif

        TWDT_RESET_FROM_TASK(display_task);
        
        if (xSemaphoreTake(lvgl_display_mutex, pdMS_TO_TICKS(TIMEOUT_MS)) != pdTRUE) {
            LOGW("Failed to take lvgl_display_mutex");
            continue;
        }

        // Check for button events
        if (xQueueReceive(btn_queue, &event, 0) == pdTRUE) {
            switch (event) {
            // Load next screen
            case button::event_t::NEXT_BUTTON_PRESSED:
                LOGI("NEXT button pressed");
                display::next_screen();
                break;

            // Load previous screen
            case button::event_t::PREV_BUTTON_PRESSED:
                LOGI("PREV button pressed");
                display::prev_screen();
                break;

            // Load graph screen for voltage and current
            case button::event_t::NEXT_LONG_PRESSED:
                // TODO: Implemen historical graph screen for voltage and current
                LOGI("NEXT button pressed for at least %lus", (BUTTON_LONG_PRESS_US / 1000000));
                break;

            // Load graph screen for temperature and humidity
            case button::event_t::PREV_LONG_PRESSED:
                // TODO: Implement historical graph screen for temperature and humidity
                LOGI("PREV button pressed for at least %lus", (BUTTON_LONG_PRESS_US / 1000000));
                break;

            // Start ble advertising
            case button::event_t::BLE_BUTTON_PRESSED:
                ret = ble::start();
                if (ret == ESP_OK) {
                    LOGI("BLE advertsing started");
                } else if (ret == ESP_ERR_INVALID_STATE) {
                    LOGW("Device already advertising");
                } else {
                    LOGE("Failed to start BLE advertising: %s", esp_err_to_name(ret));
                }
                break;

            // Stop ble advertising
            case button::event_t::BLE_LONG_PRESSED:
                ret = ble::stop();
                if (ret == ESP_OK) {
                    LOGI("BLE advertsing stopped");
                } else if (ret == ESP_ERR_INVALID_STATE) {
                    LOGW("Device not advertising");
                } else {
                    LOGE("Failed to stop BLE advertising: %s", esp_err_to_name(ret));
                }
                break;

            default:
                LOGW("Unknown button event");
            }
        }

        // Give mutex before blocking
        xSemaphoreGive(lvgl_display_mutex);

        // Block till runtime_calc_task tells us we have fresh data to update the current screen
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(TIMEOUT_MS));

        if (xSemaphoreTake(lvgl_display_mutex, pdMS_TO_TICKS(TIMEOUT_MS * 2)) != pdTRUE) {
            LOGW("Failed to take lvgl_display_mutex");
            continue;
        }

        if (xQueueReceive(final_data_queue, &data, 0) != pdTRUE) {
            LOGW("Failed to receive data from final_data_queue (display_task)");
            xSemaphoreGive(lvgl_display_mutex);
            continue;
        }

        display::update_screen_data(data);

        xSemaphoreGive(lvgl_display_mutex);

#if DISPLAY_TASK_PROFILING == 1
        end[i] = esp_timer_get_time() - start;
        LOGI("Time for display_task: %.3fms", static_cast<float>(end[i]) / 1000.0f);
        
        i++;
        if (i >= 100) {
            double average = 0;
            for (size_t j = 0; j < 100; j++) {
                average += end[j];
            }
            average /= 100;
            LOGI("Average execution time for display_task: %.3fms", average / 1000.0f);
            i = 0;
        }
#endif
    }
}

// BLE notification task
void ble_task(void* arg) {

    LOGI("ble_task started");
    
    sys::data_t data{};
    esp_err_t ret = ESP_OK;

#if BLE_TASK_PROFILING == 1
    int64_t end[100] = {};
    size_t i = 0;
#endif

    while (1) {

#if BLE_TASK_PROFILING == 1
        int64_t start = esp_timer_get_time();
#endif

        if (xQueuePeek(final_data_queue, &data, pdMS_TO_TICKS(BLE_TASK_PERIOD_MS)) != pdTRUE) {
            LOGW("Failed to receive data from final_data_queue (ble_task)");
            continue;
        }

        ret = ble::notify_data(data);
        if (ret == ESP_OK) {
            LOGI("Data sent via BLE notification successfully");
        } else if (ret == ESP_ERR_INVALID_STATE) {
            LOGW("BLE client not connected or subscibed");
        } else {
            LOGW("Failed to send data notification: %s", esp_err_to_name(ret));
        }

#if BLE_TASK_PROFILING == 1
        end[i] = esp_timer_get_time() - start;
        LOGI("Time for ble_task: %.3fms", static_cast<float>(end[i]) / 1000.0f);
        
        i++;
        if (i >= 100) {
            double average = 0;
            for (size_t j = 0; j < 100; j++) {
                average += end[j];
            }
            average /= 100;
            LOGI("Average execution time for ble_task: %.3fms", average / 1000.0f);
            i = 0;
        }
#endif

        vTaskDelay(pdMS_TO_TICKS(BLE_TASK_PERIOD_MS));
    }
}


extern "C" {

    void app_main(void) {

        // Initialize all components
        init_all();

        // Create queues
        queue_create();

        lvgl_display_mutex = xSemaphoreCreateMutex();
        ASSERT(lvgl_display_mutex, "lvgl_display_mutex cannot be null");

        // Create tasks
        BaseType_t ret = xTaskCreate(
                                     lvgl_handler_task, 
                                     "LVGLHandlerTask", 
                                     LVGL_TASK_STACK_SIZE, 
                                     nullptr, 
                                     LVGL_TASK_PRIORITY, 
                                     &lvgl_task_handle
        );
        if (ret != pdPASS) {
            LOGE("Failed to create lvgl_handler_task");
            sys::handle_error();
        }
        
        ret = xTaskCreate(
                          log_task, 
                          "LogTask", 
                          LOG_TASK_STACK_SIZE, 
                          nullptr, 
                          LOG_TASK_PRIORITY,
                          &log_task_handle
        );
        if (ret != pdPASS) {
            LOGE("Failed to create log_task");
            sys::handle_error();
        }

        ret = xTaskCreate(
                          aht_task,
                          "AHTTask",
                          AHT_TASK_STACK_SIZE,
                          nullptr,
                          AHT_TASK_PRIORITY,
                          &aht_task_handle
        );
        if (ret != pdPASS) {
            LOGE("Failed to create aht_task");
            sys::handle_error();
        }

        ret = xTaskCreate(
                          adc_task,
                          "ADCTask",
                          ADC_TASK_STACK_SIZE,
                          nullptr,
                          ADC_TASK_PRIORITY,
                          &adc_task_handle
        );
        if (ret != pdPASS) {
            LOGE("Failed to create adc_task");
            sys::handle_error();
        }

        ret = xTaskCreate(
                          display_task,
                          "DisplayTask",
                          DISPLAY_TASK_STACK_SIZE,
                          nullptr,
                          DISPLAY_TASK_PRIORITY,
                          &display_task_handle
        );
        if (ret != pdPASS) {
            LOGE("Failed to create display_task");
            sys::handle_error();
        }

        ret = xTaskCreate(
                          runtime_calc_task,
                          "RuntimeCalcsTask",
                          CALC_TASK_STACK_SIZE,
                          nullptr,
                          CALC_TASK_PRIORITY,
                          &calc_runtime_task_handle
        );
        if (ret != pdPASS) {
            LOGE("Failed to create runtime_calc_task");
            sys::handle_error();
        }

        ret = xTaskCreate(
                          ble_task, 
                          "BLETask", 
                          BLE_TASK_STACK_SIZE, 
                          nullptr, 
                          BLE_TASK_PRIORITY, 
                          &ble_task_handle
        );
        if (ret != pdPASS) {
            LOGE("Failed to create ble_task");
            sys::handle_error();
        }
    }

} // extern "C"
*/

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "ble.hpp"

#include "esp_log.h"
#include "esp_err.h"

#include <array>


extern "C" {
    void app_main() {
        ble::init();
        ble::start();
        while (1) {
            ESP_LOGI("MAIN", "Logging every 10s to stay alive");
            vTaskDelay(pdMS_TO_TICKS(10000U));
        }
        ble::stop();
        ble::deinit();
    }
} // extern "C"
