#ifndef _ST7735_H_
#define _ST7735_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_err.h"

#include <stdint.h>
#include <stdbool.h>

#define ST7735_MAX_WIDTH  128
#define ST7735_MAX_HEIGHT 160

typedef struct {

    // SPI configuration
    spi_host_device_t spi_host;
    uint32_t spi_clock_speed_hz;

    // GPIO pins
    gpio_num_t pin_mosi;
    gpio_num_t pin_sclk;
    gpio_num_t pin_cs;
    gpio_num_t pin_dc;
    gpio_num_t pin_rst;

    // Display parameters
    uint16_t width;
    uint16_t height;
    uint8_t rotation;               // 0-3 for different orientations

    // Error handling
    uint8_t max_retries;            // Number of retry attempts on SPI failure (default: ST7735_DEFAULT_MAX_RETRIES)

    // Task configuration
    uint8_t queue_size;             // Flush request queue size (default: ST7735_DEFAULT_QUEUE_SIZE)
    uint8_t task_priority;          // FreeRTOS task priority (default: ST7735_DEFAULT_TASK_PRIORITY)
    uint8_t task_core;              // CPU core to pin task to (default: ST7735_DEFAULT_TASK_CORE)
    uint16_t task_stack_size;       // Task stack size in bytes (default: ST7735_DEFAULT_TASK_STACK_SIZE)

} st7735_config_t;

// Callback invoked when flush operation completes
typedef void (*st7735_flush_cb_t)(void* user_data, esp_err_t result);

/**
 * @brief Initialize ST7735 driver
 *
 * @param[in] config struct containing driver configuration
 * 
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t st7735_init(const st7735_config_t* config);
 
/**
 * @brief Deinitialize ST7735 driver and free resources
 *
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t st7735_deinit(void);
 
/**
 * @brief Async flush pixel data to display.
 * Non-blocking: returns immediately, callback invoked when transfer completes
 *
 * @param x1, y1 Top-left corner of update region
 * @param x2, y2 Bottom-right corner of update region
 * @param pixel_data RGB565 pixel buffer
 * @param pixel_count Number of pixels
 * @param callback Function to call when flush completes (receives result code)
 * @param user_data Passed to callback
 *
 * @return ESP_OK if data queued successfully, error code otherwise
 */
esp_err_t st7735_flush(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2,
     const uint16_t* pixel_data, size_t pixel_count,
     st7735_flush_cb_t callback, void* user_data);
 
/**
 * @brief Sets full screen to specified color in RGB565 format
 * 
 * @param[in] color Color to be displayed
 * @param[in] callback Function to call when flush completes (receives result code)
 * @param[in] user_data Passed to callback
 * 
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t st7735_set_screen(uint16_t color, st7735_flush_cb_t callback, void* user_data);
 
/**
 * @brief Check if driver is ready for new flush operation
 *
 * @return true if idle, false if busy
 */
bool st7735_is_ready(void);

#ifdef __cplusplus
}
#endif

#endif // _ST7735_H_