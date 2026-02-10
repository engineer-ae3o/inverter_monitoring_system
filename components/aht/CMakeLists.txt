idf_component_register(
                    SRCS "aht20.c"
                    INCLUDE_DIRS "."
                    REQUIRES driver esp_timer freertos esp_driver_i2c
)
