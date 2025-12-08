#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"

#include "power_monitor.hpp"
#include "config.hpp"
#include "system.hpp"
#include "display.hpp"
#include "button_handler.hpp"
#include "aht20.h"
#include "st7735.h"

#include "esp_task_wdt.h"
#include "esp_err.h"
#include "esp_timer.h"
#include "esp_log.h"


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


using namespace config;


// Calc_runtime task parameters
static TaskHandle_t calc_runtime_task_handle = nullptr;
static StackType_t calc_task_stack[CALC_TASK_STACK_SIZE];
static StaticTask_t calc_task_buffer;

// Display task parameters
static TaskHandle_t display_task_handle = nullptr;
static StackType_t display_task_stack[DISPLAY_TASK_STACK_SIZE];
static StaticTask_t display_task_buffer;

// ADC read task parameters
static TaskHandle_t adc_task_handle = nullptr;
static StackType_t adc_task_stack[ADC_TASK_STACK_SIZE];
static StaticTask_t adc_task_buffer;

// AHT read task parameters
static TaskHandle_t aht_task_handle = nullptr;
static StackType_t aht_task_stack[AHT_TASK_STACK_SIZE];
static StaticTask_t aht_task_buffer;

// LVGL handler task parameters
static TaskHandle_t lvgl_task_handle = nullptr;
static StackType_t lvgl_task_stack[LVGL_TASK_STACK_SIZE];
static StaticTask_t lvgl_task_buffer;

//  Queue parameters
// AHT data queue
static QueueHandle_t aht_queue = nullptr;
static StaticQueue_t aht_queue_buffer;
static uint8_t aht_queue_stack[QUEUE_LENGTH * sizeof(aht20_data_t)];

// ADC data queue
static QueueHandle_t power_queue = nullptr;
static StaticQueue_t power_queue_buffer;
static uint8_t power_queue_stack[QUEUE_LENGTH * sizeof(adc::data_t)];

// Runtime calc stats queue
static QueueHandle_t final_data_queue = nullptr;
static StaticQueue_t final_data_queue_buffer;
static uint8_t final_data_queue_stack[QUEUE_LENGTH * sizeof(sys::data_t)];

// Mutex to ensure thread safety across lvgl function calls across different tasks
static SemaphoreHandle_t lvgl_display_mutex;
static StaticSemaphore_t lvgl_display_mutex_buffer;

// Instance of the driver class
static adc::driver power;

static void init_all(void) {

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
    esp_err_t result = button::init();
    if (result != ESP_OK) {
        LOGE("Failed to initialize button handler: %s", esp_err_to_name(result));
        sys::handle_error();
    }

    // LCD Initialization
    st7735_config_t config = {
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
        .queue_size = 5,
        .task_priority = 6,
        .task_core = 1,
        .task_stack_size = 4096
    };

    result = st7735_init(&config);
    if (result != ESP_OK) {
        LOGE("LCD initialization error: %s", esp_err_to_name(result));
        sys::handle_error();
    }

    result = display::init();
    if (result != ESP_OK) {
        LOGE("Failed to initialize LVGL and the display interface: %s", esp_err_to_name(result));
        sys::handle_error();
    }
    
    LOGI("Initialization Complete");
}

static void queue_create(void) {

    aht_queue = xQueueCreateStatic(QUEUE_LENGTH, sizeof(aht20_data_t), aht_queue_stack, &aht_queue_buffer);
    if (!aht_queue) {
        LOGE("Failed to create AHT20 sensor data queue");
        sys::handle_error();
    }

    power_queue = xQueueCreateStatic(QUEUE_LENGTH, sizeof(adc::data_t), power_queue_stack, &power_queue_buffer);
    if (!power_queue) {
        LOGE("Failed to create queue for power readings");
        sys::handle_error();
    }

    final_data_queue = xQueueCreateStatic(QUEUE_LENGTH, sizeof(sys::data_t), final_data_queue_stack, &final_data_queue_buffer);
    if (!final_data_queue) {
        LOGE("Failed to create queue to store final data");
        sys::handle_error();
    }
}


//LVGL handler task
void lvgl_handler_task(void* arg) {

    LOGI("Starting lvgl_handler_task");

    TWDT_ADD_TASK(lvgl_handler_task);

    while (1) {

        TWDT_RESET_FROM_TASK(lvgl_handler_task);

        if (xSemaphoreTake(lvgl_display_mutex, pdMS_TO_TICKS(LVGL_TASK_PERIOD_MS)) == pdTRUE) {
            lv_timer_handler();
            xSemaphoreGive(lvgl_display_mutex);
        } else {
            LOGW("Failed to take mutex. Skipping frames");
        }

        vTaskDelay(pdMS_TO_TICKS(LVGL_TASK_PERIOD_MS));
    }
}

// AHT20 read task
void aht_task(void* arg) {

    LOGI("Starting aht_task");

    TWDT_ADD_TASK(aht_task);

    aht20_data_t data = {};
    aht20_err_t ret = AHT_OK;

    while (1) {

        TWDT_RESET_FROM_TASK(aht_task);

        ret = aht20_read(&data);
        if (ret != AHT_OK) {
            LOGW("Failed to read data from the AHT20: %s", aht_err_to_string(ret));
            continue;
        }

        if (xQueueSend(aht_queue, &data, pdMS_TO_TICKS(TIMEOUT_MS)) != pdTRUE) {
            // Removing oldest data
            aht20_data_t dummy = {};
            xQueueReceive(aht_queue, &dummy, pdMS_TO_TICKS(TIMEOUT_MS));
            // Sending latest data again
            xQueueSend(aht_queue, &data, pdMS_TO_TICKS(TIMEOUT_MS));
        }

        vTaskDelay(pdMS_TO_TICKS(AHT_READ_PERIOD_MS));
    } 
}

// ADC read task
void adc_task(void* arg) {

    LOGI("Starting adc_task");

    TWDT_ADD_TASK(adc_task);

    adc::data_t data = {};
    bool ret = false;

    while (1) {

        TWDT_RESET_FROM_TASK(adc_task);

        ret = power.get_measurement_data(data);
        if (!ret) {
            LOGW("Failed to read ADC data.");
            continue;
        }

        if (xQueueSend(power_queue, &data, pdMS_TO_TICKS(TIMEOUT_MS)) != pdTRUE) {
            // Removing oldest data
            adc::data_t dummy = {};
            xQueueReceive(power_queue, &dummy, pdMS_TO_TICKS(TIMEOUT_MS));
            // Sending latest data again
            xQueueSend(power_queue, &data, pdMS_TO_TICKS(TIMEOUT_MS));
        }

        vTaskDelay(pdMS_TO_TICKS(ADC_READ_PERIOD_MS));
    }
}

// Task to calculate runtime parameters and send to final_data_queue
void runtime_calc_task(void* arg) {

    LOGI("Starting runtime_calc_task");

    TWDT_ADD_TASK(runtime_calc_task);

    aht20_data_t aht_data = {};
    adc::data_t power_data = {};
    sys::data_t final_data = {};

    while (1) {

        TWDT_RESET_FROM_TASK(runtime_calc_task);

        if (xQueueReceive(aht_queue, &aht_data, pdMS_TO_TICKS(TIMEOUT_MS)) != pdTRUE) {
            LOGW("Data not received from aht data queue. Using stale data");
        }

        if (xQueueReceive(power_queue, &power_data, pdMS_TO_TICKS(TIMEOUT_MS)) != pdTRUE) {
            LOGW("Data not received from power data queue. Using stale data");
        }

        bool ret = sys::calc_total_runtime_stats(aht_data, power_data, final_data);
        if (!ret) {
            LOGW("Failed to calculate all run time parameters successfully");
            continue;
        }

        if (xQueueSend(final_data_queue, &final_data, pdMS_TO_TICKS(TIMEOUT_MS)) != pdTRUE) {
            // Removing oldest data
            sys::data_t dummy = {};
            xQueueReceive(final_data_queue, &dummy, pdMS_TO_TICKS(TIMEOUT_MS));
            // Sending latest data again
            xQueueSend(final_data_queue, &final_data, pdMS_TO_TICKS(TIMEOUT_MS));
        }

        xTaskNotifyGive(display_task_handle);

        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

// Task which controls UI updates
void display_task(void* arg) {

    LOGI("Starting display_task");

    TWDT_ADD_TASK(display_task);

    display::bootup_screen();
    // Arbitrary delay to allow the bootup screen flush properly before creating the UI
    vTaskDelay(pdMS_TO_TICKS(200));
    display::create_ui();

    QueueHandle_t btn_queue = button::get_queue();
    ASSERT(btn_queue, "btn_queue cannot be null");

    button::event_t event = button::event_t::NO_EVENT;
    sys::data_t data = {};

    while (1) {

        TWDT_RESET_FROM_TASK(display_task);
        
        if (xSemaphoreTake(lvgl_display_mutex, pdMS_TO_TICKS(TIMEOUT_MS * 2)) != pdTRUE) {
            LOGW("Failed to take lvgl_display_mutex");
            continue;
        }

        if (xQueueReceive(btn_queue, &event, 0) == pdTRUE) {
            if (event == button::event_t::NEXT_BUTTON_PRESSED) {
                LOGI("NEXT button pressed");
                display::next_screen();
            } else if (event == button::event_t::PREV_BUTTON_PRESSED) {
                LOGI("PREV button pressed");
                display::prev_screen();
            }
        }

        xSemaphoreGive(lvgl_display_mutex);

        // Block till runtime_calc_task tells us we have fresh data to update the screen with
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(TIMEOUT_MS));

        if (xSemaphoreTake(lvgl_display_mutex, pdMS_TO_TICKS(TIMEOUT_MS * 2)) != pdTRUE) {
            LOGW("Failed to take lvgl_display_mutex");
            continue;
        }

        if (xQueueReceive(final_data_queue, &data, pdMS_TO_TICKS(TIMEOUT_MS / 5)) != pdTRUE) {
            LOGW("Failed to receive data from final_data_queue");
            xSemaphoreGive(lvgl_display_mutex);
            continue;
        }

        display::update_data(data);

        xSemaphoreGive(lvgl_display_mutex);
    }
}


extern "C" {

    void app_main(void) {

        // Initialize all components
        init_all();

        // Create queues
        queue_create();

        lvgl_display_mutex = xSemaphoreCreateMutexStatic(&lvgl_display_mutex_buffer);

        // Create tasks
        calc_runtime_task_handle = xTaskCreateStaticPinnedToCore(
                                                                 runtime_calc_task, 
                                                                 "RuntimeCalcsTask", 
                                                                 CALC_TASK_STACK_SIZE, 
                                                                 nullptr, 
                                                                 CALC_TASK_PRIORITY,
                                                                 calc_task_stack,
                                                                 &calc_task_buffer, 
                                                                 CALC_TASK_CORE
        );
        ASSERT(calc_runtime_task_handle, "calc_runtime_task_handle cannot be null");

        aht_task_handle = xTaskCreateStaticPinnedToCore(
                                                        aht_task,
                                                        "AHTTask",
                                                        AHT_TASK_STACK_SIZE,
                                                        nullptr,
                                                        AHT_TASK_PRIORITY,
                                                        aht_task_stack,
                                                        &aht_task_buffer,
                                                        AHT_TASK_CORE
        );
        ASSERT(aht_task_handle, "aht_task_handle cannot be null");

        adc_task_handle = xTaskCreateStaticPinnedToCore(
                                                        adc_task,
                                                        "ADCTask",
                                                        ADC_TASK_STACK_SIZE,
                                                        nullptr,
                                                        ADC_TASK_PRIORITY,
                                                        adc_task_stack,
                                                        &adc_task_buffer,
                                                        ADC_TASK_CORE
        );
        ASSERT(adc_task_handle, "adc_task_handle cannot be null");

        display_task_handle = xTaskCreateStaticPinnedToCore(
                                                            display_task,
                                                            "DisplayTask",
                                                            DISPLAY_TASK_STACK_SIZE,
                                                            nullptr,
                                                            DISPLAY_TASK_PRIORITY,
                                                            display_task_stack,
                                                            &display_task_buffer,
                                                            DISPLAY_TASK_CORE
        );
        ASSERT(display_task_handle, "display_task_handle cannot be null");

        lvgl_task_handle = xTaskCreateStaticPinnedToCore(
                                                         lvgl_handler_task, 
                                                         "LVGLHandlerTask", 
                                                         LVGL_TASK_STACK_SIZE, 
                                                         nullptr, 
                                                         LVGL_TASK_PRIORITY, 
                                                         lvgl_task_stack,
                                                         &lvgl_task_buffer, 
                                                         LVGL_TASK_CORE
        );
        ASSERT(lvgl_task_handle, "lvgl_task_handle cannot be null");
    }
} // extern "C"