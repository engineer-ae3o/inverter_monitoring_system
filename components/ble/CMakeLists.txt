idf_component_register (
                        SRCS "ble.cpp"
                        INCLUDE_DIRS "."
                        REQUIRES freertos driver config esp_timer bt nvs_flash ble_data system
)
