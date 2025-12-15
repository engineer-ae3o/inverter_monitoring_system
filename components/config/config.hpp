#ifndef _CONFIG_HPP_
#define _CONFIG_HPP_


#include "driver/gpio.h"
#include "driver/spi_master.h"

namespace config {
    // Tasks specifications
    constexpr inline uint16_t CALC_TASK_STACK_SIZE                   = 4096;
    constexpr inline uint16_t CALC_TASK_PRIORITY                     = 5;
    // We make CALC_TASK_PERIOD_MS the same as ADC_READ_PERIOD_MS because
    // it's the minimum period before new data will be available for runtime_calc_task.
    // I use the direct value of ADC_READ_PERIOD_MS because g++ is a whining bitch
    // The error message i got when i used ADC_READ_PERIOD_MS was <"ADC_READ_PERIOD_MS" is ambiguous>
    // whatever the fuck that means. So if you change the value of ADC_READ_PERIOD_MS,
    // remember to change it here
    constexpr inline uint16_t CALC_TASK_PERIOD_MS                    = 50;
    
    constexpr inline uint16_t DISPLAY_TASK_STACK_SIZE                = 8192;
    constexpr inline uint16_t DISPLAY_TASK_PRIORITY                  = 4;
    
    constexpr inline uint16_t AHT_TASK_STACK_SIZE                    = 4096;
    constexpr inline uint16_t AHT_TASK_PRIORITY                      = 6;
    constexpr inline uint16_t AHT_READ_PERIOD_MS                     = 2100;
    
    constexpr inline uint16_t ADC_TASK_STACK_SIZE                    = 4096;
    constexpr inline uint16_t ADC_TASK_PRIORITY                      = 7;
    constexpr inline uint16_t ADC_READ_PERIOD_MS                     = 50;
    
    constexpr inline uint16_t LVGL_TASK_STACK_SIZE                   = 8192;
    constexpr inline uint16_t LVGL_TASK_PRIORITY                     = 3;
    constexpr inline uint16_t LVGL_TASK_PERIOD_MS                    = 33;

    constexpr inline uint16_t LOG_TASK_STACK_SIZE                    = 4096;
    constexpr inline uint16_t LOG_TASK_PRIORITY                      = 2;
    constexpr inline uint16_t LOG_TASK_PERIOD_MS                     = 30 * 1000; // 30s
    
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
    constexpr inline gpio_num_t LED_PIN                              = GPIO_NUM_12;
    
    constexpr inline gpio_num_t BUTTON_PREV_PIN                      = GPIO_NUM_25;
    constexpr inline gpio_num_t BUTTON_NEXT_PIN                      = GPIO_NUM_26;
    
    // Button specification
    constexpr inline uint16_t BUTTON_DEBOUNCE_US                     = 50 * 1000;          // 30ms
    constexpr inline uint32_t BUTTON_LONG_PRESS_US                   = 2 * 1000 * 1000;    // 2s

    // File data
    constexpr inline uint32_t MAX_SAMPLES_TO_LOG                     = 50000;
    constexpr inline const char DATA_FILE_NAME[32]                   = "/storage/file_data.log";
    constexpr inline const char META_DATA_FILE_NAME[32]              = "/storage/file_meta_data.log";

    // LED brightness control
    constexpr inline uint32_t TIME_TO_LED_50_PERCENT_BRIGHTNESS_US   = 30 * 1000 * 1000;   // 30s
    constexpr inline uint32_t TIME_TO_LED_25_PERCENT_BRIGHTNESS_US   = 15 * 1000 * 1000;   // 15s
    constexpr inline uint32_t TIME_TO_LED_0_PERCENT_BRIGHTNESS_US    = 10 * 1000 * 1000;   // 10s
    
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