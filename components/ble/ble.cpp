#include "ble.hpp"
#include "ble_data.hpp"

#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"

#include "os/os_mbuf.h"

#include "host/ble_hs.h"
#include "host/util/util.h"
#include "host/ble_uuid.h"
#include "host/ble_gatt.h"

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

// We declare this function because the devs thought it was a good idea to not
// declare it in a header file. Maybe I didn't search hard enough
extern "C"{ void ble_store_config_init(void); }

// Pardon my type aliases, but i prefer suffixing my struct names with `_t`
// It's easier to work with. If you're wondering why i didn't do for all,
// it's because they're too many
using ble_gatt_svc_def_t = struct ble_gatt_svc_def;
using ble_gatt_chr_def_t = struct ble_gatt_chr_def;
using ble_gatt_access_ctxt_t = struct ble_gatt_access_ctxt;
using os_mbuf_t = struct os_mbuf;


namespace ble {

    // Don't know what to put here lol
    static uint8_t address_type                                      = 0;
    static uint16_t connection_handle                                = 0;

    // Flags
    static bool is_advertising                                       = false;
    static bool is_connected                                         = false;
    static bool is_subscribed                                        = false;

    // Constants
    static constexpr uint8_t MIN_KEY_SIZE                            = 6;
    static constexpr const char BLE_GAP_NAME[]                       = "Inverter-Monitor";

    // 16 bit UUIDs for all services and characteristics
    static constexpr ble_uuid16_t AHT_SERVICE_UUID                   = { .u = { .type = BLE_UUID_TYPE_16 }, .value = 0x181A };
    static constexpr ble_uuid16_t TEMPERATURE_CHAR_UUID              = { .u = { .type = BLE_UUID_TYPE_16 }, .value = 0x2A6E };
    static constexpr ble_uuid16_t HUMIDITY_CHAR_UUID                 = { .u = { .type = BLE_UUID_TYPE_16 }, .value = 0x2A6F };
    
    static constexpr ble_uuid16_t ADC_SERVICE_UUID                   = { .u = { .type = BLE_UUID_TYPE_16 }, .value = 0x2B19 };
    static constexpr ble_uuid16_t VOLTAGE_CHAR_UUID                  = { .u = { .type = BLE_UUID_TYPE_16 }, .value = 0x2B18 };
    static constexpr ble_uuid16_t CURRENT_CHAR_UUID                  = { .u = { .type = BLE_UUID_TYPE_16 }, .value = 0x2AEE };
    static constexpr ble_uuid16_t POWER_CHAR_UUID                    = { .u = { .type = BLE_UUID_TYPE_16 }, .value = 0x2B05 };
    
    static constexpr ble_uuid16_t BATTERY_SERVICE_UUID               = { .u = { .type = BLE_UUID_TYPE_16 }, .value = 0x180F };
    static constexpr ble_uuid16_t SoC_CHAR_UUID                      = { .u = { .type = BLE_UUID_TYPE_16 }, .value = 0x2A19 };
    static constexpr ble_uuid16_t RUNTIME_CHAR_UUID                  = { .u = { .type = BLE_UUID_TYPE_16 }, .value = 0x2A1A };

    // Handles for all characteristics. Needed for notifications
    static uint16_t temp_chr_handle                                  = 0;
    static uint16_t hmdt_chr_handle                                  = 0;
    static uint16_t voltage_chr_handle                               = 0;
    static uint16_t current_chr_handle                               = 0;
    static uint16_t power_chr_handle                                 = 0;
    static uint16_t battery_soc_chr_handle                           = 0;
    static uint16_t runtime_chr_handle                               = 0;

    // Forward declarations
    static void ble_advertise(void);
    static void fill_gatts_def(void);
    static int ble_event_handler(ble_gap_event* event, void* arg);

    // Callbacks for characteristics that get called when a client interacts with them
    static int temperature_chr(uint16_t conn_handle, uint16_t attr_handle, ble_gatt_access_ctxt_t* ctxt, void* arg);
    static int humidity_chr(uint16_t conn_handle, uint16_t attr_handle, ble_gatt_access_ctxt_t* ctxt, void* arg);
    static int voltage_chr(uint16_t conn_handle, uint16_t attr_handle, ble_gatt_access_ctxt_t* ctxt, void* arg);
    static int current_chr(uint16_t conn_handle, uint16_t attr_handle, ble_gatt_access_ctxt_t* ctxt, void* arg);
    static int power_chr(uint16_t conn_handle, uint16_t attr_handle, ble_gatt_access_ctxt_t* ctxt, void* arg);
    static int battery_soc_chr(uint16_t conn_handle, uint16_t attr_handle, ble_gatt_access_ctxt_t* ctxt, void* arg);
    static int runtime_chr(uint16_t conn_handle, uint16_t attr_handle, ble_gatt_access_ctxt_t* ctxt, void* arg);

    // Array of services
    static ble_gatt_svc_def_t gatt_svc[4] = {};
    static constexpr ble_uuid16_t gatt_svc_uuid[3] = { AHT_SERVICE_UUID, ADC_SERVICE_UUID, BATTERY_SERVICE_UUID };

    // Service for AHT data (temperature and humidity) characteristics
    static ble_gatt_chr_def_t aht_chr[3] = {};

    // Service for ADC data (voltage, current and power data) characteristics
    static ble_gatt_chr_def_t adc_chr[4] = {};

    // Service for battery state and runtime/charge time characteristics
    static ble_gatt_chr_def_t batt_chr[3] = {};


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

        // Initialize controller and nimble host stack
        ret = nimble_port_init();
        if (ret != ESP_OK) {
            BLE_LOGE("Failed to initialize controller and nimble host stack: %s", esp_err_to_name(ret));
            return ret;
        }

        // Initialize the VHCI transport layer between nimble's host layer and the ESP32's controller
        ret = esp_nimble_hci_init();
        if (ret != ESP_OK) {
            BLE_LOGE("Failed to initialize VHCI transport layer: %s", esp_err_to_name(ret));
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

            char buf[64] = {};

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
        // Determines the best address_type type to use for automatic address_type type resolution
        // @note We don't start advertising immediately as that is up to the user
        ble_hs_cfg.sync_cb = []() {
            ble_hs_id_infer_auto(1, &address_type);
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
        ble_hs_cfg.store_status_cb = [](struct ble_store_status_event* event, void* arg) {

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
        // Fill service struct definitions
        fill_gatts_def();

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

    // @note
    // If you are using a generic BLE app like the nRF Connect or some other app, it
    // will expect data in the format int16_t and an exponent of -2, so we multiply
    // by 100 and cast to int16_t. The app receives the bytes, divides by 100 and gets
    // its data and does whatever with it. Also, if you are sending to a big endian system,
    // swap the bytes before sending using `__builtin_bswap16()` if you are on gcc
    esp_err_t notify_data(const sys::data_t& data) {

        if (!is_connected || !is_subscribed) {
            BLE_LOGW("No BLE client connected or device not subscribed");
            return ESP_ERR_INVALID_STATE;
        }

        os_mbuf_t* om = nullptr;
        int ret = 0;
        esp_err_t result = ESP_OK;

        // Temperature
        int16_t temperature = static_cast<int16_t>(data.inv_temp * 100);
        om = ble_hs_mbuf_from_flat(&temperature, sizeof(temperature));
        if (om && temp_chr_handle) {
            ret = ble_gatts_notify_custom(connection_handle, temp_chr_handle, om);
            if (ret == 0) {
                BLE_LOGI("Temperature sent as notification");
            } else {
                BLE_LOGE("Failed to send temperature: %d", ret);
                result = ESP_FAIL;
            }
        } else {
            BLE_LOGE("Invalid temperature handle or mbuf allocation failed");
            result = ESP_FAIL;
        }

        // Humidity
        int16_t humidity = static_cast<int16_t>(data.inv_hmdt * 100);
        om = ble_hs_mbuf_from_flat(&humidity, sizeof(humidity));
        if (om && hmdt_chr_handle) {
            ret = ble_gatts_notify_custom(connection_handle, hmdt_chr_handle, om);
            if (ret == 0) {
                BLE_LOGI("Humidity sent as notification");
            } else {
                BLE_LOGE("Failed to send humidity: %d", ret);
                result = ESP_FAIL;
            }
        } else {
            BLE_LOGE("Invalid humidity handle or mbuf allocation failed");
            result = ESP_FAIL;
        }

        // Voltage
        int16_t voltage = static_cast<int16_t>(data.battery_voltage * 100);
        om = ble_hs_mbuf_from_flat(&voltage, sizeof(voltage));
        if (om && voltage_chr_handle) {
            ret = ble_gatts_notify_custom(connection_handle, voltage_chr_handle, om);
            if (ret == 0) {
                BLE_LOGI("Voltage sent as notification");
            } else {
                BLE_LOGE("Failed to send voltage: %d", ret);
                result = ESP_FAIL;
            }
        } else {
            BLE_LOGE("Invalid voltage handle or mbuf allocation failed");
            result = ESP_FAIL;
        }

        // Current
        int16_t current = static_cast<int16_t>(data.load_current_drawn * 100);
        om = ble_hs_mbuf_from_flat(&current, sizeof(current));
        if (om && current_chr_handle) {
            ret = ble_gatts_notify_custom(connection_handle, current_chr_handle, om);
            if (ret == 0) {
                BLE_LOGI("Current sent as notification");
            } else {
                BLE_LOGE("Failed to send current: %d", ret);
                result = ESP_FAIL;
            }
        } else {
            BLE_LOGE("Invalid current handle or mbuf allocation failed");
            result = ESP_FAIL;
        }

        // Power
        int16_t power = static_cast<int16_t>(data.power_drawn * 100);
        om = ble_hs_mbuf_from_flat(&power, sizeof(power));
        if (om && power_chr_handle) {
            ret = ble_gatts_notify_custom(connection_handle, power_chr_handle, om);
            if (ret == 0) {
                BLE_LOGI("Power sent as notification");
            } else {
                BLE_LOGE("Failed to send power: %d", ret);
                result = ESP_FAIL;
            }
        } else {
            BLE_LOGE("Invalid power handle or mbuf allocation failed");
            result = ESP_FAIL;
        }

        // Battery SoC
        int16_t battery_soc = static_cast<int16_t > (data.battery_percent * 100);
        om = ble_hs_mbuf_from_flat(&battery_soc, sizeof(battery_soc));
        if (om && battery_soc_chr_handle) {
            ret = ble_gatts_notify_custom(connection_handle, battery_soc_chr_handle, om);
            if (ret == 0) {
                BLE_LOGI("Battery SoC sent as notification");
            } else {
                BLE_LOGE("Failed to send battery SoC: %d", ret);
                result = ESP_FAIL;
            }
        } else {
            BLE_LOGE("Invalid battery SoC handle or mbuf allocation failed");
            result = ESP_FAIL;
        }

        // Runtime
        int16_t runtime = static_cast<int16_t>(data.runtime_left_s * 100);
        om = ble_hs_mbuf_from_flat(&runtime, sizeof(runtime));
        if (om && runtime_chr_handle) {
            ret = ble_gatts_notify_custom(connection_handle, runtime_chr_handle, om);
            if (ret == 0) {
                BLE_LOGI("Runtime sent as notification");
            } else {
                BLE_LOGE("Failed to send runtime: %d", ret);
                result = ESP_FAIL;
            }
        } else {
            BLE_LOGE("Invalid runtime handle or mbuf allocation failed");
            result = ESP_FAIL;
        }

        return result;
    }

    esp_err_t start(void) {

        if (is_advertising) {
            BLE_LOGW("Device already advertising");
            return ESP_ERR_INVALID_STATE;
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
            return ESP_ERR_INVALID_STATE;
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

        struct ble_hs_adv_fields adv_fields = {};
        struct ble_gap_adv_params adv_params = {};

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
        adv_fields.uuids16 = gatt_svc_uuid;
        adv_fields.num_uuids16 = 3;
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
        ret = ble_gap_adv_start(address_type, nullptr, BLE_HS_FOREVER, &adv_params, ble_event_handler, nullptr);
        if (ret != 0) {
            BLE_LOGE("Failed to start BLE advertising: reason = %d", ret);
            return;
        }
        
        is_advertising = true;
    }

    static int ble_event_handler(ble_gap_event* event, void* arg) {
        
        switch (event->type) {
        case BLE_GAP_EVENT_CONNECT:
            if (event->connect.status == 0) {
                is_connected = true;
                // Store connection handle to use for notifications
                connection_handle = event->connect.conn_handle;
                BLE_LOGI("Connection established");
            } else {
                BLE_LOGE("Connection failed. Resuming advertising");
                ble_advertise();
            }
            break;

        case BLE_GAP_EVENT_DISCONNECT:
            is_connected = false;
            is_subscribed = false;
            connection_handle = BLE_HS_CONN_HANDLE_NONE;

            // Resume advertising
            ble_advertise();
            break;

        case BLE_GAP_EVENT_SUBSCRIBE:
            is_subscribed = true;
            BLE_LOGI("Device subscribed");
            break;

        case BLE_GAP_EVENT_ADV_COMPLETE:
            is_advertising = false;
            is_connected = false;
            is_subscribed = false;
            BLE_LOGI("Advertising complete. Reason: %d", event->adv_complete.reason);
            break;

        default:
            BLE_LOGW("Unknown event occured: %d", event->type);
            break;
        }
        return 0;
    }

    static void fill_gatts_def(void) {

        // Service for AHT data (temperature and humidity)
        // Temperature characteristics
        aht_chr[0] = {
            .uuid = &TEMPERATURE_CHAR_UUID.u,
            .access_cb = temperature_chr,
            .arg = nullptr,
            .descriptors = nullptr,
            .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
            .min_key_size = MIN_KEY_SIZE,
            .val_handle = &temp_chr_handle
        };
        // Humidity characteristics
        aht_chr[1] = {
            .uuid = &HUMIDITY_CHAR_UUID.u,
            .access_cb = humidity_chr,
            .arg = nullptr,
            .descriptors = nullptr,
            .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
            .min_key_size = MIN_KEY_SIZE,
            .val_handle = &hmdt_chr_handle
        };
        // Characteristics array termination
        aht_chr[2] = {};

        // AHT data services definition
        gatt_svc[0].type = BLE_GATT_SVC_TYPE_PRIMARY;
        gatt_svc[0].uuid = &AHT_SERVICE_UUID.u;
        gatt_svc[0].characteristics = aht_chr;

        // Service for ADC data (voltage, current and power data) characteristics
        // Voltage characteristics
        adc_chr[0] = {
            .uuid = &VOLTAGE_CHAR_UUID.u,
            .access_cb = voltage_chr,
            .arg = nullptr,
            .descriptors = nullptr,
            .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
            .min_key_size = MIN_KEY_SIZE,
            .val_handle = &voltage_chr_handle
        };
        // Current characteristics
        adc_chr[1] = {
            .uuid = &CURRENT_CHAR_UUID.u,
            .access_cb = current_chr,
            .arg = nullptr,
            .descriptors = nullptr,
            .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
            .min_key_size = MIN_KEY_SIZE,
            .val_handle = &current_chr_handle
        };
        // Power characteristics
        adc_chr[2] = {
            .uuid = &POWER_CHAR_UUID.u,
            .access_cb = power_chr,
            .arg = nullptr,
            .descriptors = nullptr,
            .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
            .min_key_size = MIN_KEY_SIZE,
            .val_handle = &power_chr_handle
        };
        // Characteristics array termination
        adc_chr[3] = {};

        // ADC data services definition
        gatt_svc[1].type = BLE_GATT_SVC_TYPE_PRIMARY;
        gatt_svc[1].uuid = &ADC_SERVICE_UUID.u;
        gatt_svc[1].characteristics = adc_chr;

        // Service for battery state and runtime/charge time characteristics
        // Battery SoC characteristics
        batt_chr[0] = {
            .uuid = &SoC_CHAR_UUID.u,
            .access_cb = battery_soc_chr,
            .arg = nullptr,
            .descriptors = nullptr,
            .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
            .min_key_size = MIN_KEY_SIZE,
            .val_handle = &battery_soc_chr_handle
        };
        // Runtime/Charge time characteristics
        batt_chr[1] = {
            .uuid = &RUNTIME_CHAR_UUID.u,
            .access_cb = runtime_chr,
            .arg = nullptr,
            .descriptors = nullptr,
            .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
            .min_key_size = MIN_KEY_SIZE,
            .val_handle = &runtime_chr_handle
        };
        // Characteristics array termination
        batt_chr[2] = {};

        // Battery data services definition
        gatt_svc[2].type = BLE_GATT_SVC_TYPE_PRIMARY;
        gatt_svc[2].uuid = &BATTERY_SERVICE_UUID.u;
        gatt_svc[2].characteristics = batt_chr;

        // Services array termination
        gatt_svc[3] = {};
    }

    static int temperature_chr(uint16_t conn_handle, uint16_t attr_handle, ble_gatt_access_ctxt_t* ctxt, void* arg) {

        switch (ctxt->op) {
        case BLE_GATT_ACCESS_OP_READ_CHR: {
            int16_t temperature = static_cast<int16_t>(get_temperature() * 100);
            int ret = os_mbuf_append(ctxt->om, &temperature, sizeof(temperature));
            return ret;
        }

        // Characteristics is read only
        case BLE_GATT_ACCESS_OP_WRITE_CHR:
            return BLE_ATT_ERR_WRITE_NOT_PERMITTED;

        default:
            return BLE_ATT_ERR_UNLIKELY;
        }
        // To satisfy g++
        return BLE_ATT_ERR_UNLIKELY;
    }

    static int humidity_chr(uint16_t conn_handle, uint16_t attr_handle, ble_gatt_access_ctxt_t* ctxt, void* arg) {

        switch (ctxt->op) {
        case BLE_GATT_ACCESS_OP_READ_CHR: {
            int16_t humidity = static_cast<int16_t>(get_humidity() * 100);
            int ret = os_mbuf_append(ctxt->om, &humidity, sizeof(humidity));
            return ret;
        }

        // Characteristics is read only
        case BLE_GATT_ACCESS_OP_WRITE_CHR:
            return BLE_ATT_ERR_WRITE_NOT_PERMITTED;

        default:
            return BLE_ATT_ERR_UNLIKELY;
        }
        // To satisfy g++
        return BLE_ATT_ERR_UNLIKELY;
    }

    static int voltage_chr(uint16_t conn_handle, uint16_t attr_handle, ble_gatt_access_ctxt_t* ctxt, void* arg) {

        switch (ctxt->op) {
        case BLE_GATT_ACCESS_OP_READ_CHR: {
            int16_t voltage = static_cast<int16_t>(get_voltage() * 100);
            int ret = os_mbuf_append(ctxt->om, &voltage, sizeof(voltage));
            return ret;
        }

        // Characteristics is read only
        case BLE_GATT_ACCESS_OP_WRITE_CHR:
            return BLE_ATT_ERR_WRITE_NOT_PERMITTED;

        default:
            return BLE_ATT_ERR_UNLIKELY;
        }
        // To satisfy g++
        return BLE_ATT_ERR_UNLIKELY;
    }

    static int current_chr(uint16_t conn_handle, uint16_t attr_handle, ble_gatt_access_ctxt_t* ctxt, void* arg) {

        switch (ctxt->op) {
        case BLE_GATT_ACCESS_OP_READ_CHR: {
            int16_t current = static_cast<int16_t>(get_current() * 100);
            int ret = os_mbuf_append(ctxt->om, &current, sizeof(current));
            return ret;
        }

        // Characteristics is read only
        case BLE_GATT_ACCESS_OP_WRITE_CHR:
            return BLE_ATT_ERR_WRITE_NOT_PERMITTED;

        default:
            return BLE_ATT_ERR_UNLIKELY;
        }
        // To satisfy g++
        return BLE_ATT_ERR_UNLIKELY;
    }

    static int power_chr(uint16_t conn_handle, uint16_t attr_handle, ble_gatt_access_ctxt_t* ctxt, void* arg) {

        switch (ctxt->op) {
        case BLE_GATT_ACCESS_OP_READ_CHR: {
            int16_t power = static_cast<int16_t>(get_power() * 100);
            int ret = os_mbuf_append(ctxt->om, &power, sizeof(power));
            return ret;
        }

        // Characteristics is read only
        case BLE_GATT_ACCESS_OP_WRITE_CHR:
            return BLE_ATT_ERR_WRITE_NOT_PERMITTED;

        default:
            return BLE_ATT_ERR_UNLIKELY;
        }
        // To satisfy g++
        return BLE_ATT_ERR_UNLIKELY;
    }

    static int battery_soc_chr(uint16_t conn_handle, uint16_t attr_handle, ble_gatt_access_ctxt_t* ctxt, void* arg) {

        switch (ctxt->op) {
        case BLE_GATT_ACCESS_OP_READ_CHR: {
            int16_t battery_soc = static_cast<int16_t>(get_battery_soc() * 100);
            int ret = os_mbuf_append(ctxt->om, &battery_soc, sizeof(battery_soc));
            return ret;
        }

        // Characteristics is read only
        case BLE_GATT_ACCESS_OP_WRITE_CHR:
            return BLE_ATT_ERR_WRITE_NOT_PERMITTED;

        default:
            return BLE_ATT_ERR_UNLIKELY;
        }
        // To satisfy g++
        return BLE_ATT_ERR_UNLIKELY;
    }

    static int runtime_chr(uint16_t conn_handle, uint16_t attr_handle, ble_gatt_access_ctxt_t* ctxt, void* arg) {

        switch (ctxt->op) {
        case BLE_GATT_ACCESS_OP_READ_CHR: {
            int16_t runtime = static_cast<int16_t>(get_runtime() * 100);
            int ret = os_mbuf_append(ctxt->om, &runtime, sizeof(runtime));
            return ret;
        }

        // Characteristics is read only
        case BLE_GATT_ACCESS_OP_WRITE_CHR:
            return BLE_ATT_ERR_WRITE_NOT_PERMITTED;

        default:
            return BLE_ATT_ERR_UNLIKELY;
        }
        // To satisfy g++
        return BLE_ATT_ERR_UNLIKELY;
    }

} // namespace ble
