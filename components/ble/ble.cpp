#include "ble.hpp"

#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"

#include "host/ble_hs.h"
#include "host/util/util.h"
#include "host/ble_uuid.h"

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


// Set to 0 if nvs flash hasn't been initialized at the time of calling `ble::init()`
#define NVS_ALREADY_INITIALIZED                 0

// Set to 1 if you want `ble::deinit()` to deinitialize nvs flash
#define DEINIT_NVS_FROM_BLE_DEINIT              0

// We extern this function because the devs thought it was a good idea to not declare it in a header file
// Maybe I didn't search hard enough, but I doubt that
extern void ble_store_config_init(void);

namespace ble {

    static uint8_t address                                          = 0;
    static bool is_advertising                                      = false;
    static bool is_subscribed                                       = false;
    static constexpr const char BLE_GAP_NAME[]                      = "Inverter-Monitor";

    static constexpr auto AHT_SERVICE_UUID                          = BLE_UUID16_DECLARE(0x181A);
    static constexpr auto TEMPERATURE_CHAR_UUID                     = BLE_UUID16_DECLARE(0x2A6E);
    static constexpr auto HUMIDITY_CHAR_UUID                        = BLE_UUID16_DECLARE(0x2A6F);

    static constexpr auto ADC_SERVICE_UUID                          = BLE_UUID16_DECLARE(0x2B19);
    static constexpr auto VOLTAGE_CHAR_UUID                         = BLE_UUID16_DECLARE(0x2B18);
    static constexpr auto CURRENT_CHAR_UUID                         = BLE_UUID16_DECLARE(0x2AEE);
    static constexpr auto POWER_CHAR_UUID                           = BLE_UUID16_DECLARE(0x2B05);

    static constexpr auto BATTERY_SERVICE_UUID                      = BLE_UUID16_DECLARE(0x180F);
    static constexpr auto SoC_CHAR_UUID                             = BLE_UUID16_DECLARE(0x2A19);
    static constexpr auto RUNTIME_CHAR_UUID                         = BLE_UUID16_DECLARE(0x2A1A);

    // Forward declarations
    static void ble_advertise(void);
    static int ble_event_handler(ble_gap_event* event, void* arg);

    static int temperature_read(uint16_t conn_handle, uint16_t attr_handle, ble_gatt_access_ctxt *ctxt, void *arg);
    static int humidity_read(uint16_t conn_handle, uint16_t attr_handle, ble_gatt_access_ctxt *ctxt, void *arg);

    static int voltage_read(uint16_t conn_handle, uint16_t attr_handle, ble_gatt_access_ctxt *ctxt, void *arg);
    static int current_read(uint16_t conn_handle, uint16_t attr_handle, ble_gatt_access_ctxt *ctxt, void *arg);
    static int power_read(uint16_t conn_handle, uint16_t attr_handle, ble_gatt_access_ctxt *ctxt, void *arg);

    static int battery_soc_read(uint16_t conn_handle, uint16_t attr_handle, ble_gatt_access_ctxt *ctxt, void *arg);
    static int runtime_read(uint16_t conn_handle, uint16_t attr_handle, ble_gatt_access_ctxt *ctxt, void *arg);

    // Service definitions
    // Service for AHT data (temperature and humidity)
    static constexpr struct ble_gatt_svc_def aht_svc = {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = AHT_SERVICE_UUID,
        .characteristics = (struct ble_gatt_chr_def[]){
            // Temperature characteristics
            { .uuid = TEMPERATURE_CHAR_UUID, .access_cb = temperature_read, .arg = nullptr, .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY},
            // Humiidity characteristics
            { .uuid = HUMIDITY_CHAR_UUID, .access_cb = humidity_read, .arg = nullptr, .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY },
            // Characteristics array termination
            { 0 }
        }
    };

    // Service for ADC data (voltage, current and power data) characteristics
    static constexpr struct ble_gatt_svc_def adc_svc = {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = ADC_SERVICE_UUID,
        .characteristics = (struct ble_gatt_chr_def[]){
            // Voltage characteristics
            { .uuid = VOLTAGE_CHAR_UUID, .access_cb = voltage_read, .arg = nullptr, .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY },
            // Current characteristics
            { .uuid = CURRENT_CHAR_UUID, .access_cb = current_read, .arg = nullptr, .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY },
            // Power characteristics
            { .uuid = POWER_CHAR_UUID, .access_cb = power_read, .arg = nullptr, .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY },
            // Characteristics array termination
            { 0 }
        }
    };

    // Service for battery state and runtime/charge time characteristics
    static constexpr struct ble_gatt_svc_def batt_svc = {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BATTERY_SERVICE_UUID,
        .characteristics = (struct ble_gatt_chr_def[]){
            // Battery SoC characteristics
            { .uuid = SoC_CHAR_UUID, .access_cb = battery_soc_read, .arg = nullptr, .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY },
            // Runtime/Charge time characteristics
            { .uuid = RUNTIME_CHAR_UUID, .access_cb = runtime_read, .arg = nullptr, .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY },
            // Characteristics array termination
            { 0 }
        }
    };

    static constexpr struct ble_gatt_svc_def gatt_svc[] = {
        aht_svc, adc_svc, batt_svc, { 0 }
    };

    // Public APIs
    esp_err_t init(void) {

        esp_err_t ret = ESP_OK;

#if NVS_ALREADY_INITIALIZED == 0
        // Initialize the NVS flash partition
        ret = nvs_flash_init();
        if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
            ESP_ERROR_CHECK(nvs_flash_erase());
            ret = nvs_flash_init();
        }
        if (ret != ESP_OK) {
            BLE_LOGE("Failed to initialize nvs flash: %s", esp_err_to_name(ret));
            return ret;
        }
#endif

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

        // BLE Host settings
        // Called when a GATTS descriptor, service or characteristics is registered
        ble_hs_cfg.gatts_register_cb = [](struct ble_gatt_register_ctxt* ctxt, void* arg) {

            char buf[64]{};

            switch (ctxt->op) {
            case BLE_GATT_REGISTER_OP_SVC:
                BLE_LOGI("Registered GATTS service %s with handle = %d",
                         ble_uuid_to_str(ctxt->svc.svc_def->uuid, buf), ctxt->svc.handle);
                break;

            case BLE_GATT_REGISTER_OP_CHR:
                BLE_LOGI("Registering GATTS characteristic %s with def_handle = %d and val_handle = %d",
                         ble_uuid_to_str(ctxt->chr.chr_def->uuid, buf), ctxt->chr.def_handle, ctxt->chr.val_handle);
                break;

            case BLE_GATT_REGISTER_OP_DSC:
                BLE_LOGI("Registering GATTS descriptor %s with handle = %d",
                         ble_uuid_to_str(ctxt->dsc.dsc_def->uuid, buf), ctxt->dsc.handle);
                break;

            default:
                BLE_LOGW("Invalid ble gatts registration event");
                break;
            }
        };

        // @brief This is called when the the host and controller get synced
        // Determines the best address type to use for automatic address type resolution
        // @note We don't start advertising immediately as that is up to the user
        ble_hs_cfg.sync_cb = []() {
            ble_hs_id_infer_auto(0, &address);
        };

        // This is called when the host and controller get reset due to a fatal error
        ble_hs_cfg.reset_cb = [](int reason) {
            BLE_LOGE("Fatal error: Host and controller reset. Reason: %d", reason);
        };

        // Sets flag for generating keys for bonding/pairing
        ble_hs_cfg.sm_bonding = 1;

        // Local key distribution mask: long term key and identity resolving key
        ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;

        // User key distribution mask: same as above
        ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;

        // Auto pairing
        ble_hs_cfg.sm_io_cap = BLE_SM_IO_CAP_NO_IO;

        // No man in the middle
        ble_hs_cfg.sm_mitm = 0;

        // Secure connections
        ble_hs_cfg.sm_sc = 1;

        // This gets called when a persistence operation cannot be performed
        ble_hs_cfg.store_status_cb = [](struct ble_store_status_event* event, void *arg) {

            switch (event->event_code) {
            case BLE_STORE_EVENT_OVERFLOW:
                switch (event->overflow.obj_type) {
                case BLE_STORE_OBJ_TYPE_OUR_SEC:
                case BLE_STORE_OBJ_TYPE_PEER_SEC:
                case BLE_STORE_OBJ_TYPE_PEER_ADDR:
                    return ble_gap_unpair_oldest_peer();
                case BLE_STORE_OBJ_TYPE_CCCD:
                    // Try unpairing oldest peer except current peer
                    return ble_gap_unpair_oldest_except(&event->overflow.value->cccd.peer_addr);
                default:
                    return BLE_HS_EUNKNOWN;
                }
            
            case BLE_STORE_EVENT_FULL:
                // Just proceed with the operation.  If it results in an overflow,
                // we'll delete a record when the overflow occurs.
                return 0;
            
            default:
                return BLE_HS_EUNKNOWN;
            }
        };

        // Initialize NVS backed persistent storage for bonds
        ble_store_config_init();
        
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

        // Initialize GATT server
        ble_svc_gatt_init();

        // Start nimble freertos task
        nimble_port_freertos_init([](void* arg) {
            nimble_port_run();
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

#if DEINIT_NVS_FROM_BLE_DEINIT == 1
        // Deinitialize NVS storage partition
        ret = nvs_flash_deinit();
        if (ret != ESP_OK) {
            BLE_LOGE("Failed to deinitialize nvs flash: %s", esp_err_to_name(ret));
            return ret;
        }
#endif
        
        return ret;
    }

    esp_err_t notify_data(const sys::data_t& data) {

    }

    esp_err_t start(void) {

        if (is_advertising) {
            BLE_LOGW("Device already advertising");
            return ESP_OK;
        }

        ble_advertise();
        if (!is_advertising) {
            BLE_LOGE("Failed to start BLE advertising");
            return ESP_FAIL;
        }

        BLE_LOGI("Advertising started");

        return ESP_OK;
    }

    esp_err_t stop(void) {

        if (!is_advertising) {
            BLE_LOGW("Device not advertising");
            return ESP_OK;
        }

        int ret = ble_gap_adv_stop();
        if (ret != 0) {
            BLE_LOGE("Failed to stop advertising");
            return ESP_FAIL;
        }
        is_advertising = false;

        BLE_LOGI("Advertising stopped");

        return ESP_OK;
    }

    // Static helpers
    static void ble_advertise(void) {

        if (is_advertising) {
            BLE_LOGW("Device already advertising");
            return;
        }

        struct ble_hs_adv_fields adv_fields{};
        struct ble_gap_adv_params adv_params{};

        // Flags: General discoverable and BLE only
        adv_fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
        
        // TX power
        adv_fields.tx_pwr_lvl_is_present = 1;
        adv_fields.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;

        // Device name
        const char* name = ble_svc_gap_device_name();
        adv_fields.name = (uint8_t*)name;
        adv_fields.name_len = strlen(name);
        adv_fields.name_is_complete = 1;

        // UUID settings
        adv_fields.uuids16 = 0;
        adv_fields.num_uuids16 = 1;
        adv_fields.uuids16_is_complete = 1;

        // Set advertising fields
        int ret = ble_gap_adv_set_fields(&adv_fields);
        if (ret != 0) {
            BLE_LOGE("Error setting advertisement data: reason = %d", ret);
            return;
        }
        
        // Set advertising flags: connectable and general discoverable
        adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
        adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;

        // Start BLE advertising
        ret = ble_gap_adv_start(address, nullptr, BLE_HS_FOREVER, &adv_params, ble_event_handler, nullptr);
        if (ret != 0) {
            BLE_LOGE("Failed to start BLE advertising: reason = %d", ret);
            return;
        }
        
        is_advertising = true;
    }
    
    static int ble_event_handler(ble_gap_event* event, void* arg) {
        int ret = 0;

        switch (event->type) {
        case BLE_GAP_EVENT_CONNECT:
            
            break;
        case BLE_GAP_EVENT_DISCONNECT:
            
            break;
        case BLE_GAP_EVENT_SUBSCRIBE:
            
            break;
        case BLE_GAP_EVENT_ADV_COMPLETE:
            
            break;
        case BLE_GAP_EVENT_CONN_UPDATE:
            
            break;
        case BLE_GAP_EVENT_PASSKEY_ACTION:
            
            break;
        case BLE_GAP_EVENT_ENC_CHANGE:
            
            break;
        default:
            BLE_LOGW("Unknown event occured: ");
            break;
        }

        return ret;
    }

    static int temperature_read(uint16_t conn_handle, uint16_t attr_handle, ble_gatt_access_ctxt *ctxt, void *arg) {
        return 0;
    }\




    static int humidity_read(uint16_t conn_handle, uint16_t attr_handle, ble_gatt_access_ctxt *ctxt, void *arg) {

    }

    static int voltage_read(uint16_t conn_handle, uint16_t attr_handle, ble_gatt_access_ctxt *ctxt, void *arg) {

    }

    static int current_read(uint16_t conn_handle, uint16_t attr_handle, ble_gatt_access_ctxt *ctxt, void *arg) {

    }

    static int power_read(uint16_t conn_handle, uint16_t attr_handle, ble_gatt_access_ctxt *ctxt, void *arg) {

    }

    static int battery_soc_read(uint16_t conn_handle, uint16_t attr_handle, ble_gatt_access_ctxt *ctxt, void *arg) {

    }

    static int runtime_read(uint16_t conn_handle, uint16_t attr_handle, ble_gatt_access_ctxt *ctxt, void *arg) {

    }

} // namespace ble
