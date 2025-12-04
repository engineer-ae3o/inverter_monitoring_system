idf_component_register (
                        SRCS "button_handler.cpp"
                        INCLUDE_DIRS "."
                        REQUIRES freertos driver config esp_timer
)
