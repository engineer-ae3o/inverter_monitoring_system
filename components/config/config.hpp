#ifndef _CONFIG_HPP_
#define _CONFIG_HPP_


#include "driver/gpio.h"
#include "driver/spi_master.h"

namespace config {
    // Tasks specifications
    constexpr inline uint16_t CALC_TASK_STACK_SIZE                   = 4096;
    constexpr inline uint16_t CALC_TASK_PRIORITY                     = 3;
    constexpr inline uint16_t CALC_TASK_CORE                         = 0;
    
    constexpr inline uint16_t DISPLAY_TASK_STACK_SIZE                = 8192;
    constexpr inline uint16_t DISPLAY_TASK_PRIORITY                  = 5;
    constexpr inline uint16_t DISPLAY_TASK_CORE                      = 1;
    
    constexpr inline uint16_t AHT_TASK_STACK_SIZE                    = 3072;
    constexpr inline uint16_t AHT_TASK_PRIORITY                      = 4;
    constexpr inline uint16_t AHT_TASK_CORE                          = 0;
    constexpr inline uint16_t AHT_READ_PERIOD_MS                     = 1600;
    
    constexpr inline uint16_t ADC_TASK_STACK_SIZE                    = 3072;
    constexpr inline uint16_t ADC_TASK_PRIORITY                      = 5;
    constexpr inline uint16_t ADC_TASK_CORE                          = 0;
    constexpr inline uint16_t ADC_READ_PERIOD_MS                     = 20;
    
    constexpr inline uint16_t LVGL_TASK_STACK_SIZE                   = 8192;
    constexpr inline uint16_t LVGL_TASK_PRIORITY                     = 4;
    constexpr inline uint16_t LVGL_TASK_CORE                         = 1;
    constexpr inline uint16_t LVGL_TASK_PERIOD_MS                    = 33;
    
    // Pin definitions
    constexpr inline gpio_num_t AHT_SDA_PIN                          = GPIO_NUM_14;
    constexpr inline gpio_num_t AHT_SCL_PIN                          = GPIO_NUM_27;

    constexpr inline gpio_num_t CURRENT_SENSOR_PIN                   = GPIO_NUM_33;
    constexpr inline gpio_num_t VOLTAGE_SENSOR_PIN                   = GPIO_NUM_32;

    constexpr inline gpio_num_t MOSI_PIN                             = GPIO_NUM_18;
    constexpr inline gpio_num_t SCLK_PIN                             = GPIO_NUM_5;
    constexpr inline gpio_num_t CS_PIN                               = GPIO_NUM_22;
    constexpr inline gpio_num_t DC_PIN                               = GPIO_NUM_19;
    constexpr inline gpio_num_t RST_PIN                              = GPIO_NUM_21;
    constexpr inline gpio_num_t LED_PIN                              = GPIO_NUM_23;
    
    constexpr inline gpio_num_t BUTTON_PREV_PIN                      = GPIO_NUM_25;
    constexpr inline gpio_num_t BUTTON_NEXT_PIN                      = GPIO_NUM_26;
    
    // Button specification
    constexpr inline uint16_t BUTTON_DEBOUNCE_US                     = 50 * 1000;
    constexpr inline uint16_t BUTTON_LONG_PRESS_MS                   = 3 * 1000;
    
    // SPI and LCD Configuration details
    constexpr inline uint16_t LCD_WIDTH                              = 128;
    constexpr inline uint16_t LCD_HEIGHT                             = 160;
    constexpr inline uint16_t LCD_ROTATION                           = 4;
    constexpr inline uint16_t LCD_SPI_MAX_RETRIES                    = 4;
    constexpr inline uint32_t SPI_CLK_SPEED                          = 26700000; // 26.7MHz
    constexpr inline spi_host_device_t SPI_LCD_HOST                  = SPI2_HOST;
    
    // Queue parameters
    constexpr inline uint8_t QUEUE_LENGTH                            = 10;
    constexpr inline uint8_t TIMEOUT_MS                              = 100;
    
    // Inverter and battery specifications
    constexpr inline float BATT_ZERO_PERCENT_VOLTAGE                 = 6;
    constexpr inline float BATT_MAX_PERCENT_VOLTAGE                  = 12.6;
    constexpr inline float INVERTER_ACTIVE_THRESHOLD                 = 2;
    constexpr inline float BATTERY_RECHARGING_THRESHOLD              = -1.5;
    constexpr inline float BATTERY_DISCHARGING_THRESHOLD             = INVERTER_ACTIVE_THRESHOLD;
    constexpr inline float BATTERY_CAPACITY_AH                       = 35;   // It's 40Ah, but this is to take losses into account
}


#endif // _CONFIG_HPP_