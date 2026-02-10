idf_component_register (
                        SRCS "main.cpp"
                        INCLUDE_DIRS "."
                        REQUIRES freertos power ili9341 st7735 config display aht button ble ili_test system
)
