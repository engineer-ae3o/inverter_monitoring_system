#include "freertos/FreeRTOS.h"
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


#define ADC_TASK_PROFILING            0
#define AHT_TASK_PROFILING            0
#define LOG_TASK_PROFILING            0
#define CALC_TASK_PROFILING           0
#define DISPLAY_TASK_PROFILING        1
#define LVGL_TASK_PROFILING           1
#define BLE_TASK_PROFILING            0

using namespace config;

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

static adc::driver power{};

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
    ASSERT(display_led_timer_handle, "display_led_timer_handle cannot be nullptr");

    // LCD Initialization
    constexpr ili9341_config_t config = {
        // SPI configuration
        .spi_host = LCD_SPI_HOST,
        .spi_clock_speed_hz = LCD_SPI_CLK_SPEED,
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
    result = display::init(display_handle, lvgl_display_mutex);
    if (result != ESP_OK) {
        LOGE("Failed to initialize LVGL and the display interface: %s", esp_err_to_name(result));
        sys::handle_error();
    }
    ASSERT(lvgl_display_mutex, "lvgl_display_mutex cannot be null");

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

    result = ble::init(final_data_queue);
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

// LVGL handler task
[[noreturn]] void lvgl_handler_task(void* arg) {

    LOGI("Starting lvgl_handler_task");

    TWDT_ADD_TASK(lvgl_handler_task);

    uint32_t time_till_next_call_ms{0};

#if LVGL_TASK_PROFILING == 1
    int64_t end[100]{};
    size_t i = 0;
#endif

    while (1) {

#if LVGL_TASK_PROFILING == 1
        int64_t start = esp_timer_get_time();
#endif

        TWDT_RESET_FROM_TASK(lvgl_handler_task);

        if (xSemaphoreTake(lvgl_display_mutex, pdMS_TO_TICKS(TIMEOUT_MS)) == pdTRUE) {
            time_till_next_call_ms = lv_timer_handler();
            xSemaphoreGive(lvgl_display_mutex);
        } else {
            LOGW("Failed to take mutex. Skipping frame");
        }

#if LVGL_TASK_PROFILING == 1
        end[i] = esp_timer_get_time() - start;
        float fps = 1'000'000 / (static_cast<float>(end[i]) + (time_till_next_call_ms * 1'000));
        LOGI("Time for lvgl_handler_task: %.3fms", static_cast<float>(end[i]) / 1000.0f);
        LOGI("Frames per seconds: %.3ffps", fps);

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

        vTaskDelay(pdMS_TO_TICKS(time_till_next_call_ms));

    }
}

// AHT20 read task
[[noreturn]] void aht_task(void* arg) {

    LOGI("Starting aht_task");

    TWDT_ADD_TASK(aht_task);

    aht20_data_t data{};
    aht20_err_t ret = AHT_OK;

#if AHT_TASK_PROFILING == 1
    int64_t end[100]{};
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
            vTaskDelay(pdMS_TO_TICKS(AHT_READ_PERIOD_MS));
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
[[noreturn]] void log_task(void* arg) {

    LOGI("Starting log_task");

    // Open f_data_file for reading and writing in binary format
    // We first check if the file exists with rb+. If it exists, we proceed
    // NOTE: rb+ returns nullptr if the file doesn't exist
    FILE* f_data_file = fopen(DATA_FILE_NAME, "rb+");
    if (!f_data_file) {
        // If the file doesn't exist, we create it with wb+
        // We can't use wb+ initially because it zeros out our file
        // whether or not it does exists, overwriting existing data
        f_data_file = fopen(DATA_FILE_NAME, "wb+");
        ASSERT(f_data_file, "f_data_file cannot be null");
    }

    // Open f_meta_data_file for reading and writing in binary format
    // We first check if the file exists with rb+. If it exists, we proceed
    FILE* f_meta_data_file = fopen(META_DATA_FILE_NAME, "rb+");
    size_t data_file_idx = 0;
    if (!f_meta_data_file) {
        // If the file doesn't exist, we create it with wb+
        f_meta_data_file = fopen(META_DATA_FILE_NAME, "wb+");
        ASSERT(f_meta_data_file, "f_meta_data_file cannot be null");
        // We then write data_file_idx's initial 0 value to it
        ASSERT((fwrite(&data_file_idx, sizeof(data_file_idx), 1, f_meta_data_file) == 1), "Failed to write data_file_idx to metadata file");
    } else {
        // Since the file exists, we read from it and we bounds check against MAX_SAMPLES_TO_LOG
        ASSERT((fread(&data_file_idx, sizeof(data_file_idx), 1, f_meta_data_file) == 1), "Failed to read file index");
        if (data_file_idx >= MAX_SAMPLES_TO_LOG) {
            data_file_idx = 0;
        }
    }

    // Position f_data_file at the next write location (resume from where we left off on last boot)
    ASSERT(fseek(f_data_file, data_file_idx * sizeof(file_data_t), SEEK_SET) == 0, "Failed to set file index for writing");
    
    sys::data_t data{};
    file_data_t file_data{};
    uint8_t ret = 0;
    size_t err_count = 0;

    std::array<file_data_t, NUM_OF_ITEMS_TO_STORE_TEMP> data_buffer_temp{};
    size_t temp_buffer_idx = 0;

#if LOG_TASK_PROFILING == 1
    int64_t end[100]{};
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

        file_data.voltage     = data.battery_voltage;
        file_data.current     = data.load_current_drawn;
        file_data.temperature = data.inv_temp;
        file_data.humidity    = data.inv_hmdt;
        file_data.battery_soc = data.battery_percent;

        // Store the received data in temporary buffer and increment index
        data_buffer_temp[temp_buffer_idx++] = file_data;
        
        if (temp_buffer_idx >= NUM_OF_ITEMS_TO_STORE_TEMP) {
            // Write samples to flash after our temporary buffer is full
            ret = fwrite(data_buffer_temp.data(), sizeof(file_data_t), NUM_OF_ITEMS_TO_STORE_TEMP, f_data_file);
            if (ret != NUM_OF_ITEMS_TO_STORE_TEMP) err_count++;
            temp_buffer_idx = 0;

            // Increment data_file_idx by NUM_OF_ITEMS_TO_STORE_TEMP because we stored that number of items
            data_file_idx += NUM_OF_ITEMS_TO_STORE_TEMP;

            // Set f_meta_data_file back to the beginning of the file before writing to the file
            // to overwrite the old data index present because we don't need to store different indices
            rewind(f_meta_data_file);

            ret = fwrite(&data_file_idx, sizeof(data_file_idx), 1, f_meta_data_file);
            if (ret != 1) err_count++;

            // No need to call `fflush()` as `fwrite()` writes to flash immediately
        }

        // Wrap around to the beginning of the file if data_file_idx gets to MAX_SAMPLES_TO_LOG
        if (data_file_idx >= MAX_SAMPLES_TO_LOG) {
            data_file_idx = 0;
            // Move f_data_file to the beginning of the file so we can overwrite the oldest data
            // since we have gotten to MAX_SAMPLES_TO_LOG 
            rewind(f_data_file);
        }

        if (err_count >= MAX_FILE_IO_ERRORS) {
            LOGE("Too many file IO errors: %u", err_count);
            sys::handle_error();
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
[[noreturn]] void adc_task(void* arg) {

    LOGI("Starting adc_task");

    TWDT_ADD_TASK(adc_task);

    adc::data_t data{};
    bool ret = false;

#if ADC_TASK_PROFILING == 1
    int64_t end[100]{};
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
[[noreturn]] void runtime_calc_task(void* arg) {
     
    LOGI("Starting runtime_calc_task");

    TWDT_ADD_TASK(runtime_calc_task);

    aht20_data_t aht_data{};
    adc::data_t power_data{};
    sys::data_t final_data{};

#if CALC_TASK_PROFILING == 1
    int64_t end[100]{};
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
[[noreturn]] void display_task(void* arg) {

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
    bool is_ble_active = false;

    // Arbitrary delay to allow the bootup screen flush properly before creating the UI
    vTaskDelay(pdMS_TO_TICKS(200));
    display::create_ui();

    {
        // Create graph screens
        display::graph_samples_t env{};
        display::graph_samples_t pow{};

        create_graph_screen(env, pow);
    }
    
    // Discard all button press events that may have occurred before the bootup screen finished loading
    xQueueReset(btn_queue);

    // Start timer which controls led dimming
    esp_timer_start_once(display_led_timer_handle, TIME_TO_LED_50_PERCENT_BRIGHTNESS_US);

#if DISPLAY_TASK_PROFILING == 1
    int64_t end[100]{};
    size_t i = 0;
#endif

    while (1) {

#if DISPLAY_TASK_PROFILING == 1
        int64_t start = esp_timer_get_time();
#endif

        TWDT_RESET_FROM_TASK(display_task);

        // Check for button events
        // Dismiss any popups that may have been active
        // Do not register any button events if a popup
        // was active. Instead, only clear the popup
        if (xQueueReceive(btn_queue, &event, 0) == pdTRUE) {
            if (display::is_ble_popup_active()) {
                display::ble_popup(display::ble_popup_t::CLEAR_POPUPS);
                
            } else {
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
                    display::pow_graph_screen();
                    LOGI("NEXT button pressed for at least %lus", (BUTTON_LONG_PRESS_US / 1'000'000));
                    break;

                // Load graph screen for temperature and humidity
                case button::event_t::PREV_LONG_PRESSED:
                    display::env_graph_screen();
                    LOGI("PREV button pressed for at least %lus", (BUTTON_LONG_PRESS_US / 1'000'000));
                    break;

                // Start ble advertising
                case button::event_t::BLE_BUTTON_PRESSED:
                    if (is_ble_active) {
                        LOGW("BLE already advertising");
                        display::ble_popup(display::ble_popup_t::ALREADY_ACTIVE);
                        break;
                    }
                    ret = ble::start();
                    if (ret == ESP_OK) {
                        LOGI("BLE advertsing started");
                        display::ble_popup(display::ble_popup_t::ACTIVATED);
                        is_ble_active = true;
                    } else {
                        LOGE("Failed to start BLE advertising: %s", esp_err_to_name(ret));
                        display::ble_popup(display::ble_popup_t::ACTIVATION_FAILED);
                    }
                    break;

                // Stop ble advertising
                case button::event_t::BLE_LONG_PRESSED:
                    if (!is_ble_active) {
                        LOGW("BLE advertising already inactive");
                        display::ble_popup(display::ble_popup_t::ALREADY_INACTIVE);
                        break;
                    }
                    ret = ble::stop();
                    if (ret == ESP_OK) {
                        LOGI("BLE advertsing stopped");
                        display::ble_popup(display::ble_popup_t::DEACTIVATED);
                        is_ble_active = false;
                    } else {
                        LOGE("Failed to stop BLE advertising: %s", esp_err_to_name(ret));
                        display::ble_popup(display::ble_popup_t::DEACTIVATION_FAILED);
                    }
                    break;

                default:
                    LOGW("Unknown button event");
                }
            }
        }

        // Block till runtime_calc_task tells us we have fresh data to update the current screen
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(TIMEOUT_MS));

        if (xQueueReceive(final_data_queue, &data, 0) != pdTRUE) {
            LOGW("Failed to receive data from final_data_queue (display_task)");
            xSemaphoreGive(lvgl_display_mutex);
            continue;
        }

        display::update_screen_data(data);
        
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
[[noreturn]] void ble_task(void* arg) {

    LOGI("ble_task started");
    
    sys::data_t data{};
    esp_err_t ret = ESP_OK;

#if BLE_TASK_PROFILING == 1
    int64_t end[100]{};
    size_t i = 0;
#endif

    while (1) {

#if BLE_TASK_PROFILING == 1
        int64_t start = esp_timer_get_time();
#endif

        if (!ble::is_client_subscribed()) {
            vTaskDelay(pdMS_TO_TICKS(BLE_TASK_PERIOD_MS));
            continue;
        }

        if (xQueuePeek(final_data_queue, &data, pdMS_TO_TICKS(BLE_TASK_PERIOD_MS)) != pdTRUE) {
            LOGW("Failed to receive data from final_data_queue (ble_task)");
            vTaskDelay(pdMS_TO_TICKS(BLE_TASK_PERIOD_MS));
            continue;
        }

        ret = ble::notify_data(data);
        if (ret == ESP_OK) {
            LOGI("Data sent via BLE notification successfully");
        } else if (ret == ESP_ERR_INVALID_STATE) {
            LOGW("BLE client not connected or subscribed");
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

    void app_main() {

        // Create queues
        queue_create();

        // Initialize all components
        init_all();

        // Create tasks
        BaseType_t ret = xTaskCreate(lvgl_handler_task, "LVGLHandlerTask", LVGL_TASK_STACK_SIZE, nullptr, LVGL_TASK_PRIORITY, &lvgl_task_handle);
        if (ret != pdPASS) {
            LOGE("Failed to create lvgl_handler_task");
            sys::handle_error();
        }
        
        ret = xTaskCreate(log_task, "LogTask", LOG_TASK_STACK_SIZE, nullptr, LOG_TASK_PRIORITY, &log_task_handle);
        if (ret != pdPASS) {
            LOGE("Failed to create log_task");
            sys::handle_error();
        }

        ret = xTaskCreate(aht_task, "AHTTask", AHT_TASK_STACK_SIZE, nullptr, AHT_TASK_PRIORITY, &aht_task_handle);
        if (ret != pdPASS) {
            LOGE("Failed to create aht_task");
            sys::handle_error();
        }

        ret = xTaskCreate(adc_task, "ADCTask", ADC_TASK_STACK_SIZE, nullptr, ADC_TASK_PRIORITY, &adc_task_handle);
        if (ret != pdPASS) {
            LOGE("Failed to create adc_task");
            sys::handle_error();
        }

        ret = xTaskCreate(display_task, "DisplayTask", DISPLAY_TASK_STACK_SIZE, nullptr, DISPLAY_TASK_PRIORITY, &display_task_handle);
        if (ret != pdPASS) {
            LOGE("Failed to create display_task");
            sys::handle_error();
        }

        ret = xTaskCreate(runtime_calc_task, "RuntimeCalcsTask", CALC_TASK_STACK_SIZE, nullptr, CALC_TASK_PRIORITY, &calc_runtime_task_handle);
        if (ret != pdPASS) {
            LOGE("Failed to create runtime_calc_task");
            sys::handle_error();
        }

        ret = xTaskCreate(ble_task, "BLETask", BLE_TASK_STACK_SIZE, nullptr, BLE_TASK_PRIORITY, &ble_task_handle);
        if (ret != pdPASS) {
            LOGE("Failed to create ble_task");
            sys::handle_error();
        }
    }
} // extern "C"
