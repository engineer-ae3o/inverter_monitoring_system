#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "ble.hpp"

#include "esp_nimble_hci.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"

#include "host/ble_hs.h"
#include "host/util/util.h"

#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_nimble_hci.h"

#include <cstdint>


#define BLE_DEBUG 1

#if BLE_DEBUG == 1
static const char* TAG = "BLE";
#define BLE_LOGI(...) ESP_LOGI(TAG, __VA_ARGS__)
#define BLE_LOGW(...) ESP_LOGW(TAG, __VA_ARGS__)
#define BLE_LOGE(...) ESP_LOGE(TAG, __VA_ARGS__)
#else 
#define BLE_LOGI(...)
#define BLE_LOGW(...)
#define BLE_LOGE(...)
#endif


namespace ble {

    static uint8_t address                                   = 0;
    static constexpr const char BLE_GAP_NAME[]               = "Inverter-Monitor";
    static constexpr auto DEVICE_UUID                        = BLE_UUID16_DECLARE(0x0180);
    static constexpr auto READ_UUID                          = BLE_UUID16_DECLARE(0xFFFF);
    static constexpr auto WRITE_UUID                         = BLE_UUID16_DECLARE(0xDDDD);

    static constexpr struct ble_gatt_svc_def gatt_svc[]      = {
        {
            .type = BLE_GATT_SVC_TYPE_PRIMARY,
            .uuid = DEVICE_UUID,
            .characteristics = (struct ble_gatt_chr_def[]){
                { .uuid = READ_UUID, .access_cb = device_read, .arg = nullptr, .flags = BLE_GATT_CHR_F_READ },
                { .uuid = WRITE_UUID, .access_cb = device_write, .arg = nullptr, .flags = BLE_GATT_CHR_F_WRITE },
                { 0 }
            }
        },
        { 0 }
    };

    // Forward declarations
    static void ble_advertise(void);
    static void ble_event_handler(struct ble_gap_event* event, void* arg);
    static int device_read(uint16_t conn_handle, uint16_t attr_handle, ble_gatt_access_ctxt *ctxt, void *arg);
    static int device_write(uint16_t conn_handle, uint16_t attr_handle, ble_gatt_access_ctxt *ctxt, void *arg);

    esp_err_t init(void) {

        // Initialize the NVS flash partition
        esp_err_t ret = nvs_flash_init();
        if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
            ESP_ERROR_CHECK(nvs_flash_erase());
            ret = nvs_flash_init();
        }
        if (ret != ESP_OK) {
            BLE_LOGE("Failed to initialize nvs flash: %s", esp_err_to_name(ret));
            return ret;
        }

        // Initialize the VHCI transport layer between nimble's host layer and the ESP32's controller
        ret = esp_nimble_hci_init();
        if (ret != ESP_OK) {
            BLE_LOGE("Failed to initialize VHCI transport layer: %s", esp_err_to_name(ret));
            return ret;
        }

        // Initialize controller and nimble host stack
        ret = nimble_port_init();
        if (ret != ESP_OK) {
            BLE_LOGE("Failed to initialize controller and nimble host stack: %s", esp_err_to_name(ret));
            return ret;
        }

        // GAP settings
        // Set GAP device name
        int rc = ble_svc_gap_device_name_set(BLE_GAP_NAME);
        if (rc != 0) {
            BLE_LOGE("Failed to set device GAP name");
            return ESP_FAIL;
        }

        // Set GAP appearance
        rc = ble_svc_gap_device_appearance_set(0);
        if (rc != 0) {
            BLE_LOGE("Failed to set device GAP appearance");
            return ESP_FAIL;
        }

        // Initialize GAP services
        ble_svc_gap_init();
        
        // GATT settings
        // Adjust host configuration settings
        rc = ble_gatts_count_cfg(gatt_svc);
        if (rc != 0) {
            BLE_LOGE("Failed to adjust host configuration object's settings");
            return ESP_FAIL;
        }

        // Queues service definitions for registration
        rc = ble_gatts_add_svcs(gatt_svc);
        if (rc != 0) {
            BLE_LOGE("Failed to queue service definitions for registration");
            return ESP_FAIL;
        }
        
        ble_hs_cfg.gatts_register_arg = nullptr;
        ble_hs_cfg.sync_cb = []() {
            ble_hs_id_infer_auto(0, &address);
            ble_advertise();
        };

        // Initialize GATT server
        ble_svc_gatt_init();

        // Start nimble freertos task
        nimble_port_freertos_init([](void* arg) {
            nimble_port_run();
            // If nimble_port_run returns, we delete the task
            vTaskDelete(nullptr);
        });

        return ret;
    }
    
    esp_err_t deinit(void) {

        // Stop nimble freertos task
        int rc = nimble_port_stop();
        if (rc != 0) {
            BLE_LOGE("Failed to stop nimble port task");
            return ESP_FAIL;
        }

        // Deinitialize nimble freertos port
        nimble_port_freertos_deinit();

        // Deinitialize controller and nimble's host stack
        esp_err_t ret = nimble_port_deinit();
        if (ret != ESP_OK) {
            BLE_LOGE("Failed to deinitialize controller and nimble host stack: %s", esp_err_to_name(ret));
            return ret;
        }

        // Deinitialize the VHCI transport layer between nimble's host layer and the ESP32's controller
        ret = esp_nimble_hci_deinit();
        if (ret != ESP_OK) {
            BLE_LOGE("Failed to deinitialize VHCI transport layer: %s", esp_err_to_name(ret));
            return ret;
        }

        // Deinitialize NVS storage partition
        ret = nvs_flash_deinit();
        if (ret != ESP_OK) {
            BLE_LOGE("Failed to deinitialize nvs flash: %s", esp_err_to_name(ret));
            return ret;
        }
        
        return ret;
    }

    // Static helpers
    static int device_read(uint16_t conn_handle, uint16_t attr_handle, ble_gatt_access_ctxt *ctxt, void *arg) {

    }

    static int device_write(uint16_t conn_handle, uint16_t attr_handle, ble_gatt_access_ctxt *ctxt, void *arg) {

    }

    static void ble_advertise(void) {

    }

    static void ble_event_handler(struct ble_gap_event* event, void* arg) {

    }
    
} // namespace ble
