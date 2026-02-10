#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"

#include "st7735.h"

#include "esp_timer.h"
#include "esp_log.h"

#include <string.h>


// Debug configuration
#define ST_DEBUG 0

#if ST_DEBUG == 1
static const char* TAG = "ST7735";
#define ST_LOGI(...) ESP_LOGI(TAG, __VA_ARGS__)
#define ST_LOGW(...) ESP_LOGW(TAG, __VA_ARGS__)
#define ST_LOGE(...) ESP_LOGE(TAG, __VA_ARGS__)
#else
#define ST_LOGI(...)
#define ST_LOGW(...)
#define ST_LOGE(...)
#endif


// ST7735 commands
static const uint8_t ST7735_SWRESET                          = 0x01;
static const uint8_t ST7735_SLPOUT                           = 0x11;
static const uint8_t ST7735_NORON                            = 0x13;
static const uint8_t ST7735_INVOFF                           = 0x20;
static const uint8_t ST7735_DISPON                           = 0x29;
static const uint8_t ST7735_CASET                            = 0x2A;
static const uint8_t ST7735_RASET                            = 0x2B;
static const uint8_t ST7735_RAMWR                            = 0x2C;
static const uint8_t ST7735_MADCTL                           = 0x36;
static const uint8_t ST7735_COLMOD                           = 0x3A;


// Configuration and default settings
#define ST7735_TIMEOUT_MS                                    50U
#define ST7735_DEFAULT_MAX_RETRIES                           3U
#define ST7735_DEFAULT_QUEUE_SIZE                            10U
#define ST7735_DEFAULT_TASK_PRIORITY                         8U
#define ST7735_DEFAULT_TASK_CORE                             1U
#define ST7735_DEFAULT_TASK_STACK_SIZE                       4096U


// Driver states
typedef enum {
    ST7735_STATE_IDLE,
    ST7735_STATE_BUSY
} st7735_state_t;

// Flush request structure
typedef struct {
    uint8_t x1, y1, x2, y2;
    const uint16_t* pixels;
    size_t pixel_count;
    st7735_flush_cb_t callback;
    void* user_data;
} st7735_flush_req_t;

// Driver context
typedef struct {

    spi_device_handle_t spi;
    st7735_config_t config;
    volatile st7735_state_t state;

    SemaphoreHandle_t spi_done_sem;            // Signaled by ISR when SPI completes
    QueueHandle_t flush_queue;                 // Queue of pending flush requests
    TaskHandle_t task_handle;                  // Background processing task
    SemaphoreHandle_t task_mutex;              // Mutex for thread safety

    volatile bool initialized;
    volatile bool shutdown_requested;

} st7735_driver_t;

static DMA_ATTR uint16_t pixels_buf[ST7735_MAX_WIDTH * 64] = {}; // DMA buffer: 64 lines of pixels
static SemaphoreHandle_t dma_semphr = NULL;                      // Semaphore to indicare DMA buffer availability

static TaskHandle_t deinit_task_handle = NULL;

// Global static instance
static st7735_driver_t driver = {};

// Forward declarations
static inline void cleanup_gpio(void);
static inline void cleanup_spi(void);
static void st7735_task(void* arg);
static void IRAM_ATTR spi_post_transfer_callback(spi_transaction_t* trans);
static esp_err_t st7735_send_cmd(uint8_t cmd);
static esp_err_t st7735_send_data(const uint8_t* data, size_t len);
static void st7735_hw_reset(void);
static esp_err_t st7735_init_sequence(void);
static esp_err_t st7735_set_window(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2);
static esp_err_t st7735_send_pixels(const uint16_t* pixels, size_t count);
static void st7735_cleanup_resources(void);


// Public functions
esp_err_t st7735_init(const st7735_config_t* config) {

    if (!config) {
        ST_LOGE("Invalid pointer to config struct");
        return ESP_ERR_INVALID_ARG;
    }

    // We create the mutex here because we need it to ensure thread safety
    driver.task_mutex = xSemaphoreCreateMutex();
    if (!driver.task_mutex) return ESP_FAIL;

    if (xSemaphoreTake(driver.task_mutex, pdMS_TO_TICKS(ST7735_TIMEOUT_MS)) != pdTRUE) {
        ST_LOGE("Failed to take task_mutex during initialization");
        return ESP_ERR_TIMEOUT;
    }

    if (driver.initialized) {
        ST_LOGW("Already initialized");
        return ESP_OK;
    }

    ST_LOGI("Initializing ST7735 driver");

    // Store user configuration settings
    driver.config = *config;

    // Apply defaults if not set
    if (driver.config.queue_size == 0) {
        driver.config.queue_size = ST7735_DEFAULT_QUEUE_SIZE;
    }
    if (driver.config.task_priority == 0) {
        driver.config.task_priority = ST7735_DEFAULT_TASK_PRIORITY;
    }
    if (driver.config.task_stack_size == 0) {
        driver.config.task_stack_size = ST7735_DEFAULT_TASK_STACK_SIZE;
    }
    if (driver.config.task_core > 1) {
        driver.config.task_core = ST7735_DEFAULT_TASK_CORE;
    }
    if (driver.config.max_retries == 0) {
        driver.config.max_retries = ST7735_DEFAULT_MAX_RETRIES;
    }

    driver.state = ST7735_STATE_IDLE;
    driver.shutdown_requested = false;

    // Configure DC and RESET pins
    const gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << driver.config.pin_dc) | (1ULL << driver.config.pin_rst),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };

    esp_err_t ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        ST_LOGE("GPIO config for DC and RST pins failed: %s", esp_err_to_name(ret));
        xSemaphoreGive(driver.task_mutex);
        return ret;
    }

    // Configure SPI bus
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = driver.config.pin_mosi,
        .miso_io_num = -1,
        .sclk_io_num = driver.config.pin_sclk,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = sizeof(pixels_buf)
    };

    ret = spi_bus_initialize(driver.config.spi_host, &bus_cfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) {
        ST_LOGE("SPI bus init failed: %s", esp_err_to_name(ret));
        cleanup_gpio();
        xSemaphoreGive(driver.task_mutex);
        return ret;
    }

    // Configure SPI device
    spi_device_interface_config_t dev_cfg = {
        .clock_speed_hz = driver.config.spi_clock_speed_hz,
        .mode = 0,
        .spics_io_num = driver.config.pin_cs,
        .queue_size = driver.config.queue_size,
        .post_cb = spi_post_transfer_callback,
        .flags = 0
    };

    ret = spi_bus_add_device(driver.config.spi_host, &dev_cfg, &driver.spi);
    if (ret != ESP_OK) {
        ST_LOGE("SPI device add failed: %s", esp_err_to_name(ret));
        cleanup_gpio();
        cleanup_spi();
        xSemaphoreGive(driver.task_mutex);
        return ret;
    }

    // Create FreeRTOS primitives
    driver.spi_done_sem = xSemaphoreCreateBinary();
    driver.flush_queue = xQueueCreate(driver.config.queue_size, sizeof(st7735_flush_req_t));

    if (!driver.spi_done_sem || !driver.flush_queue) {
        ST_LOGE("Failed to create FreeRTOS primitives");
        xSemaphoreGive(driver.task_mutex);
        st7735_cleanup_resources();
        return ESP_FAIL;
    }

    dma_semphr = xSemaphoreCreateBinary();
    if (!dma_semphr) {
        ST_LOGE("Failed to create dma_semphr");
        xSemaphoreGive(driver.task_mutex);
        st7735_cleanup_resources();
        return ESP_FAIL;
    }
    // Give semaphore to indicate the DMA buffer's availability
    if (xSemaphoreGive(dma_semphr) != pdTRUE) {
        ST_LOGE("Failed to give dma_semphr");
        xSemaphoreGive(driver.task_mutex);
        st7735_cleanup_resources();
        return ESP_FAIL;
    }

    // Create background task
    BaseType_t rc = xTaskCreatePinnedToCore(st7735_task, "ST7735Task", driver.config.task_stack_size, NULL,
                                             driver.config.task_priority, &driver.task_handle, driver.config.task_core);
    if (rc != pdPASS) {
        ST_LOGE("Failed to create task");
        xSemaphoreGive(driver.task_mutex);
        st7735_cleanup_resources();
        return ESP_FAIL;
    }

    // Hardware reset
    st7735_hw_reset();

    // Send initialization sequence to ST7735
    ret = st7735_init_sequence();
    if (ret != ESP_OK) {
        ST_LOGE("Init sequence failed: %s", esp_err_to_name(ret));
        driver.shutdown_requested = true;
        driver.task_handle = NULL;
        xSemaphoreGive(driver.task_mutex); // Release mutex before deletion
        st7735_cleanup_resources();
        return ret;
    }
    
    driver.initialized = true;

    xSemaphoreGive(driver.task_mutex);

    ST_LOGI("Initialization complete");

    return ESP_OK;
}

esp_err_t st7735_deinit(void) {

    if (xSemaphoreTake(driver.task_mutex, pdMS_TO_TICKS(ST7735_TIMEOUT_MS)) != pdTRUE) return ESP_ERR_TIMEOUT;

    if (!driver.initialized) {
        ST_LOGW("ST7735 already unintialized");
        xSemaphoreGive(driver.task_mutex);
        return ESP_OK;
    }    

    ST_LOGI("Deinitializing ST7735 driver");

    // Get task handle of the currently running task
    deinit_task_handle = xTaskGetCurrentTaskHandle();

    // Signal task to shutdown
    driver.shutdown_requested = true;

    if (driver.flush_queue) {
        st7735_flush_req_t dummy = {};
        xQueueSend(driver.flush_queue, &dummy, pdMS_TO_TICKS(ST7735_TIMEOUT_MS)); // Unblock task if it was in a blocked state waiting for data from the queue
    }

    xSemaphoreGive(driver.task_mutex);  // Give mutex back before blocking and deletion

    if (driver.task_handle) {
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(ST7735_TIMEOUT_MS)); // Block till we receive notification from the task to be deleted
    }

    deinit_task_handle = NULL;

    driver.initialized = false;
    
    ST_LOGI("Deinitialization complete");

    return ESP_OK;
}

bool st7735_is_ready(void) {

    if (!driver.initialized) {
        ST_LOGW("Driver not initialized");
        return false;
    }

    if (xSemaphoreTake(driver.task_mutex, pdMS_TO_TICKS(ST7735_TIMEOUT_MS)) != pdTRUE) return false;
    bool ret = driver.state == ST7735_STATE_IDLE;
    xSemaphoreGive(driver.task_mutex);

    return ret;
}

esp_err_t st7735_flush(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2,
                       const uint16_t* pixel_data, size_t pixel_count,
                       st7735_flush_cb_t callback, void* user_data) {
    
    if (xSemaphoreTake(driver.task_mutex, pdMS_TO_TICKS(ST7735_TIMEOUT_MS)) != pdTRUE) {
        if (callback) callback(user_data, ESP_ERR_TIMEOUT);
        return ESP_ERR_TIMEOUT;
    }

    if (!driver.initialized) {
        if (callback) callback(user_data, ESP_ERR_INVALID_STATE);
        xSemaphoreGive(driver.task_mutex);
        return ESP_ERR_INVALID_STATE;
    }

    // Checking for invalid arguments
    if (!pixel_data || pixel_count == 0) {
        if (callback) callback(user_data, ESP_ERR_INVALID_ARG);
        xSemaphoreGive(driver.task_mutex);
        return ESP_ERR_INVALID_ARG;
    }

    // Bounds checking
    if (x1 >= driver.config.width || x2 >= driver.config.width || x1 > x2) {
        if (callback) callback(user_data, ESP_ERR_INVALID_ARG);
        xSemaphoreGive(driver.task_mutex);
        return ESP_ERR_INVALID_ARG;
    }
    if (y1 >= driver.config.height || y2 >= driver.config.height || y1 > y2) {
        if (callback) callback(user_data, ESP_ERR_INVALID_ARG);
        xSemaphoreGive(driver.task_mutex);
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(dma_semphr, pdMS_TO_TICKS(ST7735_TIMEOUT_MS)) != pdTRUE) {
        ST_LOGE("DMA buffer in use for too long. Timing out from st7735_flush()");
        if (callback) callback(user_data, ESP_ERR_TIMEOUT);
        xSemaphoreGive(driver.task_mutex);
        return ESP_ERR_TIMEOUT;
    }

    // Copying the pixels data into dma capable memory and then swapping each pixel before queueing as the ST7735 is big endian
    for (size_t i = 0; i < pixel_count; i++) {
        pixels_buf[i] = __builtin_bswap16(pixel_data[i]);
    }

    // Package flush request
    st7735_flush_req_t req = {
        .x1 = x1,
        .y1 = y1,
        .x2 = x2,
        .y2 = y2,
        .pixels = pixels_buf,
        .pixel_count = pixel_count,
        .callback = callback,
        .user_data = user_data
    };

    // Send to queue
    if (xQueueSend(driver.flush_queue, &req, pdMS_TO_TICKS(ST7735_TIMEOUT_MS)) != pdTRUE) {
        ST_LOGW("Flush queue full");

        xSemaphoreGive(dma_semphr);
        if (callback) callback(user_data, ESP_ERR_NO_MEM);

        xSemaphoreGive(driver.task_mutex);
        
        return ESP_ERR_NO_MEM;
    }

    xSemaphoreGive(driver.task_mutex);

    return ESP_OK;
}

esp_err_t st7735_set_screen(uint16_t color, st7735_flush_cb_t callback, void* user_data) {

    if (xSemaphoreTake(driver.task_mutex, pdMS_TO_TICKS(ST7735_TIMEOUT_MS)) != pdTRUE) {
        ST_LOGE("Unable to take mutex");
        if (callback) callback(user_data, ESP_ERR_TIMEOUT);
        return ESP_ERR_TIMEOUT;
    }

    if (!driver.initialized) {
        if (callback) callback(user_data, ESP_ERR_INVALID_STATE);
        xSemaphoreGive(driver.task_mutex);
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(dma_semphr, pdMS_TO_TICKS(ST7735_TIMEOUT_MS)) != pdTRUE) {
        ST_LOGE("DMA buffer in use for too long. Timing out from st7735_set_screen()");
        if (callback) callback(user_data, ESP_ERR_TIMEOUT);
        xSemaphoreGive(driver.task_mutex);
        return ESP_ERR_TIMEOUT;
    }

    const size_t num_of_pixels = sizeof(pixels_buf) / sizeof(pixels_buf[0]);
    color = __builtin_bswap16(color);

    for (size_t i = 0; i < num_of_pixels; i++) {
        pixels_buf[i] = color;
    }

    const uint8_t num_of_times_to_send_pixels = driver.config.width * driver.config.height / num_of_pixels;
    const uint8_t offset = driver.config.height / num_of_times_to_send_pixels;
    uint8_t y1 = 0, y2 = offset;

    // Send data in batches
    for (uint8_t i = 0; i < num_of_times_to_send_pixels; i++) {    
        // Package flush request
        st7735_flush_req_t req = {
            .x1 = 0,
            .y1 = y1,
            .x2 = driver.config.width - 1,
            .y2 = y2,
            .pixels = pixels_buf,
            .pixel_count = num_of_pixels,
            // Only send callback when we are done with all data transfer
            .callback = (i == num_of_times_to_send_pixels - 1) ? callback : NULL,
            .user_data = user_data
        };

        // Send to queue
        if (xQueueSend(driver.flush_queue, &req, pdMS_TO_TICKS(ST7735_TIMEOUT_MS)) != pdTRUE) {
            ST_LOGW("Flush queue full");
            xSemaphoreGive(dma_semphr);
            if (callback) callback(user_data, ESP_ERR_NO_MEM);
            xSemaphoreGive(driver.task_mutex);
            return ESP_ERR_NO_MEM;
        }

        y1 = y2;
        y2 += offset;
    }

    xSemaphoreGive(driver.task_mutex);

    return ESP_OK;
}

// Helper functions
static inline void cleanup_gpio(void) {
    gpio_reset_pin(driver.config.pin_dc);
    gpio_reset_pin(driver.config.pin_rst);
}

static inline void cleanup_spi(void) {
    if (driver.spi) {
        spi_bus_remove_device(driver.spi);
        driver.spi = NULL;
    }
    spi_bus_free(driver.config.spi_host);
}

static void st7735_task(void* arg) {

    ST_LOGI("Task started");

    st7735_flush_req_t req = {};

    while (!driver.shutdown_requested) {

        // Wait for flush request so we can check shutdown flag
        if (xQueueReceive(driver.flush_queue, &req, pdMS_TO_TICKS(ST7735_TIMEOUT_MS)) == pdTRUE) {

            // Check to see if a shutdown has been requested so as not to process dummy data
            if (driver.shutdown_requested) break;

            // Mark driver as busy
            if (xSemaphoreTake(driver.task_mutex, pdMS_TO_TICKS(ST7735_TIMEOUT_MS)) == pdTRUE) {
                driver.state = ST7735_STATE_BUSY;
                xSemaphoreGive(driver.task_mutex);
            } else {
                // Indicate we are done with the DMA buffer
                xSemaphoreGive(dma_semphr);
                if (req.callback) req.callback(req.user_data, ESP_ERR_TIMEOUT); // Invoke user callback
                continue;
            }

            esp_err_t ret = ESP_OK;

            for (uint8_t i = 1; i <= driver.config.max_retries; i++) {

                ret = st7735_set_window(req.x1, req.y1, req.x2, req.y2);
                if (ret != ESP_OK) {
                    ST_LOGW("Attempt #%u: Failed to set pixel window", i);
                    continue;
                }

                ret = st7735_send_pixels(req.pixels, req.pixel_count);
                if (ret == ESP_OK) break;

                ST_LOGW("Attempt #%u: Failed to send pixel data", i);
            }
            if (ret != ESP_OK) {
                ST_LOGE("Could not send pixels");
            }

            // Mark driver as idle
            if (xSemaphoreTake(driver.task_mutex, pdMS_TO_TICKS(ST7735_TIMEOUT_MS)) == pdTRUE) {
                driver.state = ST7735_STATE_IDLE;
                xSemaphoreGive(driver.task_mutex);
            }

            xSemaphoreGive(dma_semphr);
            if (req.callback) req.callback(req.user_data, ret);
        }
    }

    if (deinit_task_handle) {
        xTaskNotifyGive(deinit_task_handle);
    }

    ST_LOGI("Task shutting down");

    st7735_cleanup_resources();
    vTaskDelete(NULL);
}

static void spi_post_transfer_callback(spi_transaction_t* trans) {
    // Signal completion from ISR
    BaseType_t higher_priority_task_woken = pdFALSE;
    xSemaphoreGiveFromISR(driver.spi_done_sem, &higher_priority_task_woken);
    if (higher_priority_task_woken) portYIELD_FROM_ISR();
}

static esp_err_t st7735_send_cmd(uint8_t cmd) {

    gpio_set_level(driver.config.pin_dc, 0);  // Command mode

    spi_transaction_t trans = {
        .length = 8, // 8 bits
        .tx_buffer = &cmd,
        .flags = 0,
    };

    return spi_device_polling_transmit(driver.spi, &trans);
}

static esp_err_t st7735_send_data(const uint8_t* data, size_t len) {

    if (!data || !len) {
        return ESP_ERR_INVALID_ARG;
    }

    gpio_set_level(driver.config.pin_dc, 1);  // Data mode

    spi_transaction_t trans = {
        .length = len * 8,
        .tx_buffer = data,
        .flags = 0,
    };

    return spi_device_polling_transmit(driver.spi, &trans);
}

static esp_err_t st7735_send_pixels(const uint16_t* pixels, size_t count) {

    if (!pixels || !count) {
        ST_LOGE("Passed inavlid parameters");
        return ESP_ERR_INVALID_ARG;
    }

    // Memory write
    esp_err_t ret = st7735_send_cmd(ST7735_RAMWR);
    if (ret != ESP_OK) return ret;

    gpio_set_level(driver.config.pin_dc, 1);  // Data mode

    spi_transaction_t trans = {
        .length = count * 16,  // 16 bits per pixel
        .tx_buffer = pixels,
        .flags = 0
    };

    // Queue transaction
    ret = spi_device_queue_trans(driver.spi, &trans, pdMS_TO_TICKS(ST7735_TIMEOUT_MS));
    if (ret != ESP_OK) {
        ST_LOGE("Pixel data queue failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // Wait for DMA completion
    if (xSemaphoreTake(driver.spi_done_sem, pdMS_TO_TICKS(ST7735_TIMEOUT_MS)) != pdTRUE) {
        ST_LOGE("DMA timeout on pixel data transaction");
        return ESP_ERR_TIMEOUT;
    }

    // Get transaction result
    spi_transaction_t* trans_out = NULL;
    ret = spi_device_get_trans_result(driver.spi, &trans_out, pdMS_TO_TICKS(ST7735_TIMEOUT_MS));
    if (ret != ESP_OK) {
        ST_LOGE("Get transaction result failed: %s", esp_err_to_name(ret));
    }

    return ret;
}

static void st7735_hw_reset(void) {

    gpio_set_level(driver.config.pin_rst, 0);
    vTaskDelay(pdMS_TO_TICKS(10));

    gpio_set_level(driver.config.pin_rst, 1);
    vTaskDelay(pdMS_TO_TICKS(120));
}

static esp_err_t st7735_init_sequence(void) {

    esp_err_t ret = ESP_OK;
    ST_LOGI("Sending init sequence");

    // Software reset
    ret = st7735_send_cmd(ST7735_SWRESET);
    if (ret != ESP_OK) return ret;
    vTaskDelay(pdMS_TO_TICKS(150));

    // Sleep out
    ret = st7735_send_cmd(ST7735_SLPOUT);
    if (ret != ESP_OK) return ret;
    vTaskDelay(pdMS_TO_TICKS(500));

    // Frame rate control - normal mode
    ret = st7735_send_cmd(0xB1);
    if (ret != ESP_OK) return ret;
    const uint8_t frmctr1[] = {0x01, 0x2C, 0x2D};
    ret = st7735_send_data(frmctr1, 3);
    if (ret != ESP_OK) return ret;

    // Frame rate control - idle mode
    ret = st7735_send_cmd(0xB2);
    if (ret != ESP_OK) return ret;
    const uint8_t frmctr2[] = {0x01, 0x2C, 0x2D};
    ret = st7735_send_data(frmctr2, 3);
    if (ret != ESP_OK) return ret;

    // Frame rate control - partial mode
    ret = st7735_send_cmd(0xB3);
    if (ret != ESP_OK) return ret;
    const uint8_t frmctr3[] = {0x01, 0x2C, 0x2D, 0x01, 0x2C, 0x2D};
    ret = st7735_send_data(frmctr3, 6);
    if (ret != ESP_OK) return ret;

    // Display inversion control
    ret = st7735_send_cmd(0xB4);
    if (ret != ESP_OK) return ret;
    const uint8_t invctr = 0x07;
    ret = st7735_send_data(&invctr, 1);
    if (ret != ESP_OK) return ret;

    // Power control 1
    ret = st7735_send_cmd(0xC0);
    if (ret != ESP_OK) return ret;
    const uint8_t pwctr1[] = {0xA2, 0x02, 0x84};
    ret = st7735_send_data(pwctr1, 3);
    if (ret != ESP_OK) return ret;

    // Power control 2
    ret = st7735_send_cmd(0xC1);
    if (ret != ESP_OK) return ret;
    const uint8_t pwctr2 = 0xC5;
    ret = st7735_send_data(&pwctr2, 1);
    if (ret != ESP_OK) return ret;

    // Power control 3
    ret = st7735_send_cmd(0xC2);
    if (ret != ESP_OK) return ret;
    const uint8_t pwctr3[] = {0x0A, 0x00};
    ret = st7735_send_data(pwctr3, 2);
    if (ret != ESP_OK) return ret;

    // Power control 4
    ret = st7735_send_cmd(0xC3);
    if (ret != ESP_OK) return ret;
    const uint8_t pwctr4[] = {0x8A, 0x2A};
    ret = st7735_send_data(pwctr4, 2);
    if (ret != ESP_OK) return ret;

    // Power control 5
    ret = st7735_send_cmd(0xC4);
    if (ret != ESP_OK) return ret;
    const uint8_t pwctr5[] = {0x8A, 0xEE};
    ret = st7735_send_data(pwctr5, 2);
    if (ret != ESP_OK) return ret;

    // VCOM control
    ret = st7735_send_cmd(0xC5);
    if (ret != ESP_OK) return ret;
    const uint8_t vmctr1 = 0x0E;
    ret = st7735_send_data(&vmctr1, 1);
    if (ret != ESP_OK) return ret;

    // Display inversion off
    ret = st7735_send_cmd(ST7735_INVOFF);
    if (ret != ESP_OK) return ret;

    // Memory access control (rotation)
    ret = st7735_send_cmd(ST7735_MADCTL);
    if (ret != ESP_OK) return ret;
    uint8_t madctl = 0;
    switch (driver.config.rotation) {
    case 0:
        madctl = 0xC0;
        break;
    case 1:
        madctl = 0xA0;
        break;
    case 2:
        madctl = 0x00;
        break;
    case 3:
        madctl = 0x60;
        break;
    default:
        madctl = 0xC0;
        break;
    }
    ret = st7735_send_data(&madctl, 1);
    if (ret != ESP_OK) return ret;

    // Color mode: 16-bit
    ret = st7735_send_cmd(ST7735_COLMOD);
    if (ret != ESP_OK) return ret;
    const uint8_t colmod = 0x05;
    ret = st7735_send_data(&colmod, 1);
    if (ret != ESP_OK) return ret;

    // Gamma correction - positive polarity
    ret = st7735_send_cmd(0xE0);
    if (ret != ESP_OK) return ret;
    const uint8_t gmctrp1[] = {
        0x02, 0x1c, 0x07, 0x12,
        0x37, 0x32, 0x29, 0x2D,
        0x29, 0x25, 0x2B, 0x39,
        0x00, 0x01, 0x03, 0x10
    };
    ret = st7735_send_data(gmctrp1, 16);
    if (ret != ESP_OK) return ret;

    // Gamma correction - negative polarity
    ret = st7735_send_cmd(0xE1);
    if (ret != ESP_OK) return ret;
    const uint8_t gmctrn1[] = {
        0x03, 0x1D, 0x07, 0x06,
        0x2E, 0x2C, 0x29, 0x2D,
        0x2E, 0x2E, 0x37, 0x3F,
        0x00, 0x00, 0x02, 0x10
    };
    ret = st7735_send_data(gmctrn1, 16);
    if (ret != ESP_OK) return ret;

    // Normal display mode
    ret = st7735_send_cmd(ST7735_NORON);
    if (ret != ESP_OK) return ret;
    vTaskDelay(pdMS_TO_TICKS(10));

    // Display on
    ret = st7735_send_cmd(ST7735_DISPON);
    if (ret != ESP_OK) return ret;
    vTaskDelay(pdMS_TO_TICKS(100));

    ST_LOGI("Init sequence complete");

    return ret;
}

static esp_err_t st7735_set_window(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2) {

    esp_err_t ret = ESP_OK;

    // Column address set
    ret = st7735_send_cmd(ST7735_CASET);
    if (ret != ESP_OK) return ret;

    // ST7735 uses big endian alignment so we send high byte first
    // Column address set
    const uint8_t caset_data[4] = {
        (x1 >> 8) & 0xFF,
        x1 & 0xFF,
        (x2 >> 8) & 0xFF,
        x2 & 0xFF
    };
    ret = st7735_send_data(caset_data, 4);
    if (ret != ESP_OK) return ret;

    // Row address set
    ret = st7735_send_cmd(ST7735_RASET);
    if (ret != ESP_OK) return ret;

    const uint8_t raset_data[4] = {
        (y1 >> 8) & 0xFF,
        y1 & 0xFF,
        (y2 >> 8) & 0xFF,
        y2 & 0xFF
    }; 

    return st7735_send_data(raset_data, 4);
}

static void st7735_cleanup_resources(void) {

    // Delete FreeRTOS objects
    if (driver.spi_done_sem) {
        vSemaphoreDelete(driver.spi_done_sem);
        driver.spi_done_sem = NULL;
    }

    if (driver.task_mutex) {
        vSemaphoreDelete(driver.task_mutex);
        driver.task_mutex = NULL;
    }

    if (driver.flush_queue) {
        vQueueDelete(driver.flush_queue);
        driver.flush_queue = NULL;
    }

    if (dma_semphr) {
        vSemaphoreDelete(dma_semphr);
        dma_semphr = NULL;
    }

    // Remove SPI device and bus and GPIOs used
    cleanup_gpio();
    cleanup_spi();
}
