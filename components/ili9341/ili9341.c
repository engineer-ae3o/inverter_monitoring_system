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


static ili9341_driver_t instances[ILI9341_MAX_INSTANCES] = {};
static DMA_ATTR uint16_t pixels_buf[ILI9341_MAX_INSTANCES][ILI9341_MAX_WIDTH * 36] = {}; // DMA buffer

static uint8_t instance_counter = 0;
SemaphoreHandle_t instance_counter_mutex = NULL;


// Forward declarations
static inline ili9341_handle_t get_instance(void);
static inline void gpio_cleanup(ili9341_handle_t handle);
static inline void spi_cleanup(ili9341_handle_t handle);
static void ili9341_task(void* arg);
static void IRAM_ATTR spi_post_transfer_callback(spi_transaction_t* trans);
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

    if (!instance_counter_mutex) {
        instance_counter_mutex = xSemaphoreCreateMutex();
        if (!instance_counter_mutex) return ESP_FAIL;
    }

    if (!*handle) {
        *handle = get_instance();
        if (!*handle) return ESP_ERR_NO_MEM;
    }

    if ((*handle)->is_initialized) {
        ILI_LOGW("Current instance already initialized");
        return ESP_OK;
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
    const gpio_config_t io_conf = {
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
    const spi_bus_config_t bus_cfg = {
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
        gpio_cleanup(*handle);
        xSemaphoreGive((*handle)->handle_mutex);
        return ret;
    }

    // Configure SPI device
    const spi_device_interface_config_t dev_cfg = {
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
        gpio_cleanup(*handle);
        spi_cleanup(*handle);
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
    BaseType_t rc = xTaskCreatePinnedToCore(ili9341_task, "ILI9341Task", (*handle)->config.task_stack_size, *handle,
                                            (*handle)->config.task_priority, &(*handle)->task_handle, (*handle)->config.task_core);
    if (rc != pdPASS) {
        ILI_LOGE("Failed to create task");
        xSemaphoreGive((*handle)->handle_mutex);
        ili9341_cleanup_resources(*handle);
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
        ili9341_cleanup_resources(*handle);
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

static inline void gpio_cleanup(ili9341_handle_t handle) {
    gpio_reset_pin(handle->config.pin_dc);
    gpio_reset_pin(handle->config.pin_rst);
}

static inline void spi_cleanup(ili9341_handle_t handle) {
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

static void spi_post_transfer_callback(spi_transaction_t* trans) {
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
        .user = handle
    };

    return spi_device_polling_transmit(handle->spi, &trans);
}

static esp_err_t ili9341_send_data(const uint8_t* data, size_t len, ili9341_handle_t handle) {

    if (!data || len == 0) return ESP_ERR_INVALID_ARG;

    gpio_set_level(handle->config.pin_dc, 1);  // Data mode

    spi_transaction_t trans = {
        .length = len * 8,
        .tx_buffer = data,
        .user = handle
    };

    return spi_device_polling_transmit(handle->spi, &trans);
}

static esp_err_t ili9341_send_pixels(const uint16_t* pixels, size_t count, ili9341_handle_t handle) {

    if (!pixels || count == 0) {
        ILI_LOGE("Passed invalid parameters");
        return ESP_ERR_INVALID_ARG;
    }

    // Memory write
    esp_err_t ret = ili9341_send_cmd(0x2C, handle);
    if (ret != ESP_OK) return ret;

    gpio_set_level(handle->config.pin_dc, 1);  // Data mode

    spi_transaction_t trans = {
        .length = count * 16,  // 16 bits per pixel
        .tx_buffer = pixels,
        .user = handle
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

    ILI_LOGI("Sending init sequence");

    // 1. Software Reset
    esp_err_t ret = ili9341_send_cmd(0x01, handle);
    if (ret != ESP_OK) return ret;
    vTaskDelay(pdMS_TO_TICKS(150));

    // 2. Sleep Out - Exit sleep mode
    ret = ili9341_send_cmd(0x11, handle);
    vTaskDelay(pdMS_TO_TICKS(150));

    // 3. Power Control A
    ret = ili9341_send_cmd(0xCB, handle);
    if (ret != ESP_OK) return ret;
    const uint8_t pwr_ctrl_a[] = { 0x39, 0x2C, 0x00, 0x34, 0x02 };
    ili9341_send_data(pwr_ctrl_a, sizeof(pwr_ctrl_a), handle);
    if (ret != ESP_OK) return ret;
    
    // 4. Power Control B
    ret = ili9341_send_cmd(0xCF, handle);
    if (ret != ESP_OK) return ret;
    const uint8_t pwr_ctrl_b[] = { 0x00, 0xC1, 0x30 };
    ili9341_send_data(pwr_ctrl_b, sizeof(pwr_ctrl_b), handle);
    if (ret != ESP_OK) return ret;
    
    // 5. Driver Timing Control A
    ret = ili9341_send_cmd(0xE8, handle);
    if (ret != ESP_OK) return ret;
    const uint8_t drv_tim_ctrl_a[] = { 0x85, 0x00, 0x78 };
    ili9341_send_data(drv_tim_ctrl_a, sizeof(drv_tim_ctrl_a), handle);
    if (ret != ESP_OK) return ret;
    
    // 6. Driver Timing Control B
    ret = ili9341_send_cmd(0xEA, handle);
    if (ret != ESP_OK) return ret;
    const uint8_t drv_tim_ctrl_b[] = { 0x00, 0x00 };
    ili9341_send_data(drv_tim_ctrl_b, sizeof(drv_tim_ctrl_b), handle);
    if (ret != ESP_OK) return ret;
    
    // 7. Power On Sequence Control
    ret = ili9341_send_cmd(0xED, handle);
    if (ret != ESP_OK) return ret;
    const uint8_t pwr_seq_ctrl[] = { 0x64, 0x03, 0x12, 0x81 };
    ili9341_send_data(pwr_seq_ctrl, sizeof(pwr_seq_ctrl), handle);
    if (ret != ESP_OK) return ret;
    
    // 8. Pump Ratio Control
    ret = ili9341_send_cmd(0xF7, handle);
    if (ret != ESP_OK) return ret;
    const uint8_t pmp_ratio[] = { 0x20 };
    ili9341_send_data(pmp_ratio, sizeof(pmp_ratio), handle);
    if (ret != ESP_OK) return ret;
    
    // 9. Power Control 1
    ret = ili9341_send_cmd(0xC0, handle);
    if (ret != ESP_OK) return ret;
    const uint8_t pwr_ctrl_1[] = { 0x23 };
    ili9341_send_data(pwr_ctrl_1, sizeof(pwr_ctrl_1), handle);
    if (ret != ESP_OK) return ret;
    
    // 10. Power Control 2
    ret = ili9341_send_cmd(0xC1, handle);
    if (ret != ESP_OK) return ret;
    const uint8_t pwr_ctrl_2[] = { 0x10 };
    ili9341_send_data(pwr_ctrl_2, sizeof(pwr_ctrl_2), handle);
    if (ret != ESP_OK) return ret;
    
    // 11. VCOM Control 1
    ret = ili9341_send_cmd(0xC5, handle);
    if (ret != ESP_OK) return ret;
    const uint8_t vcom_ctrl_1[] = { 0x3E, 0x28 };
    ili9341_send_data(vcom_ctrl_1, sizeof(vcom_ctrl_1), handle);
    if (ret != ESP_OK) return ret;
    
    // 12. VCOM Control 2
    ret = ili9341_send_cmd(0xC7, handle);
    if (ret != ESP_OK) return ret;
    const uint8_t vcom_ctrl_2[] = { 0x86 };
    ili9341_send_data(vcom_ctrl_2, sizeof(vcom_ctrl_2), handle);
    if (ret != ESP_OK) return ret;
    
    // 13. Memory Access Control (rotation)
    ret = ili9341_send_cmd(0x36, handle);
    if (ret != ESP_OK) return ret;
    uint8_t mem_acc_ctrl[] = { 0x48 };
    switch (handle->config.rotation) {
    case 0:
        mem_acc_ctrl[0] = 0x40;
        break;
    case 1:
        mem_acc_ctrl[0] = 0x20;
        break;
    case 2:
        mem_acc_ctrl[0] = 0x80;
        break;
    case 3:
        mem_acc_ctrl[0] = 0xE0;
        break;
    default:
        mem_acc_ctrl[0] = 0x48;
        break;
    }
    ili9341_send_data(mem_acc_ctrl, sizeof(mem_acc_ctrl), handle);
    if (ret != ESP_OK) return ret;
    
    // 14. Pixel Format Set
    ret = ili9341_send_cmd(0x3A, handle);
    if (ret != ESP_OK) return ret;
    const uint8_t pxl_format_set[] = { 0x55 };  // 16 bits/pixel (65K colors)
    ili9341_send_data(pxl_format_set, sizeof(pxl_format_set), handle);
    if (ret != ESP_OK) return ret;
    
    // 15. Frame Rate Control - Normal Mode
    ret = ili9341_send_cmd(0xB1, handle);
    if (ret != ESP_OK) return ret;
    const uint8_t frame_rate_ctrl[] = { 0x00, 0x18 };
    ili9341_send_data(frame_rate_ctrl, sizeof(frame_rate_ctrl), handle);
    if (ret != ESP_OK) return ret;
    
    // 16. Display Function Control
    ret = ili9341_send_cmd(0xB6, handle);
    if (ret != ESP_OK) return ret;
    const uint8_t disp_func_ctrl[] = { 0x08, 0x82, 0x27 };
    ili9341_send_data(disp_func_ctrl, sizeof(disp_func_ctrl), handle);
    if (ret != ESP_OK) return ret;
    
    // 17. Enable 3G - Disable 3 gamma
    ret = ili9341_send_cmd(0xF2, handle);
    if (ret != ESP_OK) return ret;
    const uint8_t gamma_disable[] = { 0x00 };
    ili9341_send_data(gamma_disable, sizeof(gamma_disable), handle);
    if (ret != ESP_OK) return ret;
    
    // 18. Gamma Set
    ret = ili9341_send_cmd(0x26, handle);
    if (ret != ESP_OK) return ret;
    const uint8_t gamma_set[] = { 0x01 };
    ili9341_send_data(gamma_set, sizeof(gamma_set), handle);
    if (ret != ESP_OK) return ret;
    
    // 19. Positive Gamma Correction
    ret = ili9341_send_cmd(0xE0, handle);
    if (ret != ESP_OK) return ret;
    const uint8_t positive_gamma_correct[] = { 0x0F, 0x31, 0x2B, 0x0C, 0x0E, 0x08, 0x4E, 0xF1, 0x37, 0x07, 0x10, 0x03, 0x0E, 0x09, 0x00 };
    ili9341_send_data(positive_gamma_correct, sizeof(positive_gamma_correct), handle);
    if (ret != ESP_OK) return ret;
    
    // 20. Negative Gamma Correction
    ret = ili9341_send_cmd(0xE1, handle);
    if (ret != ESP_OK) return ret;
    const uint8_t negative_gamma_correct[] = { 0x00, 0x0E, 0x14, 0x03, 0x11, 0x07, 0x31, 0xC1, 0x48, 0x08, 0x0F, 0x0C, 0x31, 0x36, 0x0F };
    ili9341_send_data(negative_gamma_correct, sizeof(negative_gamma_correct), handle);
    if (ret != ESP_OK) return ret;

    // Display inversion OFF
    ret = ili9341_send_cmd(0x20, handle);
    if (ret != ESP_OK) return ret;
    
    // 21. Display ON
    ret = ili9341_send_cmd(0x29, handle);
    if (ret != ESP_OK) return ret;
    vTaskDelay(pdMS_TO_TICKS(20));

    ILI_LOGI("ILI9341 initialization sequence complete");

    return ret;
}

static esp_err_t ili9341_set_window(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, ili9341_handle_t handle) {

    // Column address set
    esp_err_t ret = ili9341_send_cmd(0x2A, handle);
    if (ret != ESP_OK) return ret;

    // The ILI9341 uses big endian alignment so we send high byte first
    // Column address set
    const uint8_t caset_data[] = {
        (uint8_t)((x1 >> 8) & 0xFF),
        (uint8_t)(x1 & 0xFF),
        (uint8_t)((x2 >> 8) & 0xFF),
        (uint8_t)(x2 & 0xFF)
    };
    ret = ili9341_send_data(caset_data, sizeof(caset_data), handle);
    if (ret != ESP_OK) return ret;

    // Row address set
    ret = ili9341_send_cmd(0x2B, handle);
    if (ret != ESP_OK) return ret;

    const uint8_t raset_data[] = {
        (uint8_t)((y1 >> 8) & 0xFF),
        (uint8_t)(y1 & 0xFF),
        (uint8_t)((y2 >> 8) & 0xFF),
        (uint8_t)(y2 & 0xFF)
    }; 

    return ili9341_send_data(raset_data, sizeof(raset_data), handle);
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
    gpio_cleanup(handle);
    spi_cleanup(handle);
}
