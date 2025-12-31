#include "ili9341.h"

#include "esp_timer.h"
#include "esp_log.h"

#include <string.h>


// Debug configuration
#define ILI_DEBUG 0

#if ILI_DEBUG == 1
static const char* TAG = "ILI9341";
#define ILI_LOGI(...) ESP_LOGI(TAG, __VA_ARGS__)
#define ILI_LOGW(...) ESP_LOGW(TAG, __VA_ARGS__)
#define ILI_LOGE(...) ESP_LOGE(TAG, __VA_ARGS__)
#else
#define ILI_LOGI(...)
#define ILI_LOGW(...)
#define ILI_LOGE(...)
#endif


// ILI9341 commands
static const uint8_t ILI9341_SWRESET                          = 0x01;
static const uint8_t ILI9341_SLPOUT                           = 0x11;
static const uint8_t ILI9341_NORON                            = 0x13;
static const uint8_t ILI9341_INVOFF                           = 0x20;
static const uint8_t ILI9341_DISPON                           = 0x29;
static const uint8_t ILI9341_CASET                            = 0x2A;
static const uint8_t ILI9341_RASET                            = 0x2B;
static const uint8_t ILI9341_RAMWR                            = 0x2C;
static const uint8_t ILI9341_MADCTL                           = 0x36;
static const uint8_t ILI9341_COLMOD                           = 0x3A;


static ili9341_driver_t instances[ILI9341_MAX_INSTANCES] = {};
static DMA_ATTR uint16_t pixels_buf[ILI9341_MAX_INSTANCES][ILI9341_MAX_WIDTH * 10] = {}; // DMA buffer

static uint8_t instance_counter = 0;
SemaphoreHandle_t instance_counter_mutex = NULL;


// Forward declarations
static inline ili9341_handle_t get_instance(void);
static inline void cleanup_gpio(ili9341_handle_t handle);
static inline void cleanup_spi(ili9341_handle_t handle);
static void ili9341_task(void* arg);
static void spi_post_transfer_callback(spi_transaction_t* trans);
static esp_err_t ili9341_send_cmd(uint8_t cmd, ili9341_handle_t handle);
static esp_err_t ili9341_send_data(const uint8_t* data, size_t len, ili9341_handle_t handle);
static void ili9341_hw_reset(ili9341_handle_t handle);
static esp_err_t ili9341_init_sequence(ili9341_handle_t handle);
static esp_err_t ili9341_set_window(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, ili9341_handle_t handle);
static esp_err_t ili9341_send_pixels(const uint16_t* pixels, size_t count, ili9341_handle_t handle);
static void ili9341_cleanup_resources(ili9341_handle_t handle);


// Public functions
esp_err_t ili9341_init(const ili9341_config_t* config, ili9341_handle_t* handle) {

    if (!config) {
        ILI_LOGE("Invalid configuration data");
        return ESP_ERR_INVALID_ARG;
    }

    if (!*handle) {
        *handle = get_instance();
        if (!*handle) return ESP_ERR_NO_MEM;
    }

    if ((*handle)->is_initialized) {
        ILI_LOGW("Current instance already initialized");
        return ESP_OK;
    }

    if (!instance_counter_mutex) {
        instance_counter_mutex = xSemaphoreCreateMutex();
        if (!instance_counter_mutex) return ESP_FAIL;
    }

    // We create the mutex here because we need it to ensure thread safety
    (*handle)->handle_mutex = xSemaphoreCreateMutex();
    if (!(*handle)->handle_mutex) return ESP_FAIL;

    if (xSemaphoreTake((*handle)->handle_mutex, pdMS_TO_TICKS(ILI9341_TIMEOUT_MS)) != pdTRUE) {
        ILI_LOGE("Failed to take mutex during initialization");
        return ESP_ERR_TIMEOUT;
    }

    ILI_LOGI("Initializing ili9341 handle");

    // Store user configuration settings
    (*handle)->config = *config;

    // Apply defaults if not set
    if ((*handle)->config.queue_size == 0) {
        (*handle)->config.queue_size = ILI9341_DEFAULT_QUEUE_SIZE;
    }
    if ((*handle)->config.task_priority == 0) {
        (*handle)->config.task_priority = ILI9341_DEFAULT_TASK_PRIORITY;
    }
    if ((*handle)->config.task_stack_size == 0) {
        (*handle)->config.task_stack_size = ILI9341_DEFAULT_TASK_STACK_SIZE;
    }
    if ((*handle)->config.task_core > 1) {
        (*handle)->config.task_core = ILI9341_DEFAULT_TASK_CORE;
    }
    if ((*handle)->config.max_retries == 0) {
        (*handle)->config.max_retries = ILI9341_DEFAULT_MAX_RETRIES;
    }

    (*handle)->state = ILI9341_STATE_IDLE;
    (*handle)->shutdown_requested = false;

    // Configure DC and RESET pins
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << (*handle)->config.pin_dc) | (1ULL << (*handle)->config.pin_rst),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };

    esp_err_t ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
     ILI_LOGE("GPIO config for DC and RST pins failed: %s", esp_err_to_name(ret));
        xSemaphoreGive((*handle)->handle_mutex);
        return ret;
    }

    // Configure SPI bus
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = (*handle)->config.pin_mosi,
        .miso_io_num = -1,
        .sclk_io_num = (*handle)->config.pin_sclk,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = (*handle)->size_of_pixel_buf_bytes
    };

    ret = spi_bus_initialize((*handle)->config.spi_host, &bus_cfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) {
     ILI_LOGE("SPI bus init failed: %s", esp_err_to_name(ret));
        cleanup_gpio(*handle);
        xSemaphoreGive((*handle)->handle_mutex);
        return ret;
    }

    // Configure SPI device
    spi_device_interface_config_t dev_cfg = {
        .clock_speed_hz = (*handle)->config.spi_clock_speed_hz,
        .mode = 0,
        .spics_io_num = (*handle)->config.pin_cs,
        .queue_size = ILI9341_DEFAULT_QUEUE_SIZE,
        .post_cb = spi_post_transfer_callback,
        .flags = 0
    };

    ret = spi_bus_add_device((*handle)->config.spi_host, &dev_cfg, &(*handle)->spi);
    if (ret != ESP_OK) {
     ILI_LOGE("SPI device add failed: %s", esp_err_to_name(ret));
        cleanup_gpio(*handle);
        cleanup_spi(*handle);
        xSemaphoreGive((*handle)->handle_mutex);
        return ret;
    }

    // Create FreeRTOS primitives
    (*handle)->spi_done_sem = xSemaphoreCreateBinary();
    (*handle)->flush_queue = xQueueCreate((*handle)->config.queue_size, sizeof(ili9341_flush_req_t));

    if (!(*handle)->spi_done_sem || !(*handle)->flush_queue) {
        ILI_LOGE("Failed to create spi_done_sem and flush_queue");
        xSemaphoreGive((*handle)->handle_mutex);
        ili9341_cleanup_resources(*handle);
        return ESP_FAIL;
    }

    (*handle)->dma_semphr = xSemaphoreCreateBinary();
    if (!(*handle)->dma_semphr) {
        ILI_LOGE("Failed to create dma_semphr");
        xSemaphoreGive((*handle)->handle_mutex);
        ili9341_cleanup_resources(*handle);
        return ESP_FAIL;
    }

    // Give semaphore to indicate the DMA buffer's availability
    if (xSemaphoreGive((*handle)->dma_semphr) != pdTRUE) {
        ILI_LOGE("Failed to give dma_semphr");
        xSemaphoreGive((*handle)->handle_mutex);
        ili9341_cleanup_resources(*handle);
        return ESP_FAIL;
    }

    // Create background task
    BaseType_t ret = xTaskCreatePinnedToCore(ili9341_task, "ILI9341Task", (*handle)->config.task_stack_size, handle,
                                             (*handle)->config.task_priority, (*handle)->task_handle, (*handle)->config.task_core);
    if (ret != pdPASS) {
     ILI_LOGE("Failed to create task");
        xSemaphoreGive((*handle)->handle_mutex);
        ili9341_cleanup_resources(*handle); // This already handles the cleanup of the gpio and spi resources
        return ESP_FAIL;
    }

    // Hardware reset
    ili9341_hw_reset(*handle);

    // Send initialization sequence to ili9341
    ret = ili9341_init_sequence(*handle);
    if (ret != ESP_OK) {
     ILI_LOGE("Init sequence failed: %s", esp_err_to_name(ret));
        (*handle)->shutdown_requested = true;
        (*handle)->task_handle = NULL;
        xSemaphoreGive((*handle)->handle_mutex); // Release mutex before deletion
        ili9341_cleanup_resources(*handle);  // This cleans up all the created resources and cleans up the spi and gpio resources that have been created
        return ret;
    }
    
    (*handle)->is_initialized = true;

    xSemaphoreGive((*handle)->handle_mutex);

    ILI_LOGI("Initialization complete");

    return ESP_OK;
}

esp_err_t ili9341_deinit(ili9341_handle_t* handle) {

    if (!*handle) {
        ILI_LOGE("Invalid driver handle");
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake((*handle)->handle_mutex, pdMS_TO_TICKS(ILI9341_TIMEOUT_MS)) != pdTRUE) return ESP_ERR_TIMEOUT;

    if (!(*handle)->is_initialized) {
        ILI_LOGW("ili9341 already unintialized");
        xSemaphoreGive((*handle)->handle_mutex);
        return ESP_OK;
    }    

    ILI_LOGI("Deinitializing ili9341");

    // Get task handle of the currently running task
    (*handle)->deinit_task_handle = xTaskGetCurrentTaskHandle();

    // Signal task to shutdown
    (*handle)->shutdown_requested = true;

    if ((*handle)->flush_queue) {
        ili9341_flush_req_t dummy = {};
        xQueueSend((*handle)->flush_queue, &dummy, pdMS_TO_TICKS(ILI9341_TIMEOUT_MS)); // Unblock task if it was in a blocked state waiting for data from the queue
    }

    xSemaphoreGive((*handle)->handle_mutex);  // Give mutex back before blocking and deletion

    if ((*handle)->task_handle) {
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(ILI9341_TIMEOUT_MS)); // Block till we receive notification from the task to be deleted
    }

    ili9341_cleanup_resources(*handle);
    (*handle)->deinit_task_handle = NULL;

    // Decrement instance counter
    if (xSemaphoreTake(instance_counter_mutex, pdMS_TO_TICKS(ILI9341_TIMEOUT_MS)) != pdTRUE) {
        ILI_LOGE("Failed to take instance_counter_mutex");
        return ESP_ERR_TIMEOUT;
    }
    instance_counter--;
    const bool instance_counter_at_0 = (instance_counter == 0);
    xSemaphoreGive(instance_counter_mutex);

    // Delete instance_counter_mutex if instance_counter gets to 0
    if (instance_counter_at_0) {
        vSemaphoreDelete(instance_counter_mutex);
        instance_counter_mutex = NULL;
    }

    (*handle)->is_initialized = false;
    *handle = NULL;
    
    ILI_LOGI("Deinitialization complete");

    return ESP_OK;
}

esp_err_t ili9341_flush(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2,
                       const uint16_t* pixel_data, size_t pixel_count,
                       ili9341_flush_cb_t callback, void* user_data, ili9341_handle_t handle) {

    if (!handle) {
        ILI_LOGE("Invalid driver handle");
        if (callback) callback(user_data, ESP_ERR_INVALID_ARG);
        return ESP_ERR_INVALID_ARG;
    }
    
    if (xSemaphoreTake(handle->handle_mutex, pdMS_TO_TICKS(ILI9341_TIMEOUT_MS)) != pdTRUE) {
        if (callback) callback(user_data, ESP_ERR_TIMEOUT);
        return ESP_ERR_TIMEOUT;
    }

    if (!handle->is_initialized) {
        if (callback) callback(user_data, ESP_ERR_INVALID_STATE);
        xSemaphoreGive(handle->handle_mutex);
        return ESP_ERR_INVALID_STATE;
    }

    // Checking for invalid arguments
    if (!pixel_data || pixel_count == 0) {
        if (callback) callback(user_data, ESP_ERR_INVALID_ARG);
        xSemaphoreGive(handle->handle_mutex);
        return ESP_ERR_INVALID_ARG;
    }

    // Bounds checking
    if (pixel_count > handle->size_of_pixel_buf_bytes / sizeof(handle->pixels_buf[0])) {
        if (callback) callback(user_data, ESP_ERR_INVALID_SIZE);
        xSemaphoreGive(handle->handle_mutex);
        return ESP_ERR_INVALID_SIZE;
    }
    if (x1 >= handle->config.width || x2 >= handle->config.width || x1 > x2) {
        if (callback) callback(user_data, ESP_ERR_INVALID_ARG);
        xSemaphoreGive(handle->handle_mutex);
        return ESP_ERR_INVALID_ARG;
    }
    if (y1 >= handle->config.height || y2 >= handle->config.height || y1 > y2) {
        if (callback) callback(user_data, ESP_ERR_INVALID_ARG);
        xSemaphoreGive(handle->handle_mutex);
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(handle->dma_semphr, pdMS_TO_TICKS(ILI9341_TIMEOUT_MS)) != pdTRUE) {
        ILI_LOGE("DMA buffer in use for too long. Timing out from ili9341_flush()");
        if (callback) callback(user_data, ESP_ERR_TIMEOUT);
        xSemaphoreGive(handle->handle_mutex);
        return ESP_ERR_TIMEOUT;
    }

    // Copying the pixels data into dma capable memory and then swapping each pixel before queueing as the ili9341 is big endian
    for (size_t i = 0; i < pixel_count; i++) {
        handle->pixels_buf[i] = __builtin_bswap16(pixel_data[i]);
    }
    
    // Package flush request
    ili9341_flush_req_t req = {
        .x1 = x1,
        .y1 = y1,
        .x2 = x2,
        .y2 = y2,
        .pixels = handle->pixels_buf,
        .pixel_count = pixel_count,
        .callback = callback,
        .user_data = user_data
    };

    // Send to queue
    if (xQueueSend(handle->flush_queue, &req, pdMS_TO_TICKS(ILI9341_TIMEOUT_MS)) != pdTRUE) {

        ILI_LOGW("Flush queue full");
        xSemaphoreGive(handle->dma_semphr);
        if (callback) callback(user_data, ESP_ERR_NO_MEM);

        xSemaphoreGive(handle->handle_mutex);
        
        return ESP_ERR_NO_MEM;
    }

    xSemaphoreGive(handle->handle_mutex);

    return ESP_OK;
}

esp_err_t ili9341_set_screen(uint16_t color, ili9341_flush_cb_t callback, void* user_data, ili9341_handle_t handle) {

    if (!handle) {
        ILI_LOGE("Invalid driver handle");
        if (callback) callback(user_data, ESP_ERR_INVALID_ARG);
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(handle->handle_mutex, pdMS_TO_TICKS(ILI9341_TIMEOUT_MS)) != pdTRUE) {
     ILI_LOGE("Unable to take mutex");
        if (callback) callback(user_data, ESP_ERR_TIMEOUT);
        return ESP_ERR_TIMEOUT;
    }

    if (!handle->is_initialized) {
        if (callback) callback(user_data, ESP_ERR_INVALID_STATE);
        xSemaphoreGive(handle->handle_mutex);
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(handle->dma_semphr, pdMS_TO_TICKS(ILI9341_TIMEOUT_MS)) != pdTRUE) {
     ILI_LOGE("DMA buffer in use for too long. Timing out from ili9341_set_screen()");
        if (callback) callback(user_data, ESP_ERR_TIMEOUT);
        xSemaphoreGive(handle->handle_mutex);
        return ESP_ERR_TIMEOUT;
    }

    // Number of pixels that can be stored in the buffer per time
    const size_t num_of_pixels = handle->size_of_pixel_buf_bytes / sizeof(handle->pixels_buf[0]);
    color = __builtin_bswap16(color);

    for (size_t i = 0; i < num_of_pixels; i++) {
        handle->pixels_buf[i] = color;
    }

    const uint16_t num_of_times_to_send_pixels = handle->config.width * handle->config.height / num_of_pixels;
    const uint16_t offset = handle->config.height / num_of_times_to_send_pixels;
    uint16_t y1 = 0, y2 = offset;

    // Send data in batches
    for (uint16_t i = 0; i < num_of_times_to_send_pixels; i++) {    
        // Package flush request
        ili9341_flush_req_t req = {
            .x1 = 0,
            .y1 = y1,
            .x2 = handle->config.width - 1,
            .y2 = y2,
            .pixels = handle->pixels_buf,
            .pixel_count = num_of_pixels,
            // Only send callback when we are done with all data transfer
            .callback = (i == num_of_times_to_send_pixels - 1) ? callback : NULL,
            .user_data = user_data
        };

        // Send to queue
        if (xQueueSend(handle->flush_queue, &req, pdMS_TO_TICKS(ILI9341_TIMEOUT_MS)) != pdTRUE) {
            ILI_LOGW("Flush queue full");
            xSemaphoreGive(handle->dma_semphr);
            if (callback) callback(user_data, ESP_ERR_NO_MEM);
            xSemaphoreGive(handle->handle_mutex);
            return ESP_ERR_NO_MEM;
        }

        y1 = y2;
        y2 += offset;
    }

    xSemaphoreGive(handle->handle_mutex);

    return ESP_OK;
}

bool ili9341_is_ready(ili9341_handle_t handle) {

    if (!handle) {
        ILI_LOGE("Invalid driver handle");
        return ESP_ERR_INVALID_ARG;
    }

    if (!handle->is_initialized) {
        ILI_LOGW("Driver not initialized");
        return false;
    }

    if (xSemaphoreTake(handle->handle_mutex, pdMS_TO_TICKS(ILI9341_TIMEOUT_MS)) != pdTRUE) return false;
    bool ret = handle->state == ILI9341_STATE_IDLE;
    xSemaphoreGive(handle->handle_mutex);

    return ret;
}

// Helper functions
static inline ili9341_handle_t get_instance(void) {

    if (xSemaphoreTake(instance_counter_mutex, pdMS_TO_TICKS(ILI9341_TIMEOUT_MS)) != pdTRUE) return NULL;

    if (instance_counter >= ILI9341_MAX_INSTANCES) {
        xSemaphoreGive(instance_counter_mutex);
        return NULL;
    }

    // Save the current state of instance_counter before incrementing
    uint8_t idx = instance_counter;
    instance_counter++;

    xSemaphoreGive(instance_counter_mutex);

    // Zero out driver struct and pixel buffer incase of garbage data or from previous cycle
    memset(&instances[idx], 0, sizeof(ili9341_driver_t));
    memset(&pixels_buf[idx], 0, sizeof(pixels_buf[idx]));

    // Assign pointer to dma pixel buffer
    instances[idx].pixels_buf = pixels_buf[idx];
    instances[idx].size_of_pixel_buf_bytes = sizeof(pixels_buf[idx]);

    return &instances[idx];
}

static inline void cleanup_gpio(ili9341_handle_t handle) {
    gpio_reset_pin(handle->config.pin_dc);
    gpio_reset_pin(handle->config.pin_rst);
}

static inline void cleanup_spi(ili9341_handle_t handle) {
    if (handle->spi) {
        spi_bus_remove_device(handle->spi);
        handle->spi = NULL;
    }
    spi_bus_free(handle->config.spi_host);
}

static void ili9341_task(void* arg) {

    ILI_LOGI("ili9341_task started");

    ili9341_handle_t handle = (ili9341_handle_t)arg;
    ili9341_flush_req_t req = {};

    while (!handle->shutdown_requested) {

        // Wait for flush request from queue
        if (xQueueReceive(handle->flush_queue, &req, pdMS_TO_TICKS(ILI9341_TIMEOUT_MS)) == pdTRUE) {

            // Check to see if a shutdown has been requested so as not to process dummy data
            if (handle->shutdown_requested) break;

            // Mark handle as busy
            if (xSemaphoreTake(handle->handle_mutex, pdMS_TO_TICKS(ILI9341_TIMEOUT_MS)) == pdTRUE) {
                handle->state = ILI9341_STATE_BUSY;
                xSemaphoreGive(handle->handle_mutex);
            } else {
                // Indicate we are done with the DMA buffer
                xSemaphoreGive(handle->dma_semphr);
                if (req.callback) req.callback(req.user_data, ESP_ERR_TIMEOUT); // Invoke user callback
                continue;
            }

            esp_err_t ret = ESP_OK;

            for (int i = 1; i <= handle->config.max_retries; i++) {

                ret = ili9341_set_window(req.x1, req.y1, req.x2, req.y2, handle);
                if (ret != ESP_OK) {
                    ILI_LOGW("Attempt #%d: Failed to set pixel window", i);
                    continue;
                }

                ret = ili9341_send_pixels(req.pixels, req.pixel_count, handle);
                if (ret == ESP_OK) break;

                ILI_LOGW("Attempt #%d: Failed to send pixel data", i);
            }
            if (ret != ESP_OK) {
                ILI_LOGE("Failed to send pixels");
            }

            // Mark handle as idle
            if (xSemaphoreTake(handle->handle_mutex, pdMS_TO_TICKS(ILI9341_TIMEOUT_MS)) == pdTRUE) {
                handle->state = ILI9341_STATE_IDLE;
                xSemaphoreGive(handle->handle_mutex);
            }

            xSemaphoreGive(handle->dma_semphr);
            if (req.callback) req.callback(req.user_data, ret);
        }
    }

    if (handle->deinit_task_handle) {
        xTaskNotifyGive(handle->deinit_task_handle);
    }

    ILI_LOGI("ili9341_task shutting down");

    vTaskDelete(NULL);
}

static void IRAM_ATTR spi_post_transfer_callback(spi_transaction_t* trans) {
    // Signal completion from ISR
    BaseType_t higher_priority_task_woken = pdFALSE;
    ili9341_handle_t handle = (ili9341_handle_t)trans->user;
    xSemaphoreGiveFromISR(handle->spi_done_sem, &higher_priority_task_woken);
    if (higher_priority_task_woken) portYIELD_FROM_ISR();
}

static esp_err_t ili9341_send_cmd(uint8_t cmd, ili9341_handle_t handle) {

    gpio_set_level(handle->config.pin_dc, 0);  // Command mode

    spi_transaction_t trans = {
        .length = 8, // 8 bits
        .tx_buffer = &cmd,
        .flags = 0,
    };

    return spi_device_polling_transmit(handle->spi, &trans);
}

static esp_err_t ili9341_send_data(const uint8_t* data, size_t len, ili9341_handle_t handle) {

    if (!data || !len) {
        return ESP_ERR_INVALID_ARG;
    }

    gpio_set_level(handle->config.pin_dc, 1);  // Data mode

    spi_transaction_t trans = {
        .length = len * 8,
        .tx_buffer = data,
        .flags = 0,
    };

    return spi_device_polling_transmit(handle->spi, &trans);
}

static esp_err_t ili9341_send_pixels(const uint16_t* pixels, size_t count, ili9341_handle_t handle) {

    if (!pixels || !count) {
     ILI_LOGE("Passed invalid parameters");
        return ESP_ERR_INVALID_ARG;
    }

    // Memory write
    esp_err_t ret = ili9341_send_cmd(ILI9341_RAMWR, handle);
    if (ret != ESP_OK) return ret;

    gpio_set_level(handle->config.pin_dc, 1);  // Data mode

    spi_transaction_t trans = {
        .length = count * 16,  // 16 bits per pixel
        .tx_buffer = pixels,
        .user = handle,
        .flags = 0
    };

    // Queue transaction
    ret = spi_device_queue_trans(handle->spi, &trans, pdMS_TO_TICKS(ILI9341_TIMEOUT_MS));
    if (ret != ESP_OK) {
     ILI_LOGE("Pixel data queue failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // Wait for DMA completion
    if (xSemaphoreTake(handle->spi_done_sem, pdMS_TO_TICKS(ILI9341_TIMEOUT_MS)) != pdTRUE) {
     ILI_LOGE("DMA timeout on pixel data transaction");
        return ESP_ERR_TIMEOUT;
    }

    // Get transaction result
    spi_transaction_t* trans_out = NULL;
    ret = spi_device_get_trans_result(handle->spi, &trans_out, pdMS_TO_TICKS(ILI9341_TIMEOUT_MS));
    if (ret != ESP_OK) {
     ILI_LOGE("Get transaction result failed: %s", esp_err_to_name(ret));
    }

    return ret;
}

static void ili9341_hw_reset(ili9341_handle_t handle) {

    gpio_set_level(handle->config.pin_rst, 0);
    vTaskDelay(pdMS_TO_TICKS(10));

    gpio_set_level(handle->config.pin_rst, 1);
    vTaskDelay(pdMS_TO_TICKS(120));
}

static esp_err_t ili9341_init_sequence(ili9341_handle_t handle) {

    esp_err_t ret = ESP_OK;
    ILI_LOGI("Sending init sequence");

    // Software reset
    ret = ili9341_send_cmd(ILI9341_SWRESET, handle);
    if (ret != ESP_OK) return ret;
    vTaskDelay(pdMS_TO_TICKS(150));

    // Sleep out
    ret = ili9341_send_cmd(ILI9341_SLPOUT, handle);
    if (ret != ESP_OK) return ret;
    vTaskDelay(pdMS_TO_TICKS(500));

    // Frame rate control - normal mode
    ret = ili9341_send_cmd(0xB1, handle);
    if (ret != ESP_OK) return ret;
    const uint8_t frmctr1[] = {0x01, 0x2C, 0x2D};
    ret = ili9341_send_data(frmctr1, 3, handle);
    if (ret != ESP_OK) return ret;

    // Frame rate control - idle mode
    ret = ili9341_send_cmd(0xB2, handle);
    if (ret != ESP_OK) return ret;
    const uint8_t frmctr2[] = {0x01, 0x2C, 0x2D};
    ret = ili9341_send_data(frmctr2, 3, handle);
    if (ret != ESP_OK) return ret;

    // Frame rate control - partial mode
    ret = ili9341_send_cmd(0xB3, handle);
    if (ret != ESP_OK) return ret;
    const uint8_t frmctr3[] = {0x01, 0x2C, 0x2D, 0x01, 0x2C, 0x2D};
    ret = ili9341_send_data(frmctr3, 6, handle);
    if (ret != ESP_OK) return ret;

    // Display inversion control
    ret = ili9341_send_cmd(0xB4, handle);
    if (ret != ESP_OK) return ret;
    const uint8_t invctr = 0x07;
    ret = ili9341_send_data(&invctr, 1, handle);
    if (ret != ESP_OK) return ret;

    // Power control 1
    ret = ili9341_send_cmd(0xC0, handle);
    if (ret != ESP_OK) return ret;
    const uint8_t pwctr1[] = {0xA2, 0x02, 0x84};
    ret = ili9341_send_data(pwctr1, 3, handle);
    if (ret != ESP_OK) return ret;

    // Power control 2
    ret = ili9341_send_cmd(0xC1, handle);
    if (ret != ESP_OK) return ret;
    const uint8_t pwctr2 = 0xC5;
    ret = ili9341_send_data(&pwctr2, 1, handle);
    if (ret != ESP_OK) return ret;

    // Power control 3
    ret = ili9341_send_cmd(0xC2, handle);
    if (ret != ESP_OK) return ret;
    const uint8_t pwctr3[] = {0x0A, 0x00};
    ret = ili9341_send_data(pwctr3, 2, handle);
    if (ret != ESP_OK) return ret;

    // Power control 4
    ret = ili9341_send_cmd(0xC3, handle);
    if (ret != ESP_OK) return ret;
    const uint8_t pwctr4[] = {0x8A, 0x2A};
    ret = ili9341_send_data(pwctr4, 2, handle);
    if (ret != ESP_OK) return ret;

    // Power control 5
    ret = ili9341_send_cmd(0xC4, handle);
    if (ret != ESP_OK) return ret;
    const uint8_t pwctr5[] = {0x8A, 0xEE};
    ret = ili9341_send_data(pwctr5, 2, handle);
    if (ret != ESP_OK) return ret;

    // VCOM control
    ret = ili9341_send_cmd(0xC5, handle);
    if (ret != ESP_OK) return ret;
    const uint8_t vmctr1 = 0x0E;
    ret = ili9341_send_data(&vmctr1, 1, handle);
    if (ret != ESP_OK) return ret;

    // Display inversion off
    ret = ili9341_send_cmd(ILI9341_INVOFF, handle);
    if (ret != ESP_OK) return ret;

    // Memory access control (rotation)
    ret = ili9341_send_cmd(ILI9341_MADCTL, handle);
    if (ret != ESP_OK) return ret;
    uint8_t madctl;
    switch (handle->config.rotation) {
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
    ret = ili9341_send_data(&madctl, 1, handle);
    if (ret != ESP_OK) return ret;

    // Color mode: 16-bit
    ret = ili9341_send_cmd(ILI9341_COLMOD, handle);
    if (ret != ESP_OK) return ret;
    const uint8_t colmod = 0x05;
    ret = ili9341_send_data(&colmod, 1, handle);
    if (ret != ESP_OK) return ret;

    // Gamma correction - positive polarity
    ret = ili9341_send_cmd(0xE0, handle);
    if (ret != ESP_OK) return ret;
    const uint8_t gmctrp1[] = {
        0x02, 0x1c, 0x07, 0x12,
        0x37, 0x32, 0x29, 0x2D,
        0x29, 0x25, 0x2B, 0x39,
        0x00, 0x01, 0x03, 0x10
    };
    ret = ili9341_send_data(gmctrp1, 16, handle);
    if (ret != ESP_OK) return ret;

    // Gamma correction - negative polarity
    ret = ili9341_send_cmd(0xE1, handle);
    if (ret != ESP_OK) return ret;
    const uint8_t gmctrn1[] = {
        0x03, 0x1D, 0x07, 0x06,
        0x2E, 0x2C, 0x29, 0x2D,
        0x2E, 0x2E, 0x37, 0x3F,
        0x00, 0x00, 0x02, 0x10
    };
    ret = ili9341_send_data(gmctrn1, 16, handle);
    if (ret != ESP_OK) return ret;

    // Normal display mode
    ret = ili9341_send_cmd(ILI9341_NORON, handle);
    if (ret != ESP_OK) return ret;
    vTaskDelay(pdMS_TO_TICKS(10));

    // Display on
    ret = ili9341_send_cmd(ILI9341_DISPON, handle);
    if (ret != ESP_OK) return ret;
    vTaskDelay(pdMS_TO_TICKS(100));

    ILI_LOGI("Init sequence complete");

    return ret;
}

static esp_err_t ili9341_set_window(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, ili9341_handle_t handle) {

    esp_err_t ret = ESP_OK;

    // Column address set
    ret = ili9341_send_cmd(ILI9341_CASET, handle);
    if (ret != ESP_OK) return ret;

    // ili9341 uses big endian alignment so we send high byte first
    // Column address set
    const uint8_t caset_data[4] = {
        (x1 >> 8) & 0xFF,
        x1 & 0xFF,
        (x2 >> 8) & 0xFF,
        x2 & 0xFF
    };
    ret = ili9341_send_data(caset_data, 4, handle);
    if (ret != ESP_OK) return ret;

    // Row address set
    ret = ili9341_send_cmd(ILI9341_RASET, handle);
    if (ret != ESP_OK) return ret;

    const uint8_t raset_data[4] = {
        (y1 >> 8) & 0xFF,
        y1 & 0xFF,
        (y2 >> 8) & 0xFF,
        y2 & 0xFF
    }; 

    return ili9341_send_data(raset_data, 4, handle);
}

static void ili9341_cleanup_resources(ili9341_handle_t handle) {

    // Delete FreeRTOS objects
    if (handle->spi_done_sem) {
        vSemaphoreDelete(handle->spi_done_sem);
        handle->spi_done_sem = NULL;
    }

    if (handle->handle_mutex) {
        vSemaphoreDelete(handle->handle_mutex);
        handle->handle_mutex = NULL;
    }

    if (handle->flush_queue) {
        vQueueDelete(handle->flush_queue);
        handle->flush_queue = NULL;
    }

    if (handle->dma_semphr) {
        vSemaphoreDelete(handle->dma_semphr);
        handle->dma_semphr = NULL;
    }

    // Remove SPI device and bus and GPIOs used
    cleanup_gpio(handle);
    cleanup_spi(handle);
}
