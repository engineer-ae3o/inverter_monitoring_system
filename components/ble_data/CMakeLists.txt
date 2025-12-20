idf_component_register (
                        SRCS "ble_data.cpp"
                        INCLUDE_DIRS "."
                        REQUIRES freertos driver config esp_timer system
)
