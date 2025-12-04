idf_component_register (
                        SRCS "main.cpp"
                        INCLUDE_DIRS "."
                        REQUIRES freertos power st7735 config display aht button system
)
