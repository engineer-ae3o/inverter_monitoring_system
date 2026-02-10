#ifndef _ILI_H_
#define _ILI_H_

#ifdef __cplusplus
extern "C" {
#endif


#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_err.h"

#include <stdint.h>
#include <stdbool.h>


#define ILI_MAX_WIDTH  240
#define ILI_MAX_HEIGHT 320

typedef void (callback_t)(void* arg, esp_err_t ret);

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
    uint8_t rotation;  // 0-3 for different orientations
} ili_config_t;


esp_err_t ili_init(const ili_config_t* config);

esp_err_t ili_flush(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2,
                    const uint16_t* pixel_data, size_t pixel_count,
                    callback_t callback, void* arg);

esp_err_t ili_set_screen(uint16_t color);


#ifdef __cplusplus
}
#endif

#endif // _ILI_H_