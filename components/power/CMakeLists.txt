idf_component_register (
                        SRCS "power_monitor.cpp"
                        INCLUDE_DIRS "."
                        REQUIRES freertos esp_adc driver
)
