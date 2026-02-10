#include "ili.h"
#include "esp_err.h"
#include "esp_log.h"
#include <string.h>


// Debug configuration
#define ILI_DEBUG 1

#if ILI_DEBUG == 1
static const char* TAG = "ili";
#define ILI_LOGI(...) ESP_LOGI(TAG, __VA_ARGS__)
#define ILI_LOGW(...) ESP_LOGW(TAG, __VA_ARGS__)
#define ILI_LOGE(...) ESP_LOGE(TAG, __VA_ARGS__)
#else
#define ILI_LOGI(...)
#define ILI_LOGW(...)
#define ILI_LOGE(...)
#endif

static ili_config_t handle = {};
static spi_device_handle_t device_handle = NULL;
static uint16_t pixels_buf[ILI_MAX_WIDTH * 32] = {};

static void spi_post_transfer_callback(spi_transaction_t *trans);
static inline void gpio_cleanup(void);
static inline void spi_cleanup(void);
static esp_err_t ili_send_cmd(uint8_t cmd);
static esp_err_t ili_send_data(const uint8_t* data, size_t len);
static void ili_hw_reset(void);
static esp_err_t ili_init_sequence(void);
static esp_err_t ili_set_window(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2);
static esp_err_t ili_send_pixels(const uint16_t* pixels, size_t count);


// ---------------------------------------------------------------------------
// Public functions
// ---------------------------------------------------------------------------
esp_err_t ili_init(const ili_config_t* config) {
    
    handle = *config;

    // Configure DC and RESET pins
    const gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << handle.pin_dc) | (1ULL << handle.pin_rst),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };

    esp_err_t ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        ILI_LOGE("GPIO config for DC and RST pins failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // Configure SPI bus
    const spi_bus_config_t bus_cfg = {
        .mosi_io_num = handle.pin_mosi,
        .miso_io_num = -1,
        .sclk_io_num = handle.pin_sclk,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = sizeof(pixels_buf)
    };

    ret = spi_bus_initialize(handle.spi_host, &bus_cfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) {
        ILI_LOGE("SPI bus init failed: %s", esp_err_to_name(ret));
        gpio_cleanup();
        return ret;
    }

    // Configure SPI device
    const spi_device_interface_config_t dev_cfg = {
        .clock_speed_hz = handle.spi_clock_speed_hz,
        .mode = 0,
        .spics_io_num = handle.pin_cs,
        .queue_size = 10,
        .post_cb = spi_post_transfer_callback,
        .flags = 0
    };

    ret = spi_bus_add_device(handle.spi_host, &dev_cfg, &device_handle);
    if (ret != ESP_OK) {
        ILI_LOGE("SPI device add failed: %s", esp_err_to_name(ret));
        gpio_cleanup();
        spi_cleanup();
        return ret;
    }

    // Hardware reset
    ili_hw_reset();

    // Send initialization sequence to the ILI9341
    ret = ili_init_sequence();
    if (ret != ESP_OK) {
        ILI_LOGE("Init sequence failed: %s", esp_err_to_name(ret));
        gpio_cleanup();
        spi_cleanup();
        return ret;
    }

    return ret;
}

esp_err_t ili_flush(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2,
                        const uint16_t* pixel_data, size_t pixel_count,
                    callback_t callback, void* arg) {
    esp_err_t ret = ili_set_window(x1, y1, x2, y2);
    if (ret != ESP_OK) return ret;
    ret = ili_send_pixels(pixel_data, pixel_count);
    if (callback) callback(arg, ret);
    return ret;
}

esp_err_t ili_set_screen(uint16_t color) {
    
    const size_t num_of_pixels = sizeof(pixels_buf) / sizeof(pixels_buf[0]);
    const uint32_t num_of_times_to_send_pixels = ILI_MAX_WIDTH * ILI_MAX_HEIGHT / num_of_pixels;
    const uint32_t offset = ILI_MAX_HEIGHT / num_of_times_to_send_pixels;
    uint32_t y1 = 0, y2 = offset;
    esp_err_t ret = ESP_OK;

    color = __builtin_bswap16(color);
    for (size_t i = 0; i < num_of_pixels; i++) {
        pixels_buf[i] = color;
    }

    // Send data in batches
    for (uint32_t i = 0; i < num_of_times_to_send_pixels; i++) {
        ret = ili_flush(0, y1, ILI_MAX_WIDTH - 1, y2, pixels_buf, num_of_pixels, NULL, NULL);
        if (ret != ESP_OK) return ret;
        y1 = y2;
        y2 += offset;
    }
    return ret;
}

// ---------------------------------------------------------------------------
// Static helpers
// ---------------------------------------------------------------------------
static void spi_post_transfer_callback(spi_transaction_t *trans) {
    // Do nothing
}

static inline void gpio_cleanup(void) {
    gpio_reset_pin(handle.pin_dc);
    gpio_reset_pin(handle.pin_rst);
}

static inline void spi_cleanup(void) {
    if (device_handle) {
        spi_bus_remove_device(device_handle);
        device_handle = NULL;
    }
    spi_bus_free(handle.spi_host);
}

static esp_err_t ili_send_cmd(uint8_t cmd) {

    gpio_set_level(handle.pin_dc, 0);  // Command mode

    spi_transaction_t trans = {
        .length = 8, // 8 bits
        .tx_buffer = &cmd,
        .flags = 0
    };

    return spi_device_polling_transmit(device_handle, &trans);
}

static esp_err_t ili_send_data(const uint8_t* data, size_t len) {

    gpio_set_level(handle.pin_dc, 1);  // Data mode

    spi_transaction_t trans = {
        .length = len * 8,
        .tx_buffer = data,
        .flags = 0
    };

    return spi_device_polling_transmit(device_handle, &trans);
}

static void ili_hw_reset(void) {

    gpio_set_level(handle.pin_rst, 0);
    vTaskDelay(pdMS_TO_TICKS(10));

    gpio_set_level(handle.pin_rst, 1);
    vTaskDelay(pdMS_TO_TICKS(120));
}

static esp_err_t ili_init_sequence(void) {

    // Software reset
    esp_err_t ret = ili_send_cmd(0x01);
    if (ret != ESP_OK) return ret;
    vTaskDelay(pdMS_TO_TICKS(150));

    ret = ili_send_cmd(0xEF);
    if (ret != ESP_OK) return ret;
    const uint8_t data_1[] = { 0x03, 0x80, 0x02 };
    ret = ili_send_data(data_1, sizeof(data_1));
    if (ret != ESP_OK) return ret;

    ret = ili_send_cmd(0xCF);
    if (ret != ESP_OK) return ret;
    const uint8_t data_2[] = { 0x00, 0xC1, 0x30 };
    ret = ili_send_data(data_2, sizeof(data_2));
    if (ret != ESP_OK) return ret;

    ret = ili_send_cmd(0xED);
    if (ret != ESP_OK) return ret;
    const uint8_t data_3[] = { 0x64, 0x03, 0x12, 0x81 };
    ret = ili_send_data(data_3, sizeof(data_3));
    if (ret != ESP_OK) return ret;

    ret = ili_send_cmd(0xE8);
    if (ret != ESP_OK) return ret;
    const uint8_t data_4[] = { 0x85, 0x00, 0x78 };
    ret = ili_send_data(data_4, sizeof(data_4));
    if (ret != ESP_OK) return ret;

    ret = ili_send_cmd(0xCB);
    if (ret != ESP_OK) return ret;
    const uint8_t data_5[] = { 0x39, 0x2C, 0x00, 0x34, 0x02 };
    ret = ili_send_data(data_5, sizeof(data_5));
    if (ret != ESP_OK) return ret;

    ret = ili_send_cmd(0xF7);
    if (ret != ESP_OK) return ret;
    const uint8_t data_6[] = { 0x20 };
    ret = ili_send_data(data_6, sizeof(data_6));
    if (ret != ESP_OK) return ret;

    ret = ili_send_cmd(0xE8);
    if (ret != ESP_OK) return ret;
    const uint8_t data_7[] = { 0x00, 0x00 };
    ret = ili_send_data(data_7, sizeof(data_7));
    if (ret != ESP_OK) return ret;

    ret = ili_send_cmd(0xC0);
    if (ret != ESP_OK) return ret;
    const uint8_t data_8[] = { 0x23 };
    ret = ili_send_data(data_8, sizeof(data_8));
    if (ret != ESP_OK) return ret;

    ret = ili_send_cmd(0xC1);
    if (ret != ESP_OK) return ret;
    const uint8_t data_9[] = { 0x10 };
    ret = ili_send_data(data_9, sizeof(data_9));
    if (ret != ESP_OK) return ret;

    ret = ili_send_cmd(0xC5);
    if (ret != ESP_OK) return ret;
    const uint8_t data_10[] = { 0x3E, 0x28 };
    ret = ili_send_data(data_10, sizeof(data_10));
    if (ret != ESP_OK) return ret;

    ret = ili_send_cmd(0xC7);
    if (ret != ESP_OK) return ret;
    const uint8_t data_11[] = { 0x86 };
    ret = ili_send_data(data_11, sizeof(data_11));
    if (ret != ESP_OK) return ret;

    ret = ili_send_cmd(0x37);
    if (ret != ESP_OK) return ret;
    const uint8_t data_12[] = { 0x00 };
    ret = ili_send_data(data_12, sizeof(data_12));
    if (ret != ESP_OK) return ret;

    ret = ili_send_cmd(0x3A);
    if (ret != ESP_OK) return ret;
    const uint8_t data_13[] = { 0x55 };
    ret = ili_send_data(data_13, sizeof(data_13));
    if (ret != ESP_OK) return ret;

    ret = ili_send_cmd(0xB1);
    if (ret != ESP_OK) return ret;
    const uint8_t data_14[] = { 0x00, 0x18 };
    ret = ili_send_data(data_14, sizeof(data_14));
    if (ret != ESP_OK) return ret;

    ret = ili_send_cmd(0xB6);
    if (ret != ESP_OK) return ret;
    const uint8_t data_15[] = { 0x08, 0x82, 0x27 };
    ret = ili_send_data(data_15, sizeof(data_15));
    if (ret != ESP_OK) return ret;

    ret = ili_send_cmd(0xF2);
    if (ret != ESP_OK) return ret;
    const uint8_t data_16[] = { 0x00 };
    ret = ili_send_data(data_16, sizeof(data_16));
    if (ret != ESP_OK) return ret;

    ret = ili_send_cmd(0x26);
    if (ret != ESP_OK) return ret;
    const uint8_t data_17[] = { 0x01 };
    ret = ili_send_data(data_17, sizeof(data_17));
    if (ret != ESP_OK) return ret;

    ret = ili_send_cmd(0xE0);
    if (ret != ESP_OK) return ret;
    const uint8_t data_18[] = { 0x0F, 0x31, 0x2B, 0x0C, 0x0E, 0x08, 0x4E, 0xF1, 0x37, 0x07, 0x10, 0x03, 0x0E, 0x09, 0x00 };
    ret = ili_send_data(data_18, sizeof(data_18));
    if (ret != ESP_OK) return ret;

    ret = ili_send_cmd(0xE1);
    if (ret != ESP_OK) return ret;
    const uint8_t data_19[] = { 0x00, 0x0E, 0x14, 0x03, 0x11, 0x07, 0x31, 0xC1, 0x48, 0x08, 0x0F, 0x0C, 0x31, 0x36, 0x0F };
    ret = ili_send_data(data_19, sizeof(data_19));
    if (ret != ESP_OK) return ret;

    // Memory access control (rotation)
    ret = ili_send_cmd(0x36);
    if (ret != ESP_OK) return ret;
    uint8_t mem_acc_ctrl[] = { 0x00 };
    switch (handle.rotation) {
    case 0:
        mem_acc_ctrl[0] = 0x08;
        break;
    case 1:
        mem_acc_ctrl[0] = 0x48;
        break;
    case 2:
        mem_acc_ctrl[0] = 0x88;
        break;
    case 3:
        mem_acc_ctrl[0] = 0xB8;
        break;
    default:
        mem_acc_ctrl[0] = 0x08;
        break;
    }
    ili_send_data(mem_acc_ctrl, sizeof(mem_acc_ctrl));
    if (ret != ESP_OK) return ret;

    // Display inversion OFF
    ret = ili_send_cmd(0x20);
    if (ret != ESP_OK) return ret;
    
    // Exit sleep
    ret = ili_send_cmd(0x11);
    if (ret != ESP_OK) return ret;
    vTaskDelay(pdMS_TO_TICKS(150));

    // Display ON
    ret = ili_send_cmd(0x29);
    if (ret != ESP_OK) return ret;
    vTaskDelay(pdMS_TO_TICKS(20));

    return ret;
}

static esp_err_t ili_set_window(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2) {
    // Column address set
    esp_err_t ret = ili_send_cmd(0x2A);
    if (ret != ESP_OK) return ret;

    // The ILI9341 uses big endian alignment so we send high byte first
    // Column address set
    const uint8_t caset_data[] = {
        (uint8_t)((x1 >> 8) & 0xFF),
        (uint8_t)(x1 & 0xFF),
        (uint8_t)((x2 >> 8) & 0xFF),
        (uint8_t)(x2 & 0xFF)
    };
    ret = ili_send_data(caset_data, sizeof(caset_data));
    if (ret != ESP_OK) return ret;

    // Row address set
    ret = ili_send_cmd(0x2B);
    if (ret != ESP_OK) return ret;

    const uint8_t raset_data[] = {
        (uint8_t)((y1 >> 8) & 0xFF),
        (uint8_t)(y1 & 0xFF),
        (uint8_t)((y2 >> 8) & 0xFF),
        (uint8_t)(y2 & 0xFF)
    }; 

    return ili_send_data(raset_data, sizeof(raset_data));
}

static esp_err_t ili_send_pixels(const uint16_t* pixels, size_t count) {

    // Memory write
    esp_err_t ret = ili_send_cmd(0x2C);
    if (ret != ESP_OK) return ret;

    gpio_set_level(handle.pin_dc, 1);  // Data mode

    spi_transaction_t trans = {
        .length = count * 16,  // 16 bits per pixel
        .tx_buffer = pixels,
        .flags = 0
    };
    
    return spi_device_polling_transmit(device_handle, &trans);
}
